// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Enum/NexusManageAssetEnumCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Engine/UserDefinedEnum.h"
#include "UObject/UnrealType.h"
#include "NexusMcpTool.h"

// EnumEditorUtils 属于 UnrealEd（Editor-only），Game 目标不可用
#if WITH_EDITOR
#include "Kismet2/EnumEditorUtils.h"
#endif

void FNexusManageAssetEnumCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetEnum");
	Out.SearchAssetTypes = {TEXT("UserDefinedEnum")};
	Out.Description = TEXT("Edit UserDefinedEnum entries. action: AddEntry/RemoveEntry/SetDisplayName.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("Action"),      FNexusSchema::Str(TEXT("AddEntry / RemoveEntry / SetDisplayName")))
		.Prop(TEXT("Index"),           FNexusSchema::Int(TEXT("Enum entry index (required for RemoveEntry/SetDisplayName)")))
		.Prop(TEXT("DisplayName"),     FNexusSchema::Str(TEXT("Display name (required for SetDisplayName)")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"),   FNexusSchema::Str(TEXT("Enum asset package path")))
		.Required(TEXT("Operations"),  FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("enum"), TEXT("entry"), TEXT("Add"), TEXT("Remove"), TEXT("Rename"), TEXT("Display") };
	Out.RelatedCapabilities = { TEXT("CreateAssetEnum"), TEXT("GetAssetEnum") };
}

#if WITH_EDITOR
struct FEnumActionState
{
	UUserDefinedEnum* Enum = nullptr;
	bool bDirty = false;
};

static FEnumActionState* EnumState(FNexusActionContext& Ctx)
{
	return static_cast<FEnumActionState*>(Ctx.Target);
}

static UUserDefinedEnum* EnumFrom(FNexusActionContext& Ctx)
{
	FEnumActionState* S = EnumState(Ctx);
	return S ? S->Enum : nullptr;
}

static void MarkEnumDirty(FNexusActionContext& Ctx)
{
	if (FEnumActionState* S = EnumState(Ctx))
	{
		S->bDirty = true;
	}
}

static void HandleEnum_AddEntry(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	(void)Op;
	UUserDefinedEnum* Enum = EnumFrom(Ctx);
	if (!Enum)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("UserDefinedEnum is null"));
		return;
	}
	FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);
	Ctx.Entry->SetNumberField(TEXT("NewIndex"), Enum->NumEnums() - 2); // 最新项（-2：跳 _MAX）
	MarkEnumDirty(Ctx);
}

static void HandleEnum_RemoveEntry(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UUserDefinedEnum* Enum = EnumFrom(Ctx);
	if (!Enum)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("UserDefinedEnum is null"));
		return;
	}
	if (!Op->HasField(TEXT("Index")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveEntry requires index"));
		return;
	}
	const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("Index")));
	const int32 ValidCount = Enum->NumEnums() - 1;
	if (Idx < 0 || Idx >= ValidCount)
	{
		Ctx.Entry->SetStringField(TEXT("Error"),
			FString::Printf(TEXT("index %d out of range [0,%d)"), Idx, ValidCount));
		return;
	}
	FEnumEditorUtils::RemoveEnumeratorFromUserDefinedEnum(Enum, Idx);
	MarkEnumDirty(Ctx);
}

static void HandleEnum_SetDisplayName(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UUserDefinedEnum* Enum = EnumFrom(Ctx);
	if (!Enum)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("UserDefinedEnum is null"));
		return;
	}
	if (!Op->HasField(TEXT("Index")) || !Op->HasField(TEXT("DisplayName")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetDisplayName requires index and DisplayName"));
		return;
	}
	const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("Index")));
	const FString DispName = Op->GetStringField(TEXT("DisplayName"));
	const int32 ValidCount = Enum->NumEnums() - 1;
	if (Idx < 0 || Idx >= ValidCount)
	{
		Ctx.Entry->SetStringField(TEXT("Error"),
			FString::Printf(TEXT("index %d out of range [0,%d)"), Idx, ValidCount));
		return;
	}
	// 通过反射写入 DisplayNameMap（跨版本稳定）
	bool bSetOk = false;
	const FName EntryKey = FName(*Enum->GetNameStringByIndex(Idx));
	if (FMapProperty* MapProp = FindFProperty<FMapProperty>(Enum->GetClass(), TEXT("DisplayNameMap")))
	{
		using FDispMap = TMap<FName, FText>;
		FDispMap* Map = MapProp->ContainerPtrToValuePtr<FDispMap>(Enum);
		if (Map)
		{
			Map->Add(EntryKey, FText::FromString(DispName));
			bSetOk = true;
		}
	}
	if (!bSetOk)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetDisplayName: failed to write DisplayNameMap"));
		return;
	}
	MarkEnumDirty(Ctx);
}
#endif // WITH_EDITOR

bool FNexusManageAssetEnumCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
#if !WITH_EDITOR
	(void)Args;
	(void)Entry;
	(void)OutTarget;
	OutError = TEXT("ManageAssetEnum only available in Editor builds");
	return false;
#else
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UUserDefinedEnum* Enum = LoadObject<UUserDefinedEnum>(nullptr, *AssetPath);
	if (!Enum)
	{
		OutError = FString::Printf(TEXT("Failed to load UserDefinedEnum: %s"), *AssetPath);
		return false;
	}
	FEnumActionState* State = new FEnumActionState();
	State->Enum = Enum;
	OutTarget = State;
	return true;
#endif
}

void FNexusManageAssetEnumCapability::FinalizeTarget(void* Target) const
{
#if WITH_EDITOR
	FEnumActionState* State = static_cast<FEnumActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Enum)
	{
		State->Enum->MarkPackageDirty();
	}
	delete State;
#else
	(void)Target;
#endif
}

void FNexusManageAssetEnumCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
#if WITH_EDITOR
	OutHandlers.Add(TEXT("AddEntry"),        &HandleEnum_AddEntry);
	OutHandlers.Add(TEXT("RemoveEntry"),     &HandleEnum_RemoveEntry);
	OutHandlers.Add(TEXT("SetDisplayName"), &HandleEnum_SetDisplayName);
#else
	(void)OutHandlers;
#endif
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetEnumCapability)
