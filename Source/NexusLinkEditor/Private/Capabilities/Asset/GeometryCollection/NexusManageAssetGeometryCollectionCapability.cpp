// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GeometryCollection/NexusManageAssetGeometryCollectionCapability.h"
#if WITH_GEOMETRY_COLLECTION
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPropertyUtils.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "NexusMcpTool.h"

void FManageAssetGeometryCollectionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_geometry_collection");
	Out.SearchAssetTypes = {TEXT("GeometryCollection")};
	Out.Description = TEXT("Batch edit GeometryCollection. action=set_damage_threshold/set_property.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("set_damage_threshold"), TEXT("set_property") }))
		.Prop(TEXT("index"), FNexusSchema::Int(TEXT("Threshold index (set_damage_threshold)"), 0))
		.Prop(TEXT("value"), FNexusSchema::Str(TEXT("Threshold or property value")))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("Property path (set_property)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("GeometryCollection asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("chaos"), TEXT("damage"), TEXT("threshold") };
	Out.RelatedCapabilities = { TEXT("get_asset_geometry_collection"), TEXT("create_asset_geometry_collection") };
}

struct FGCActionState
{
	UGeometryCollection* GC = nullptr;
	bool bDirty = false;
};

static FGCActionState* GCState(FNexusActionContext& Ctx)
{
	return static_cast<FGCActionState*>(Ctx.Target);
}

static UGeometryCollection* GCFrom(FNexusActionContext& Ctx)
{
	FGCActionState* S = GCState(Ctx);
	return S ? S->GC : nullptr;
}

static void MarkGCDirty(FNexusActionContext& Ctx)
{
	if (FGCActionState* S = GCState(Ctx))
	{
		S->bDirty = true;
	}
}

static void HandleGC_SetDamageThreshold(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UGeometryCollection* GC = GCFrom(Ctx);
	if (!Op->HasField(TEXT("value")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_damage_threshold requires value"));
		return;
	}
	const int32 Idx = Op->HasField(TEXT("index")) ? static_cast<int32>(Op->GetNumberField(TEXT("index"))) : 0;
	const float Val = static_cast<float>(Op->GetNumberField(TEXT("value")));
	if (GC->DamageThreshold.Num() == 0) GC->DamageThreshold.Add(Val);
	else if (GC->DamageThreshold.IsValidIndex(Idx)) GC->DamageThreshold[Idx] = Val;
	else GC->DamageThreshold.Add(Val);
	MarkGCDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("index"), Idx);
	Ctx.Entry->SetNumberField(TEXT("value"), Val);
}

static void HandleGC_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UGeometryCollection* GC = GCFrom(Ctx);
	FString PropPath, Value;
	Op->TryGetStringField(TEXT("propertyPath"), PropPath);
	Op->TryGetStringField(TEXT("value"), Value);
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_property requires propertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(GC, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("error"), Err);
		return;
	}
	MarkGCDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("propertyPath"), PropPath);
}

bool FManageAssetGeometryCollectionCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UGeometryCollection* GC = FNexusAssetUtils::LoadAssetWithFallback<UGeometryCollection>(AssetPath);
	if (!GC)
	{
		OutError = FString::Printf(TEXT("Failed to load GeometryCollection: %s"), *AssetPath);
		return false;
	}
	FGCActionState* State = new FGCActionState();
	State->GC = GC;
	OutTarget = State;
	return true;
}

void FManageAssetGeometryCollectionCapability::FinalizeTarget(void* Target) const
{
	FGCActionState* State = static_cast<FGCActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->GC)
	{
		State->GC->MarkPackageDirty();
	}
	delete State;
}

void FManageAssetGeometryCollectionCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_damage_threshold"), &HandleGC_SetDamageThreshold);
	OutHandlers.Add(TEXT("set_property"),         &HandleGC_SetProperty);
}

REGISTER_MCP_CAPABILITY(FManageAssetGeometryCollectionCapability)
#endif
