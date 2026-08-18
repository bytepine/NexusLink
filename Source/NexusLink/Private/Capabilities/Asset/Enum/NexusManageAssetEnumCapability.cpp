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

void FManageAssetEnumCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_enum");
	Out.SearchAssetTypes = {TEXT("UserDefinedEnum")};
	Out.Description = TEXT("Edit UserDefinedEnum entries. action: add_entry/remove_entry/set_display_name.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"),      FNexusSchema::Str(TEXT("add_entry / remove_entry / set_display_name")))
		.Prop(TEXT("index"),           FNexusSchema::Int(TEXT("Enum entry index (required for remove_entry/set_display_name)")))
		.Prop(TEXT("displayName"),     FNexusSchema::Str(TEXT("Display name (required for set_display_name)")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),   FNexusSchema::Str(TEXT("Enum asset package path")))
		.Required(TEXT("operations"),  FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("enum"), TEXT("entry"), TEXT("add"), TEXT("remove"), TEXT("rename"), TEXT("display") };
	Out.RelatedCapabilities = { TEXT("create_asset_enum"), TEXT("get_asset_enum") };
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
	FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);
	Ctx.Entry->SetNumberField(TEXT("newIndex"), Enum->NumEnums() - 2); // 最新项（-2：跳 _MAX）
	MarkEnumDirty(Ctx);
}

static void HandleEnum_RemoveEntry(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UUserDefinedEnum* Enum = EnumFrom(Ctx);
	if (!Op->HasField(TEXT("index")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_entry requires index"));
		return;
	}
	const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("index")));
	const int32 ValidCount = Enum->NumEnums() - 1;
	if (Idx < 0 || Idx >= ValidCount)
	{
		Ctx.Entry->SetStringField(TEXT("error"),
			FString::Printf(TEXT("index %d out of range [0,%d)"), Idx, ValidCount));
		return;
	}
	FEnumEditorUtils::RemoveEnumeratorFromUserDefinedEnum(Enum, Idx);
	MarkEnumDirty(Ctx);
}

static void HandleEnum_SetDisplayName(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UUserDefinedEnum* Enum = EnumFrom(Ctx);
	if (!Op->HasField(TEXT("index")) || !Op->HasField(TEXT("displayName")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_display_name requires index and displayName"));
		return;
	}
	const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("index")));
	const FString DispName = Op->GetStringField(TEXT("displayName"));
	const int32 ValidCount = Enum->NumEnums() - 1;
	if (Idx < 0 || Idx >= ValidCount)
	{
		Ctx.Entry->SetStringField(TEXT("error"),
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
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_display_name: failed to write DisplayNameMap"));
		return;
	}
	MarkEnumDirty(Ctx);
}
#endif // WITH_EDITOR

bool FManageAssetEnumCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
#if !WITH_EDITOR
	(void)Args;
	(void)Entry;
	(void)OutTarget;
	OutError = TEXT("manage_asset_enum only available in Editor builds");
	return false;
#else
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
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

void FManageAssetEnumCapability::FinalizeTarget(void* Target) const
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

void FManageAssetEnumCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
#if WITH_EDITOR
	OutHandlers.Add(TEXT("add_entry"),        &HandleEnum_AddEntry);
	OutHandlers.Add(TEXT("remove_entry"),     &HandleEnum_RemoveEntry);
	OutHandlers.Add(TEXT("set_display_name"), &HandleEnum_SetDisplayName);
#else
	(void)OutHandlers;
#endif
}

REGISTER_MCP_CAPABILITY(FManageAssetEnumCapability)
