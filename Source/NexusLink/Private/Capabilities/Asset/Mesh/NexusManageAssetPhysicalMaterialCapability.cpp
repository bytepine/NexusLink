// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusManageAssetPhysicalMaterialCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "NexusMcpTool.h"

void FManageAssetPhysicalMaterialCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_physical_material");
	Out.SearchAssetTypes = {TEXT("PhysicalMaterial")};
	Out.Description = TEXT("Set PhysicalMaterial property: friction / restitution / density / surfaceType / raiseMassToPower.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),           FNexusSchema::Enum(TEXT("Action"), { TEXT("set") }))
		.Prop(TEXT("friction"),         FNexusSchema::Num(TEXT("Friction [0,1]")))
		.Prop(TEXT("restitution"),      FNexusSchema::Num(TEXT("Restitution [0,1]")))
		.Prop(TEXT("density"),          FNexusSchema::Num(TEXT("Density g/cm³")))
		.Prop(TEXT("raiseMassToPower"), FNexusSchema::Num(TEXT("Mass scale power correction [0,1]")))
		.Prop(TEXT("surfaceType"),      FNexusSchema::Int(TEXT("Surface type enum (EPhysicalSurface int)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("PhysicalMaterial asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("physical"), TEXT("material"), TEXT("friction"), TEXT("surface"), TEXT("density") };
	Out.RelatedCapabilities = { TEXT("get_asset_physical_material"), TEXT("create_asset_physical_material") };
}

struct FPhysMatActionState
{
	UPhysicalMaterial* PM = nullptr;
	bool bDirty = false;
};

static FPhysMatActionState* PMState(FNexusActionContext& Ctx)
{
	return static_cast<FPhysMatActionState*>(Ctx.Target);
}

static void HandlePM_Set(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPhysicalMaterial* PM = PMState(Ctx)->PM;
	if (Op->HasField(TEXT("friction")))         PM->Friction         = static_cast<float>(Op->GetNumberField(TEXT("friction")));
	if (Op->HasField(TEXT("restitution")))      PM->Restitution      = static_cast<float>(Op->GetNumberField(TEXT("restitution")));
	if (Op->HasField(TEXT("density")))          PM->Density          = static_cast<float>(Op->GetNumberField(TEXT("density")));
	if (Op->HasField(TEXT("raiseMassToPower"))) PM->RaiseMassToPower = static_cast<float>(Op->GetNumberField(TEXT("raiseMassToPower")));
	if (Op->HasField(TEXT("surfaceType")))
	{
		const int32 SurfVal = static_cast<int32>(Op->GetNumberField(TEXT("surfaceType")));
		PM->SurfaceType = EPhysicalSurface(SurfVal);
	}
	PMState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("name"),        PM->GetName());
	Ctx.Entry->SetNumberField(TEXT("friction"),    PM->Friction);
	Ctx.Entry->SetNumberField(TEXT("restitution"), PM->Restitution);
	Ctx.Entry->SetNumberField(TEXT("density"),     PM->Density);
	Ctx.Entry->SetNumberField(TEXT("surfaceType"), static_cast<double>(static_cast<int32>(PM->SurfaceType.GetValue())));
}

bool FManageAssetPhysicalMaterialCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UPhysicalMaterial* PM = LoadObject<UPhysicalMaterial>(nullptr, *AssetPath);
	if (!PM)
	{
		OutError = FString::Printf(TEXT("Failed to load PhysicalMaterial: %s"), *AssetPath);
		return false;
	}
	FPhysMatActionState* State = new FPhysMatActionState();
	State->PM = PM;
	OutTarget = State;
	return true;
}

void FManageAssetPhysicalMaterialCapability::FinalizeTarget(void* Target) const
{
	FPhysMatActionState* State = static_cast<FPhysMatActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->PM) State->PM->MarkPackageDirty();
	delete State;
}

void FManageAssetPhysicalMaterialCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set"), &HandlePM_Set);
}

REGISTER_MCP_CAPABILITY(FManageAssetPhysicalMaterialCapability)
