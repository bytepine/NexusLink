// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusAssetEditUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/OutputDeviceNull.h"

static FString SupportedEditAction(const FString& Capability)
{
	if (Capability == TEXT("ManageAssetBlueprint")) return TEXT("SetDefaults");
	if (Capability == TEXT("ManageAssetDataTable")) return TEXT("Set");
	if (Capability == TEXT("ManageAssetUserWidget")) return TEXT("SetProperty");
	return FString();
}

bool FNexusAssetEditUtils::IsVerified(const TSharedPtr<FJsonObject>& Args)
{
	bool bVerify = false;
	if (Args.IsValid()) Args->TryGetBoolField(TEXT("bVerify"), bVerify);
	return bVerify;
}

bool FNexusAssetEditUtils::ValidateOperations(const FString& Capability, const TSharedPtr<FJsonObject>& Args, FString& Error)
{
	for (const TSharedPtr<FJsonValue>& Value : FNexusJsonUtils::ExtractOperations(Args))
	{
		if (!Value.IsValid() || Value->Type != EJson::Object) continue;
		const TSharedPtr<FJsonObject> Operation = Value->AsObject();
		if ((IsVerified(Args) || Operation->HasField(TEXT("ExpectedValue")))
			&& !FNexusArgs(Operation).Str(TEXT("Action")).Equals(SupportedEditAction(Capability), ESearchCase::IgnoreCase))
		{
			Error = TEXT("bVerify / ExpectedValue only support SetDefaults, DataTable Set, and Widget SetProperty");
			return false;
		}
	}
	return true;
}

struct FAssetEditProperty final
{
	UObject* Owner = nullptr;
	FProperty* Property = nullptr;
	void* Value = nullptr;
};

static bool ResolveEditProperty(UObject* Asset, const FString& Capability, const TSharedPtr<FJsonObject>& Operation,
	FAssetEditProperty& Resolved, FString& Error)
{
	const FNexusArgs Args(Operation);
	const FString Action = SupportedEditAction(Capability);
	if (!Asset || Action.IsEmpty() || !Args.Str(TEXT("Action")).Equals(Action, ESearchCase::IgnoreCase))
	{ Error = TEXT("Unsupported asset or operation for scalar preview/verification"); return false; }
	FString Path = Args.Str(TEXT("PropertyPath"));
	const UStruct* Struct = nullptr;
	void* Container = nullptr;
	if (Capability == TEXT("ManageAssetDataTable"))
	{
		UDataTable* Table = Cast<UDataTable>(Asset);
		if (!Table) { Error = TEXT("Asset is not a DataTable"); return false; }
		Resolved.Owner = Table;
		Struct = Table->GetRowStruct();
		Container = Table->FindRowUnchecked(FName(*Args.Str(TEXT("RowName"))));
		Path = Args.Str(TEXT("FieldName"));
	}
	else if (Capability == TEXT("ManageAssetUserWidget"))
	{
		UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Asset);
		Resolved.Owner = WidgetBP && WidgetBP->WidgetTree ? WidgetBP->WidgetTree->FindWidget(FName(*Args.Str(TEXT("WidgetName")))) : nullptr;
	}
	else
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
		Resolved.Owner = Blueprint && Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
	}
	if (!Resolved.Owner) { Error = TEXT("Target/CDO/Widget is unavailable; prepare the asset before preview"); return false; }
	if (Capability != TEXT("ManageAssetDataTable"))
	{ Struct = Resolved.Owner->GetClass(); Container = Resolved.Owner; }
	if (!Struct || !Container || Path.IsEmpty()) { Error = TEXT("Property path or DataTable row is missing"); return false; }
	TArray<FString> Segments;
	Path.ParseIntoArray(Segments, TEXT("."), false);
	for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
	{
		Resolved.Property = Struct->FindPropertyByName(FName(*Segments[SegmentIndex]));
		if (!Resolved.Property || Resolved.Property->ArrayDim != 1)
		{ Error = TEXT("Property not found or fixed-size array is unsupported"); return false; }
		Resolved.Value = Resolved.Property->ContainerPtrToValuePtr<void>(Container);
		if (SegmentIndex + 1 < Segments.Num())
		{
			FStructProperty* Nested = CastField<FStructProperty>(Resolved.Property);
			if (!Nested) { Error = TEXT("Only struct field traversal is supported; object/container traversal is rejected"); return false; }
			Struct = Nested->Struct; Container = Resolved.Value;
		}
	}
	FProperty* Property = Resolved.Property;
	if (!CastField<FBoolProperty>(Property) && !CastField<FNumericProperty>(Property) && !CastField<FEnumProperty>(Property)
		&& !CastField<FStrProperty>(Property) && !CastField<FNameProperty>(Property) && !CastField<FTextProperty>(Property))
	{ Error = TEXT("Unsupported property type; only bool, number, enum, string, Name and Text are supported"); return false; }
	return true;
}

static bool ParseEditValue(const FAssetEditProperty& Resolved, const FString& Input, void* Storage, FString& Normalized, FString& Error)
{
	FProperty* Property = Resolved.Property;
	const FString Trimmed = Input.TrimStartAndEnd();
	if (CastField<FBoolProperty>(Property) && !Trimmed.Equals(TEXT("true"), ESearchCase::IgnoreCase)
		&& !Trimmed.Equals(TEXT("false"), ESearchCase::IgnoreCase) && Trimmed != TEXT("0") && Trimmed != TEXT("1"))
	{ Error = TEXT("Invalid boolean value"); return false; }
	FString IntegerText;
	FNumericProperty* Numeric = CastField<FNumericProperty>(Property);
	if (Numeric && !Numeric->IsEnum())
	{
		if (Numeric->IsInteger())
		{
			IntegerText = Trimmed;
			bool bNegative = IntegerText.RemoveFromStart(TEXT("-"));
			if (!bNegative) IntegerText.RemoveFromStart(TEXT("+"));
			if (IntegerText.IsEmpty()) { Error = TEXT("Invalid integer value"); return false; }
			for (TCHAR Character : IntegerText)
				if (Character < TEXT('0') || Character > TEXT('9')) { Error = TEXT("Invalid integer value"); return false; }
			while (IntegerText.Len() > 1 && IntegerText[0] == TEXT('0')) IntegerText.RightChopInline(1);
			if (bNegative && IntegerText != TEXT("0")) IntegerText = TEXT("-") + IntegerText;
		}
		else
		{
			double Number = 0;
			if (!FDefaultValueHelper::ParseDouble(Trimmed, Number) || !FMath::IsFinite(Number))
			{ Error = TEXT("Invalid or nonfinite numeric value"); return false; }
		}
	}
	FOutputDeviceNull ParseErrors;
#if NX_UE_HAS_IMPORT_TEXT_DIRECT
	const TCHAR* End = Property->ImportText_Direct(*Input, Storage, Resolved.Owner, PPF_None, &ParseErrors);
#else
	const TCHAR* End = Property->ImportText(*Input, Storage, PPF_None, Resolved.Owner, &ParseErrors);
#endif
	if (!End || !FString(End).TrimStartAndEnd().IsEmpty()) { Error = TEXT("Value does not match the property type"); return false; }
	FNexusPropertyUtils::ExportText(Property, Normalized, Storage);
	if (Numeric && !Numeric->IsEnum() && ((Numeric->IsInteger() && Normalized != IntegerText)
		|| (Numeric->IsFloatingPoint() && !FMath::IsFinite(Numeric->GetFloatingPointPropertyValue(Storage)))))
	{ Error = TEXT("Numeric value is outside the property range"); return false; }
	return true;
}

static bool PrepareEdit(UObject* Asset, const FString& Capability, const TSharedPtr<FJsonObject>& Operation,
	FAssetEditProperty& Resolved, TSharedPtr<FJsonObject>& Entry, FString& Error)
{
	for (const TCHAR* Field : { TEXT("Action"), TEXT("PropertyPath"), TEXT("WidgetName"), TEXT("RowName"), TEXT("FieldName") })
	{
		FString Value;
		if (Operation->TryGetStringField(Field, Value)) Entry->SetStringField(Field, Value);
	}
	if (!ResolveEditProperty(Asset, Capability, Operation, Resolved, Error)) return false;
	Entry->SetStringField(TEXT("PropertyType"), Resolved.Property->GetCPPType());
	FString Current;
	FNexusPropertyUtils::ExportText(Resolved.Property, Current, Resolved.Value);
	Entry->SetStringField(TEXT("CurrentValue"), Current);
	if (Operation->HasField(TEXT("ExpectedValue")))
	{
		FDefaultConstructedPropertyElement Expected(Resolved.Property);
		FString ExpectedValue;
		if (!ParseEditValue(Resolved, FNexusArgs(Operation).Str(TEXT("ExpectedValue")), Expected.GetObjAddress(), ExpectedValue, Error)) return false;
		Entry->SetStringField(TEXT("ExpectedValue"), ExpectedValue);
		if (!Resolved.Property->Identical(Resolved.Value, Expected.GetObjAddress(), PPF_None))
		{ Error = TEXT("ExpectedValue mismatch: target changed after preview"); return false; }
	}
	if (!Operation->HasTypedField<EJson::String>(TEXT("Value"))) { Error = TEXT("Value is required and must be a string"); return false; }
	return true;
}

void FNexusAssetEditUtils::Preview(UObject* Asset, const FString& Capability, const TSharedPtr<FJsonObject>& Operation,
	TSharedPtr<FJsonObject>& Entry)
{
	FAssetEditProperty Resolved;
	FString Error, Normalized;
	if (PrepareEdit(Asset, Capability, Operation, Resolved, Entry, Error))
	{
		FDefaultConstructedPropertyElement Storage(Resolved.Property);
		if (ParseEditValue(Resolved, FNexusArgs(Operation).Str(TEXT("Value")), Storage.GetObjAddress(), Normalized, Error))
			Entry->SetStringField(TEXT("TargetValue"), Normalized);
	}
	Entry->SetBoolField(TEXT("bValid"), Error.IsEmpty());
	if (!Error.IsEmpty()) Entry->SetStringField(TEXT("Error"), Error);
}

void FNexusAssetEditUtils::Apply(UObject* Asset, const FString& Capability, const TSharedPtr<FJsonObject>& Operation,
	bool bVerify, TSharedPtr<FJsonObject>& Entry)
{
	FAssetEditProperty Resolved;
	FString Error, Expected;
	Entry->SetBoolField(TEXT("bWritten"), false);
	Entry->SetStringField(TEXT("Verification"), bVerify ? TEXT("Failed") : TEXT("NotRequested"));
	if (bVerify) Entry->SetBoolField(TEXT("bVerified"), false);
	if (!PrepareEdit(Asset, Capability, Operation, Resolved, Entry, Error))
	{ Entry->SetStringField(TEXT("Error"), Error); return; }
	Entry->SetStringField(TEXT("OldValue"), Entry->GetStringField(TEXT("CurrentValue")));
	const FString PropertyType = Resolved.Property->GetCPPType();
	{
		FDefaultConstructedPropertyElement Storage(Resolved.Property);
		if (!ParseEditValue(Resolved, FNexusArgs(Operation).Str(TEXT("Value")), Storage.GetObjAddress(), Expected, Error))
		{ Entry->SetStringField(TEXT("Error"), Error); return; }
		// 先记 Undo，再写入。临时属性存储必须在蓝图编译/重建类型前销毁。
		Asset->SetFlags(RF_Transactional); Asset->Modify();
		Resolved.Owner->SetFlags(RF_Transactional); Resolved.Owner->Modify();
		if (UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Asset)) WidgetBP->WidgetTree->Modify();
		Resolved.Property->CopyCompleteValue(Resolved.Value, Storage.GetObjAddress());
	}
	Entry->SetBoolField(TEXT("bWritten"), true);
	Entry->SetStringField(TEXT("NewValue"), Expected);
	Asset->MarkPackageDirty();
	UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
	if (Blueprint)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		if (bVerify || Capability == TEXT("ManageAssetBlueprint"))
		{
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			Entry->SetBoolField(TEXT("bCompiled"), Blueprint->Status != BS_Error);
			if (Blueprint->Status == BS_Error)
			{
				Entry->SetStringField(TEXT("CompileError"), TEXT("Blueprint compilation failed; package was not saved"));
				return;
			}
		}
	}
	if (bVerify)
	{
		FAssetEditProperty Actual;
		if (!ResolveEditProperty(Asset, Capability, Operation, Actual, Error))
		{ Entry->SetStringField(TEXT("Error"), TEXT("Readback failed: ") + Error); return; }
		FString Readback;
		FNexusPropertyUtils::ExportText(Actual.Property, Readback, Actual.Value);
		Entry->SetStringField(TEXT("ReadbackValue"), Readback);
		if (Actual.Property->GetCPPType() != PropertyType || Readback != Expected)
		{ Entry->SetStringField(TEXT("Error"), TEXT("Readback differs from normalized target value; package was not saved")); return; }
		Entry->SetBoolField(TEXT("bVerified"), true);
		Entry->SetStringField(TEXT("Verification"), TEXT("Passed"));
	}
}
