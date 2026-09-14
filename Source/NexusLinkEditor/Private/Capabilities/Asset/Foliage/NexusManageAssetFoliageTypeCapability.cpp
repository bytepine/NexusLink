// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Foliage/NexusManageAssetFoliageTypeCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPropertyUtils.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "Engine/StaticMesh.h"
#include "NexusMcpTool.h"

void FManageAssetFoliageTypeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_foliage_type");
	Out.SearchAssetTypes = {TEXT("FoliageType")};
	Out.Description = TEXT("Batch edit FoliageType. action=set_mesh/set_density/set_property.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("set_mesh"), TEXT("set_density"), TEXT("set_property") }))
		.Prop(TEXT("meshPath"), FNexusSchema::Str(TEXT("StaticMesh path (set_mesh)")))
		.Prop(TEXT("density"), FNexusSchema::Num(TEXT("Density (set_density)")))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("Property path (set_property)")))
		.Prop(TEXT("value"), FNexusSchema::Str(TEXT("New property value (set_property)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("FoliageType asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("foliage"), TEXT("mesh"), TEXT("density"), TEXT("vegetation") };
	Out.RelatedCapabilities = { TEXT("get_asset_foliage_type"), TEXT("create_asset_foliage_type") };
}

struct FFoliageActionState
{
	UFoliageType* Type = nullptr;
	bool bDirty = false;
};

static FFoliageActionState* FolState(FNexusActionContext& Ctx)
{
	return static_cast<FFoliageActionState*>(Ctx.Target);
}

static UFoliageType* FolFrom(FNexusActionContext& Ctx)
{
	FFoliageActionState* S = FolState(Ctx);
	return S ? S->Type : nullptr;
}

static void MarkFolDirty(FNexusActionContext& Ctx)
{
	if (FFoliageActionState* S = FolState(Ctx))
	{
		S->bDirty = true;
	}
}

static void HandleFol_SetMesh(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UFoliageType_InstancedStaticMesh* ISM = Cast<UFoliageType_InstancedStaticMesh>(FolFrom(Ctx));
	if (!ISM)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Only InstancedStaticMesh supports set_mesh"));
		return;
	}
	const FString MeshPath = FNexusArgs(Op).Str(TEXT("meshPath"));
	if (MeshPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_mesh requires meshPath"));
		return;
	}
	UStaticMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<UStaticMesh>(MeshPath);
	if (!Mesh)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("StaticMesh not found: %s"), *MeshPath));
		return;
	}
	ISM->Mesh = Mesh;
	MarkFolDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("meshPath"), Mesh->GetPathName());
}

static void HandleFol_SetDensity(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UFoliageType* Type = FolFrom(Ctx);
	if (!Op->HasField(TEXT("density")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_density requires density"));
		return;
	}
	Type->Density = static_cast<float>(Op->GetNumberField(TEXT("density")));
	MarkFolDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("density"), Type->Density);
}

static void HandleFol_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UFoliageType* Type = FolFrom(Ctx);
	const FNexusArgs A(Op);
	const FString PropPath = A.Str(TEXT("propertyPath"));
	const FString Value = A.Str(TEXT("value"));
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_property requires propertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Type, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("error"), Err);
		return;
	}
	MarkFolDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("propertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("oldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("newValue"), ActualVal);
}

bool FManageAssetFoliageTypeCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UFoliageType* Type = FNexusAssetUtils::LoadAssetWithFallback<UFoliageType>(AssetPath);
	if (!Type)
	{
		OutError = FString::Printf(TEXT("Failed to load FoliageType: %s"), *AssetPath);
		return false;
	}
	FFoliageActionState* State = new FFoliageActionState();
	State->Type = Type;
	OutTarget = State;
	return true;
}

void FManageAssetFoliageTypeCapability::FinalizeTarget(void* Target) const
{
	FFoliageActionState* State = static_cast<FFoliageActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Type)
	{
		State->Type->MarkPackageDirty();
	}
	delete State;
}

void FManageAssetFoliageTypeCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_mesh"),     &HandleFol_SetMesh);
	OutHandlers.Add(TEXT("set_density"),  &HandleFol_SetDensity);
	OutHandlers.Add(TEXT("set_property"), &HandleFol_SetProperty);
}

REGISTER_MCP_CAPABILITY(FManageAssetFoliageTypeCapability)
