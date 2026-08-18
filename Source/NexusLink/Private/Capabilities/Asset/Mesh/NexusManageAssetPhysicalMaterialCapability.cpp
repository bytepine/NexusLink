// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusManageAssetPhysicalMaterialCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
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

FCapabilityResult FManageAssetPhysicalMaterialCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));
		UPhysicalMaterial* PM = LoadObject<UPhysicalMaterial>(nullptr, *AssetPath);
		if (!PM)
		{
			OutError = FString::Printf(TEXT("Failed to load PhysicalMaterial: %s"), *AssetPath);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("Missing or empty operations");
			return;
		}

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject>* OpPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpPtr) || !OpPtr)
			{
				Entry->SetStringField(TEXT("error"), TEXT("Invalid operation item"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			const TSharedPtr<FJsonObject>& Op = *OpPtr;

			const FString Action = FNexusArgs(Op).Str(TEXT("action")).ToLower();
			Entry->SetStringField(TEXT("action"), Action);
			if (Action != TEXT("set"))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported operation: '%s' (set only)"), *Action));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			if (Op->HasField(TEXT("friction")))         PM->Friction         = (float)Op->GetNumberField(TEXT("friction"));
			if (Op->HasField(TEXT("restitution")))      PM->Restitution      = (float)Op->GetNumberField(TEXT("restitution"));
			if (Op->HasField(TEXT("density")))          PM->Density          = (float)Op->GetNumberField(TEXT("density"));
			if (Op->HasField(TEXT("raiseMassToPower"))) PM->RaiseMassToPower = (float)Op->GetNumberField(TEXT("raiseMassToPower"));
			if (Op->HasField(TEXT("surfaceType")))
			{
				const int32 SurfVal = (int32)Op->GetNumberField(TEXT("surfaceType"));
				PM->SurfaceType = EPhysicalSurface(SurfVal);
			}

			PM->MarkPackageDirty();

			Entry->SetStringField(TEXT("name"),        PM->GetName());
			Entry->SetNumberField(TEXT("friction"),    PM->Friction);
			Entry->SetNumberField(TEXT("restitution"), PM->Restitution);
			Entry->SetNumberField(TEXT("density"),     PM->Density);
			Entry->SetNumberField(TEXT("surfaceType"), (double)(int32)PM->SurfaceType.GetValue());
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetPhysicalMaterialCapability)
