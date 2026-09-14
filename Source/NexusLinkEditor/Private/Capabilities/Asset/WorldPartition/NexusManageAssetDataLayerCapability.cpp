// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/WorldPartition/NexusManageAssetDataLayerCapability.h"
#include "Utils/NexusVersionCompat.h"

#if NX_UE_HAS_DATA_LAYER_ASSET

#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"

void FManageAssetDataLayerCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("manage_asset_data_layer");
	Out.SearchAssetTypes = {TEXT("DataLayerAsset")};
	Out.Description = TEXT("Edit DataLayer asset (≥UE5.1, editor): set_type/set_debug_color.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("DataLayerAsset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrOfObj(TEXT("Operation list; each item requires action")))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("datalayer"), TEXT("data layer"), TEXT("world partition"), TEXT("streaming"), TEXT("color"), TEXT("type") };
	Out.RelatedCapabilities = { TEXT("get_asset_data_layer"), TEXT("create_asset_data_layer") };
	Out.WhenToUse = TEXT("Edit DataLayerAsset type (Runtime/Editor) or debug color (≥UE5.1)");
}

#if WITH_EDITOR
struct FDataLayerActionState
{
	UDataLayerAsset* DLA = nullptr;
	bool bDirty = false;
};

static FDataLayerActionState* DLState(FNexusActionContext& Ctx)
{
	return static_cast<FDataLayerActionState*>(Ctx.Target);
}

static UDataLayerAsset* DLFrom(FNexusActionContext& Ctx)
{
	FDataLayerActionState* S = DLState(Ctx);
	return S ? S->DLA : nullptr;
}

static void MarkDLDirty(FNexusActionContext& Ctx)
{
	if (FDataLayerActionState* S = DLState(Ctx))
	{
		S->bDirty = true;
	}
}

static void HandleDL_SetType(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UDataLayerAsset* DLA = DLFrom(Ctx);
	FString TypeStr;
	Op->TryGetStringField(TEXT("type"), TypeStr);
	EDataLayerType LayerType = EDataLayerType::Runtime;
	if (TypeStr.Equals(TEXT("Editor"), ESearchCase::IgnoreCase))
		LayerType = EDataLayerType::Editor;
	DLA->SetType(LayerType);
	Ctx.Entry->SetStringField(TEXT("type"), TypeStr.IsEmpty() ? TEXT("Runtime") : TypeStr);
	MarkDLDirty(Ctx);
}

static void HandleDL_SetDebugColor(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UDataLayerAsset* DLA = DLFrom(Ctx);
	FString ColorStr;
	Op->TryGetStringField(TEXT("color"), ColorStr);
	if (ColorStr.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_debug_color requires color field (#RRGGBB)"));
		return;
	}
	const FColor Color = FColor::FromHex(ColorStr);
	DLA->SetDebugColor(Color);
	Ctx.Entry->SetStringField(TEXT("color"), Color.ToHex());
	MarkDLDirty(Ctx);
}
#endif // WITH_EDITOR

bool FManageAssetDataLayerCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
#if !WITH_EDITOR
	(void)Args;
	(void)Entry;
	(void)OutTarget;
	OutError = TEXT("manage_asset_data_layer requires editor");
	return false;
#else
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UDataLayerAsset* DLA = FNexusAssetUtils::LoadAssetWithFallback<UDataLayerAsset>(AssetPath);
	if (!DLA)
	{
		OutError = FString::Printf(TEXT("DataLayerAsset not found: %s"), *AssetPath);
		return false;
	}
	FDataLayerActionState* State = new FDataLayerActionState();
	State->DLA = DLA;
	OutTarget = State;
	return true;
#endif
}

void FManageAssetDataLayerCapability::FinalizeTarget(void* Target) const
{
#if WITH_EDITOR
	FDataLayerActionState* State = static_cast<FDataLayerActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->DLA)
	{
		State->DLA->MarkPackageDirty();
	}
	delete State;
#else
	(void)Target;
#endif
}

void FManageAssetDataLayerCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
#if WITH_EDITOR
	OutHandlers.Add(TEXT("set_type"),        &HandleDL_SetType);
	OutHandlers.Add(TEXT("set_debug_color"), &HandleDL_SetDebugColor);
#else
	(void)OutHandlers;
#endif
}

REGISTER_MCP_CAPABILITY(FManageAssetDataLayerCapability)

#else // NX_UE_HAS_DATA_LAYER_ASSET

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"

void FManageAssetDataLayerCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("manage_asset_data_layer");
	Out.Description = TEXT("(DataLayerAsset requires UE5.1+ on this engine)");
	Out.InputSchema = FNexusSchema::Object().Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
}

FCapabilityResult FManageAssetDataLayerCapability::Execute(const TSharedPtr<FJsonObject>&) const
{
	return FNexusCapabilityResultBuilder::Build([](auto& OutEntries, auto&, auto& OutError)
	{
		OutError = TEXT("manage_asset_data_layer requires UE5.1+");
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetDataLayerCapability)

#endif // NX_UE_HAS_DATA_LAYER_ASSET
