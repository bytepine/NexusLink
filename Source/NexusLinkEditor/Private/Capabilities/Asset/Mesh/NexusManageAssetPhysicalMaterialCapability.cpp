// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusManageAssetPhysicalMaterialCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "NexusMcpTool.h"

void FNexusManageAssetPhysicalMaterialCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetPhysicalMaterial");
	Out.SearchAssetTypes = {TEXT("PhysicalMaterial")};
	Out.Description = TEXT("Set PhysicalMaterial property: friction / restitution / density / SurfaceType / RaiseMassToPower.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),           FNexusSchema::Enum(TEXT("Action"), { TEXT("Set") }))
		.Prop(TEXT("Friction"),         FNexusSchema::Num(TEXT("Friction [0,1]")))
		.Prop(TEXT("Restitution"),      FNexusSchema::Num(TEXT("Restitution [0,1]")))
		.Prop(TEXT("Density"),          FNexusSchema::Num(TEXT("Density g/cm³")))
		.Prop(TEXT("RaiseMassToPower"), FNexusSchema::Num(TEXT("Mass scale power correction [0,1]")))
		.Prop(TEXT("SurfaceType"),      FNexusSchema::Int(TEXT("Surface type enum (EPhysicalSurface int)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("PhysicalMaterial asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("physical"), TEXT("Material"), TEXT("Friction"), TEXT("Surface"), TEXT("Density") };
	Out.RelatedCapabilities = { TEXT("GetAssetPhysicalMaterial"), TEXT("CreateAssetPhysicalMaterial") };
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
	if (Op->HasField(TEXT("Friction")))         PM->Friction         = static_cast<float>(Op->GetNumberField(TEXT("Friction")));
	if (Op->HasField(TEXT("Restitution")))      PM->Restitution      = static_cast<float>(Op->GetNumberField(TEXT("Restitution")));
	if (Op->HasField(TEXT("Density")))          PM->Density          = static_cast<float>(Op->GetNumberField(TEXT("Density")));
	if (Op->HasField(TEXT("RaiseMassToPower"))) PM->RaiseMassToPower = static_cast<float>(Op->GetNumberField(TEXT("RaiseMassToPower")));
	if (Op->HasField(TEXT("SurfaceType")))
	{
		const int32 SurfVal = static_cast<int32>(Op->GetNumberField(TEXT("SurfaceType")));
		PM->SurfaceType = EPhysicalSurface(SurfVal);
	}
	PMState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("name"),        PM->GetName());
	Ctx.Entry->SetNumberField(TEXT("Friction"),    PM->Friction);
	Ctx.Entry->SetNumberField(TEXT("Restitution"), PM->Restitution);
	Ctx.Entry->SetNumberField(TEXT("Density"),     PM->Density);
	Ctx.Entry->SetNumberField(TEXT("SurfaceType"), static_cast<double>(static_cast<int32>(PM->SurfaceType.GetValue())));
}

bool FNexusManageAssetPhysicalMaterialCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetPhysicalMaterialCapability::FinalizeTarget(void* Target) const
{
	FPhysMatActionState* State = static_cast<FPhysMatActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->PM) State->PM->MarkPackageDirty();
	delete State;
}

void FNexusManageAssetPhysicalMaterialCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("Set"), &HandlePM_Set);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetPhysicalMaterialCapability)
