// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Localization/NexusManageAssetStringTableCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "NexusMcpTool.h"

void FManageAssetStringTableCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_string_table");
	Out.SearchAssetTypes = {TEXT("StringTable")};
	Out.Description = TEXT("Batch edit StringTable. action=add_key/remove_key/set_source.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("add_key"), TEXT("remove_key"), TEXT("set_source") }))
		.Prop(TEXT("key"), FNexusSchema::Str(TEXT("Entry key")))
		.Prop(TEXT("source"), FNexusSchema::Str(TEXT("Source string (add_key/set_source)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("StringTable asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("localization"), TEXT("loc"), TEXT("key"), TEXT("source") };
	Out.RelatedCapabilities = { TEXT("get_asset_string_table"), TEXT("create_asset_string_table") };
}

struct FStringTableActionState
{
	UStringTable* Table = nullptr;
	bool bDirty = false;
};

static FStringTableActionState* STState(FNexusActionContext& Ctx)
{
	return static_cast<FStringTableActionState*>(Ctx.Target);
}

static UStringTable* STFrom(FNexusActionContext& Ctx)
{
	FStringTableActionState* S = STState(Ctx);
	return S ? S->Table : nullptr;
}

static void MarkSTDirty(FNexusActionContext& Ctx)
{
	if (FStringTableActionState* S = STState(Ctx))
	{
		S->bDirty = true;
	}
}

static FString RequireSTKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FString Key;
	Op->TryGetStringField(TEXT("key"), Key);
	if (Key.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("key required"));
	}
	return Key;
}

static void HandleST_AddOrSetSource(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStringTable* Table = STFrom(Ctx);
	const FString Key = RequireSTKey(Op, Ctx);
	if (Key.IsEmpty()) return;
	FString Source;
	Op->TryGetStringField(TEXT("source"), Source);
	if (Source.IsEmpty() && Ctx.Action == TEXT("add_key"))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_key requires source"));
		return;
	}
	const FStringTableRef Mutable = Table->GetMutableStringTable();
#if NX_UE_HAS_STRING_TABLE_SOURCE_DEV_NOTES
	Mutable->SetSourceString(Key, Source, FString());
#else
	Mutable->SetSourceString(Key, Source);
#endif
	MarkSTDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("key"), Key);
	Ctx.Entry->SetStringField(TEXT("source"), Source);
}

static void HandleST_RemoveKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStringTable* Table = STFrom(Ctx);
	const FString Key = RequireSTKey(Op, Ctx);
	if (Key.IsEmpty()) return;
	Table->GetMutableStringTable()->RemoveSourceString(Key);
	MarkSTDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("key"), Key);
}

bool FManageAssetStringTableCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UStringTable* Table = FNexusAssetUtils::LoadAssetWithFallback<UStringTable>(AssetPath);
	if (!Table)
	{
		OutError = FString::Printf(TEXT("Failed to load StringTable: %s"), *AssetPath);
		return false;
	}
	FStringTableActionState* State = new FStringTableActionState();
	State->Table = Table;
	OutTarget = State;
	return true;
}

void FManageAssetStringTableCapability::FinalizeTarget(void* Target) const
{
	FStringTableActionState* State = static_cast<FStringTableActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Table)
	{
		State->Table->MarkPackageDirty();
	}
	delete State;
}

void FManageAssetStringTableCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("add_key"),    &HandleST_AddOrSetSource);
	OutHandlers.Add(TEXT("set_source"), &HandleST_AddOrSetSource);
	OutHandlers.Add(TEXT("remove_key"), &HandleST_RemoveKey);
}

REGISTER_MCP_CAPABILITY(FManageAssetStringTableCapability)
