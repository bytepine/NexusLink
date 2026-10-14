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

void FNexusManageAssetFoliageTypeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetFoliageType");
	Out.SearchAssetTypes = {TEXT("FoliageType")};
	Out.Description = TEXT("Batch edit FoliageType. Action=SetMesh/SetDensity/SetProperty.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("SetMesh"), TEXT("SetDensity"), TEXT("SetProperty") }))
		.Prop(TEXT("MeshPath"), FNexusSchema::Str(TEXT("StaticMesh path (SetMesh)")))
		.Prop(TEXT("Density"), FNexusSchema::Num(TEXT("Density (SetDensity)")))
		.Prop(TEXT("PropertyPath"), FNexusSchema::Str(TEXT("Property path (SetProperty)")))
		.Prop(TEXT("Value"), FNexusSchema::Str(TEXT("New property value (SetProperty)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("FoliageType asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("foliage"), TEXT("mesh"), TEXT("Density"), TEXT("vegetation") };
	Out.RelatedCapabilities = { TEXT("GetAssetFoliageType"), TEXT("CreateAssetFoliageType") };
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
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Only InstancedStaticMesh supports SetMesh"));
		return;
	}
	const FString MeshPath = FNexusArgs(Op).Str(TEXT("MeshPath"));
	if (MeshPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetMesh requires MeshPath"));
		return;
	}
	UStaticMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<UStaticMesh>(MeshPath);
	if (!Mesh)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("StaticMesh not found: %s"), *MeshPath));
		return;
	}
	ISM->Mesh = Mesh;
	MarkFolDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("MeshPath"), Mesh->GetPathName());
}

static void HandleFol_SetDensity(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UFoliageType* Type = FolFrom(Ctx);
	if (!Type)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("FoliageType is null"));
		return;
	}
	if (!Op->HasField(TEXT("Density")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetDensity requires density"));
		return;
	}
	Type->Density = static_cast<float>(Op->GetNumberField(TEXT("Density")));
	MarkFolDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("Density"), Type->Density);
}

static void HandleFol_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UFoliageType* Type = FolFrom(Ctx);
	const FNexusArgs A(Op);
	const FString PropPath = A.Str(TEXT("PropertyPath"));
	const FString Value = A.Str(TEXT("Value"));
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetProperty requires PropertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Type, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), Err);
		return;
	}
	MarkFolDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("PropertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("OldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("NewValue"), ActualVal);
}

bool FNexusManageAssetFoliageTypeCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetFoliageTypeCapability::FinalizeTarget(void* Target) const
{
	FFoliageActionState* State = static_cast<FFoliageActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Type)
	{
		State->Type->MarkPackageDirty();
	}
	delete State;
}

void FNexusManageAssetFoliageTypeCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetMesh"),     &HandleFol_SetMesh);
	OutHandlers.Add(TEXT("SetDensity"),  &HandleFol_SetDensity);
	OutHandlers.Add(TEXT("SetProperty"), &HandleFol_SetProperty);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetFoliageTypeCapability)
