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

void FNexusManageAssetStringTableCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetStringTable");
	Out.SearchAssetTypes = {TEXT("StringTable")};
	Out.Description = TEXT("Batch edit StringTable. Action=AddKey/RemoveKey/SetSource.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("AddKey"), TEXT("RemoveKey"), TEXT("SetSource") }))
		.Prop(TEXT("Key"), FNexusSchema::Str(TEXT("Entry key")))
		.Prop(TEXT("Source"), FNexusSchema::Str(TEXT("Source string (AddKey/SetSource)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("StringTable asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("localization"), TEXT("loc"), TEXT("Key"), TEXT("Source") };
	Out.RelatedCapabilities = { TEXT("GetAssetStringTable"), TEXT("CreateAssetStringTable") };
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
	Op->TryGetStringField(TEXT("Key"), Key);
	if (Key.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("key required"));
	}
	return Key;
}

static void HandleST_AddOrSetSource(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStringTable* Table = STFrom(Ctx);
	if (!Table)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("StringTable is null"));
		return;
	}
	const FString Key = RequireSTKey(Op, Ctx);
	if (Key.IsEmpty()) return;
	FString Source;
	Op->TryGetStringField(TEXT("Source"), Source);
	if (Source.IsEmpty() && Ctx.Action == TEXT("AddKey"))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddKey requires source"));
		return;
	}
	const FStringTableRef Mutable = Table->GetMutableStringTable();
#if NX_UE_HAS_STRING_TABLE_SOURCE_DEV_NOTES
	Mutable->SetSourceString(Key, Source, FString());
#else
	Mutable->SetSourceString(Key, Source);
#endif
	MarkSTDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("Key"), Key);
	Ctx.Entry->SetStringField(TEXT("Source"), Source);
}

static void HandleST_RemoveKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UStringTable* Table = STFrom(Ctx);
	if (!Table)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("StringTable is null"));
		return;
	}
	const FString Key = RequireSTKey(Op, Ctx);
	if (Key.IsEmpty()) return;
	Table->GetMutableStringTable()->RemoveSourceString(Key);
	MarkSTDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("Key"), Key);
}

bool FNexusManageAssetStringTableCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetStringTableCapability::FinalizeTarget(void* Target) const
{
	FStringTableActionState* State = static_cast<FStringTableActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Table)
	{
		State->Table->MarkPackageDirty();
	}
	delete State;
}

void FNexusManageAssetStringTableCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("AddKey"),    &HandleST_AddOrSetSource);
	OutHandlers.Add(TEXT("SetSource"), &HandleST_AddOrSetSource);
	OutHandlers.Add(TEXT("RemoveKey"), &HandleST_RemoveKey);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetStringTableCapability)
