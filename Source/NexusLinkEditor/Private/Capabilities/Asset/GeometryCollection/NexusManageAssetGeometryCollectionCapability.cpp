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

void FNexusManageAssetGeometryCollectionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetGeometryCollection");
	Out.SearchAssetTypes = {TEXT("GeometryCollection")};
	Out.Description = TEXT("Batch edit GeometryCollection. Action=SetDamageThreshold/SetProperty.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("SetDamageThreshold"), TEXT("SetProperty") }))
		.Prop(TEXT("Index"), FNexusSchema::Int(TEXT("Threshold index (SetDamageThreshold)"), 0))
		.Prop(TEXT("Value"), FNexusSchema::Str(TEXT("Threshold or property value")))
		.Prop(TEXT("PropertyPath"), FNexusSchema::Str(TEXT("Property path (SetProperty)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("GeometryCollection asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("chaos"), TEXT("damage"), TEXT("threshold") };
	Out.RelatedCapabilities = { TEXT("GetAssetGeometryCollection"), TEXT("CreateAssetGeometryCollection") };
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
	if (!GC)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("GeometryCollection is null"));
		return;
	}
	if (!Op->HasField(TEXT("Value")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetDamageThreshold requires value"));
		return;
	}
	const int32 Idx = Op->HasField(TEXT("Index")) ? static_cast<int32>(Op->GetNumberField(TEXT("Index"))) : 0;
	const float Val = static_cast<float>(Op->GetNumberField(TEXT("Value")));
	if (GC->DamageThreshold.Num() == 0) GC->DamageThreshold.Add(Val);
	else if (GC->DamageThreshold.IsValidIndex(Idx)) GC->DamageThreshold[Idx] = Val;
	else GC->DamageThreshold.Add(Val);
	MarkGCDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("Index"), Idx);
	Ctx.Entry->SetNumberField(TEXT("Value"), Val);
}

static void HandleGC_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UGeometryCollection* GC = GCFrom(Ctx);
	FString PropPath, Value;
	Op->TryGetStringField(TEXT("PropertyPath"), PropPath);
	Op->TryGetStringField(TEXT("Value"), Value);
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetProperty requires PropertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(GC, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), Err);
		return;
	}
	MarkGCDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("PropertyPath"), PropPath);
}

bool FNexusManageAssetGeometryCollectionCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetGeometryCollectionCapability::FinalizeTarget(void* Target) const
{
	FGCActionState* State = static_cast<FGCActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->GC)
	{
		State->GC->MarkPackageDirty();
	}
	delete State;
}

void FNexusManageAssetGeometryCollectionCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetDamageThreshold"), &HandleGC_SetDamageThreshold);
	OutHandlers.Add(TEXT("SetProperty"),         &HandleGC_SetProperty);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetGeometryCollectionCapability)
#endif
