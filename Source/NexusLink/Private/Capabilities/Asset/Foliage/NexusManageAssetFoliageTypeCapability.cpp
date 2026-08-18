// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Foliage/NexusManageAssetFoliageTypeCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
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

FCapabilityResult FManageAssetFoliageTypeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UFoliageType* Type = FNexusAssetUtils::LoadAssetWithFallback<UFoliageType>(AssetPath);
		if (!Type)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("Failed to load FoliageType: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("Missing or empty operations"));
			return;
		}

		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			const TSharedPtr<FJsonObject>* OpPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpPtr) || !OpPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpPtr;
			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);
			Action = Action.ToLower();

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), AssetPath);
			Entry->SetStringField(TEXT("action"), Action);

			if (Action == TEXT("set_mesh"))
			{
				UFoliageType_InstancedStaticMesh* ISM = Cast<UFoliageType_InstancedStaticMesh>(Type);
				if (!ISM)
				{
					Entry->SetStringField(TEXT("error"), TEXT("Only InstancedStaticMesh supports set_mesh"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				FString MeshPath;
				if (!Op->TryGetStringField(TEXT("meshPath"), MeshPath) || MeshPath.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_mesh requires meshPath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				UStaticMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<UStaticMesh>(MeshPath);
				if (!Mesh)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("StaticMesh not found: %s"), *MeshPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				ISM->Mesh = Mesh;
				bDirty = true;
				Entry->SetStringField(TEXT("meshPath"), Mesh->GetPathName());
			}
			else if (Action == TEXT("set_density"))
			{
				if (!Op->HasField(TEXT("density")))
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_density requires density"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				Type->Density = static_cast<float>(Op->GetNumberField(TEXT("density")));
				bDirty = true;
				Entry->SetNumberField(TEXT("density"), Type->Density);
			}
			else if (Action == TEXT("set_property"))
			{
				FString PropPath, Value;
				Op->TryGetStringField(TEXT("propertyPath"), PropPath);
				Op->TryGetStringField(TEXT("value"), Value);
				if (PropPath.IsEmpty() || Value.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_property requires propertyPath and value"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				FString OldVal, ActualVal, Err;
				if (!FNexusPropertyUtils::WritePropertyAndEcho(Type, { PropPath }, 0, Value, OldVal, ActualVal, Err))
				{
					Entry->SetStringField(TEXT("error"), Err);
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				bDirty = true;
				Entry->SetStringField(TEXT("propertyPath"), PropPath);
				if (!OldVal.IsEmpty()) Entry->SetStringField(TEXT("oldValue"), OldVal);
				if (!ActualVal.IsEmpty()) Entry->SetStringField(TEXT("newValue"), ActualVal);
			}
			else
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported operation: '%s'"), *Action));
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		if (bDirty) Type->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetFoliageTypeCapability)
