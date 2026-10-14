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

void FNexusManageAssetDataLayerCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("ManageAssetDataLayer");
	Out.SearchAssetTypes = {TEXT("DataLayerAsset")};
	Out.Description = TEXT("Edit DataLayer asset (≥UE5.1, editor): SetType/SetDebugColor.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("DataLayerAsset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrOfObj(TEXT("Operation list; each item requires action")))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("datalayer"), TEXT("data layer"), TEXT("world partition"), TEXT("streaming"), TEXT("Color"), TEXT("type") };
	Out.RelatedCapabilities = { TEXT("GetAssetDataLayer"), TEXT("CreateAssetDataLayer") };
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
	if (!DLA)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("DataLayerAsset is null"));
		return;
	}
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
	if (!DLA)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("DataLayerAsset is null"));
		return;
	}
	FString ColorStr;
	Op->TryGetStringField(TEXT("Color"), ColorStr);
	if (ColorStr.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetDebugColor requires color field (#RRGGBB)"));
		return;
	}
	const FColor Color = FColor::FromHex(ColorStr);
	DLA->SetDebugColor(Color);
	Ctx.Entry->SetStringField(TEXT("Color"), Color.ToHex());
	MarkDLDirty(Ctx);
}
#endif // WITH_EDITOR

bool FNexusManageAssetDataLayerCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
#if !WITH_EDITOR
	(void)Args;
	(void)Entry;
	(void)OutTarget;
	OutError = TEXT("ManageAssetDataLayer requires editor");
	return false;
#else
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetDataLayerCapability::FinalizeTarget(void* Target) const
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

void FNexusManageAssetDataLayerCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
#if WITH_EDITOR
	OutHandlers.Add(TEXT("SetType"),        &HandleDL_SetType);
	OutHandlers.Add(TEXT("SetDebugColor"), &HandleDL_SetDebugColor);
#else
	(void)OutHandlers;
#endif
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetDataLayerCapability)

#else // NX_UE_HAS_DATA_LAYER_ASSET

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"

void FNexusManageAssetDataLayerCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("ManageAssetDataLayer");
	Out.Description = TEXT("(DataLayerAsset requires UE5.1+ on this engine)");
	Out.InputSchema = FNexusSchema::Object().Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
}

FCapabilityResult FNexusManageAssetDataLayerCapability::Execute(const TSharedPtr<FJsonObject>&) const
{
	return FNexusCapabilityResultBuilder::Build([](auto& OutEntries, auto&, auto& OutError)
	{
		OutError = TEXT("ManageAssetDataLayer requires UE5.1+");
	});
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetDataLayerCapability)

#endif // NX_UE_HAS_DATA_LAYER_ASSET
