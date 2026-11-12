// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusAssetEditUtils.h"
#include "Utils/NexusEditorLevelUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusEditorTransaction.h"
#include "Utils/NexusBlueprintEditUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Engine/DataAsset.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "JsonObjectConverter.h"

static FString SupportedEditAction(const FString& Capability)
{
	if (Capability == TEXT("ManageAssetBlueprint")) return TEXT("SetDefaults");
	if (Capability == TEXT("ManageAssetDataTable")) return TEXT("Set");
	if (Capability == TEXT("ManageAssetUserWidget")) return TEXT("SetProperty");
	if (Capability == TEXT("ManageAssetDataAsset")) return TEXT("Set");
	if (Capability == TEXT("ManageAssetLevel")) return TEXT("SetActorProperty");
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
		const FString Action = FNexusArgs(Operation).Str(TEXT("Action"));
		const bool bScalarSet = Action.Equals(SupportedEditAction(Capability), ESearchCase::IgnoreCase)
			|| (Capability == TEXT("ManageAssetBlueprint") && (Action == TEXT("SetComponentProperty") || Action == TEXT("SetArray") || Action == TEXT("ClearArray")));
		// Set 支持完整反射值；同批 Reset 自行比较默认值，不接受 ExpectedValue。
		const bool bReset = (Capability == TEXT("ManageAssetLevel") && Action == TEXT("ResetActorPropertyToDefault"))
			|| (Capability == TEXT("ManageAssetBlueprint") && Action == TEXT("ResetComponentPropertyToDefault"));
		if (!bScalarSet && (Operation->HasField(TEXT("ExpectedValue")) || (IsVerified(Args) && !bReset)))
		{
			Error = TEXT("Verification=Unsupported: ExpectedValue/bVerify supports reflected property sets, array edits and resets only");
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
	const bool bArray = Capability == TEXT("ManageAssetBlueprint") && (Args.Str(TEXT("Action")) == TEXT("SetArray") || Args.Str(TEXT("Action")) == TEXT("ClearArray"));
	const bool bComponent = Capability == TEXT("ManageAssetBlueprint") && (Args.Str(TEXT("Action")) == TEXT("SetComponentProperty") || Args.Str(TEXT("Action")) == TEXT("ResetComponentPropertyToDefault") || (bArray && !Args.Str(TEXT("ComponentName")).IsEmpty()));
	if (!Asset || Action.IsEmpty() || (!bComponent && !bArray && !Args.Str(TEXT("Action")).Equals(Action, ESearchCase::IgnoreCase)))
	{ Error = TEXT("Unsupported asset or operation for property preview/verification"); return false; }
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
	else if (Capability == TEXT("ManageAssetDataAsset"))
	{
		Resolved.Owner = Cast<UDataAsset>(Asset);
		Path = Args.Str(TEXT("PropertyName"));
	}
	else if (Capability == TEXT("ManageAssetLevel"))
	{
		UWorld* World = Cast<UWorld>(Asset);
		Resolved.Owner = FNexusEditorLevelUtils::FindLevelActorByNameOrLabel(World, Args.Str(TEXT("ActorName")), false);
	}
	else if (bComponent)
	{
		Resolved.Owner = FNexusBlueprintEditUtils::FindComponent(Cast<UBlueprint>(Asset), Args.Str(TEXT("ComponentName")), false, Error);
	}
	else
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
		Resolved.Owner = Blueprint && Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
	}
	if (!Resolved.Owner) { Error = TEXT("Target/CDO/Widget is unavailable; prepare the asset before preview"); return false; }
	if (Capability != TEXT("ManageAssetDataTable"))
	{ Struct = Resolved.Owner->GetClass(); Container = Resolved.Owner; }
	UObject* RootOwner = Resolved.Owner;
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
			if (FStructProperty* Nested = CastField<FStructProperty>(Resolved.Property))
			{ Struct = Nested->Struct; Container = Resolved.Value; }
			else if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Resolved.Property))
			{
				UObject* Subobject = ObjectProperty->GetObjectPropertyValue(Resolved.Value);
				// 只允许目标 CDO/Widget 自己持有的子对象，禁止沿资产引用修改外部对象。
				if (!Subobject || !Subobject->IsIn(RootOwner))
				{ Error = TEXT("Object traversal requires an owned subobject; null/external references are rejected"); return false; }
				Resolved.Owner = Subobject;
				Struct = Subobject->GetClass(); Container = Subobject;
			}
			else { Error = TEXT("Only struct fields and owned subobjects support traversal; containers are rejected"); return false; }
		}
	}
	FProperty* Property = Resolved.Property;
	if (!CastField<FBoolProperty>(Property) && !CastField<FNumericProperty>(Property) && !CastField<FEnumProperty>(Property)
		&& !CastField<FStrProperty>(Property) && !CastField<FNameProperty>(Property) && !CastField<FTextProperty>(Property)
		&& !CastField<FObjectProperty>(Property) && !CastField<FStructProperty>(Property)
		&& !CastField<FArrayProperty>(Property) && !CastField<FSetProperty>(Property) && !CastField<FMapProperty>(Property))
	{ Error = TEXT("Verification=Unsupported: this property type cannot be compared"); return false; }
	if (Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_ContainsInstancedReference))
	{ Error = TEXT("Verification=Unsupported: instanced reference values require explicit subobject leaf paths"); return false; }
	if (bArray && !CastField<FArrayProperty>(Property))
	{ Error = TEXT("SetArray/ClearArray requires an array property"); return false; }
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
	}
	if (!FNexusPropertyUtils::ParsePropertyValue(Property, Input, Storage, Resolved.Owner, Error)) return false;
	FNexusPropertyUtils::ExportText(Property, Normalized, Storage);
	if (Numeric && !Numeric->IsEnum() && ((Numeric->IsInteger() && Normalized != IntegerText)
		|| (Numeric->IsFloatingPoint() && !FMath::IsFinite(Numeric->GetFloatingPointPropertyValue(Storage)))))
	{ Error = TEXT("Numeric value is outside the property range"); return false; }
	return true;
}

static bool ParseOperationValue(const FAssetEditProperty& Resolved, const TSharedPtr<FJsonObject>& Operation,
	void* Storage, FString& Normalized, FString& Error)
{
	Resolved.Property->CopyCompleteValue(Storage, Resolved.Value);
	const FString Action = FNexusArgs(Operation).Str(TEXT("Action"));
	if (Action == TEXT("ClearArray") || Action == TEXT("SetArray"))
	{
		FArrayProperty* Array = CastField<FArrayProperty>(Resolved.Property);
		if (!Array) { Error = TEXT("Array operation requires an array property"); return false; }
		FScriptArrayHelper Helper(Array, Storage);
		Helper.EmptyValues();
		if (Action == TEXT("SetArray"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Operation->TryGetArrayField(TEXT("Values"), Values) || Values->Num() > 4096)
			{ Error = TEXT("SetArray requires typed JSON Values (max 4096 elements); [] clears the array"); return false; }
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				const int32 ElementIndex = Helper.AddValue();
				if (!FJsonObjectConverter::JsonValueToUProperty(Value, Array->Inner, Helper.GetRawPtr(ElementIndex), 0, 0))
				{ Error = FString::Printf(TEXT("Values[%d] does not match the array element type"), ElementIndex); return false; }
			}
		}
		FNexusPropertyUtils::ExportText(Resolved.Property, Normalized, Storage);
		return true;
	}
	return ParseEditValue(Resolved, FNexusArgs(Operation).Str(TEXT("Value")), Storage, Normalized, Error);
}

static bool PrepareEdit(UObject* Asset, const FString& Capability, const TSharedPtr<FJsonObject>& Operation,
	FAssetEditProperty& Resolved, TSharedPtr<FJsonObject>& Entry, FString& Error)
{
	for (const TCHAR* Field : { TEXT("Action"), TEXT("PropertyPath"), TEXT("PropertyName"), TEXT("ActorName"), TEXT("ComponentName"), TEXT("WidgetName"), TEXT("RowName"), TEXT("FieldName") })
	{
		FString Value;
		if (Operation->TryGetStringField(Field, Value)) Entry->SetStringField(Field, Value);
	}
	if (!ResolveEditProperty(Asset, Capability, Operation, Resolved, Error)) return false;
	if (Capability == TEXT("ManageAssetLevel"))
	{
		AActor* Actor = FNexusEditorLevelUtils::FindLevelActorByNameOrLabel(Cast<UWorld>(Asset), FNexusArgs(Operation).Str(TEXT("ActorName")), false);
		if (!Actor) { Error = TEXT("Actor is unavailable in the requested level"); return false; }
		Entry->SetStringField(TEXT("ActorPath"), Actor->GetPathName());
		Entry->SetStringField(TEXT("LevelPath"), Actor->GetOutermost()->GetName());
	}
	Entry->SetStringField(TEXT("PropertyType"), Resolved.Property->GetCPPType());
	Entry->SetStringField(TEXT("VerificationSupport"), TEXT("ReflectedValue"));
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
	const FString Action = FNexusArgs(Operation).Str(TEXT("Action"));
	if (Action != TEXT("SetArray") && Action != TEXT("ClearArray") && !Operation->HasTypedField<EJson::String>(TEXT("Value")))
	{ Error = TEXT("Value is required and must be a string"); return false; }
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
		if (ParseOperationValue(Resolved, Operation, Storage.GetObjAddress(), Normalized, Error))
			Entry->SetStringField(TEXT("TargetValue"), Normalized);
	}
	Entry->SetBoolField(TEXT("bValid"), Error.IsEmpty());
	if (!Error.IsEmpty()) Entry->SetStringField(TEXT("Error"), Error);
	if (Error.StartsWith(TEXT("Verification=Unsupported")))
	{
		Entry->SetStringField(TEXT("Verification"), TEXT("Unsupported"));
		Entry->SetStringField(TEXT("VerificationSupport"), TEXT("Unsupported"));
	}
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
	{ Entry->SetStringField(TEXT("Error"), Error); if (Error.StartsWith(TEXT("Verification=Unsupported"))) { Entry->SetStringField(TEXT("Verification"), TEXT("Unsupported")); Entry->SetStringField(TEXT("VerificationSupport"), TEXT("Unsupported")); } return; }
	Entry->SetStringField(TEXT("OldValue"), Entry->GetStringField(TEXT("CurrentValue")));
	const FString PropertyType = Resolved.Property->GetCPPType();
	{
		FDefaultConstructedPropertyElement Storage(Resolved.Property);
		if (!ParseOperationValue(Resolved, Operation, Storage.GetObjAddress(), Expected, Error))
		{ Entry->SetStringField(TEXT("Error"), Error); return; }
		if (Capability == TEXT("ManageAssetBlueprint") && !FNexusArgs(Operation).Str(TEXT("ComponentName")).IsEmpty())
		{
			if (!FNexusBlueprintEditUtils::FindComponent(Cast<UBlueprint>(Asset), FNexusArgs(Operation).Str(TEXT("ComponentName")), true, Error)
				|| !ResolveEditProperty(Asset, Capability, Operation, Resolved, Error))
			{ Entry->SetStringField(TEXT("Error"), Error); return; }
		}
		// 先记 Undo，再写入。临时属性存储必须在蓝图编译/重建类型前销毁。
		Asset->SetFlags(RF_Transactional); Asset->Modify();
		Resolved.Owner->SetFlags(RF_Transactional); Resolved.Owner->Modify();
		if (UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Asset)) WidgetBP->WidgetTree->Modify();
		Resolved.Property->CopyCompleteValue(Resolved.Value, Storage.GetObjAddress());
	}
	Entry->SetBoolField(TEXT("bWritten"), true);
	Entry->SetStringField(TEXT("NewValue"), Expected);
	Asset->MarkPackageDirty();
	if (Capability == TEXT("ManageAssetLevel"))
		FNexusEditorLevelUtils::RefreshActorAfterPropertyEdit(FNexusEditorLevelUtils::FindLevelActorByNameOrLabel(Cast<UWorld>(Asset), FNexusArgs(Operation).Str(TEXT("ActorName")), false));
	UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
	if (Blueprint)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		// BP 在整批操作结束后统一编译与回读；这里不能因 ExpectedValue/bVerify 隐式编译。
		if (bVerify && Capability != TEXT("ManageAssetBlueprint"))
		{
			const EBlueprintCompileOptions Options = FNexusEditorTransaction::IsTransactionActive()
				? EBlueprintCompileOptions::SkipGarbageCollection : EBlueprintCompileOptions::None;
			FKismetEditorUtilities::CompileBlueprint(Blueprint, Options);
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

void FNexusAssetEditUtils::Verify(UObject* Asset, const FString& Capability,
	const TSharedPtr<FJsonObject>& Operation, TSharedPtr<FJsonObject>& Entry)
{
	FAssetEditProperty Actual;
	FString Error, Readback;
	Entry->SetBoolField(TEXT("bVerified"), false);
	Entry->SetStringField(TEXT("Verification"), TEXT("Failed"));
	if (!ResolveEditProperty(Asset, Capability, Operation, Actual, Error))
	{ Entry->SetStringField(TEXT("Error"), TEXT("Readback failed: ") + Error); return; }
	FNexusPropertyUtils::ExportText(Actual.Property, Readback, Actual.Value);
	Entry->SetStringField(TEXT("ReadbackValue"), Readback);
	if (Actual.Property->GetCPPType() != Entry->GetStringField(TEXT("PropertyType"))
		|| Readback != Entry->GetStringField(TEXT("NewValue")))
	{ Entry->SetStringField(TEXT("Error"), TEXT("Readback differs from normalized target value; package was not saved")); return; }
	Entry->SetBoolField(TEXT("bVerified"), true);
	Entry->SetStringField(TEXT("Verification"), TEXT("Passed"));
}
