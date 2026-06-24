// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusManageAssetStaticMeshCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "NexusMcpTool.h"

void FManageAssetStaticMeshCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_static_mesh");
	Out.Description = TEXT("编辑 StaticMesh 属性。action=set_material_slot|set_property。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),      FNexusSchema::Str(TEXT("StaticMesh 资产路径")))
		.Prop(TEXT("action"),         FNexusSchema::Enum(TEXT("操作"), { TEXT("set_material_slot"), TEXT("set_property") }))
		.Prop(TEXT("slotIndex"),      FNexusSchema::Int(TEXT("材质槽索引（set_material_slot）")))
		.Prop(TEXT("materialPath"),   FNexusSchema::Str(TEXT("材质资产路径（set_material_slot）")))
		.Prop(TEXT("propertyPath"),   FNexusSchema::Str(TEXT("属性路径（set_property）")))
		.Prop(TEXT("value"),          FNexusSchema::Str(TEXT("属性新值（set_property）")))
		.Required({ TEXT("assetPath"), TEXT("action") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mesh"), TEXT("material"), TEXT("collision"), TEXT("static") };
	Out.RelatedCapabilities = { TEXT("get_asset_static_mesh"), TEXT("search_asset") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("改 StaticMesh 材质槽/属性；修改后需 save_asset 落盘");
}

FCapabilityResult FManageAssetStaticMeshCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath, Action;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;
		if (!FNexusCapability::RequireString(Arguments, TEXT("action"), Action, OutEntries, {{TEXT("assetPath"), AssetPath}})) return;

		UStaticMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<UStaticMesh>(AssetPath);
		if (!Mesh)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}},
				FString::Printf(TEXT("StaticMesh 未找到: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("assetPath"), AssetPath);
		Entry->SetStringField(TEXT("action"), Action);

		if (Action.Equals(TEXT("set_material_slot"), ESearchCase::IgnoreCase))
		{
			int32 SlotIndex = 0;
			FString MaterialPath;
			if (Arguments.IsValid())
			{
				if (Arguments->HasField(TEXT("slotIndex")))
					SlotIndex = static_cast<int32>(Arguments->GetNumberField(TEXT("slotIndex")));
				Arguments->TryGetStringField(TEXT("materialPath"), MaterialPath);
			}
			if (MaterialPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_material_slot 需要 materialPath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
			if (!Material)
			{
				Material = FNexusAssetUtils::LoadAssetWithFallback<UMaterialInterface>(MaterialPath);
			}
#if NX_UE_HAS_STATIC_MESH_ACCESSORS
			const TArray<FStaticMaterial>& Materials = Mesh->GetStaticMaterials();
#else
			const TArray<FStaticMaterial>& Materials = Mesh->StaticMaterials;
#endif
			if (!Materials.IsValidIndex(SlotIndex))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("材质槽索引 %d 超出范围 [0, %d)"), SlotIndex, Materials.Num()));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
#if WITH_EDITOR
			Mesh->SetMaterial(SlotIndex, Material);
			Mesh->MarkPackageDirty();
			Entry->SetNumberField(TEXT("slotIndex"), SlotIndex);
			Entry->SetStringField(TEXT("materialClass"), Material ? Material->GetClass()->GetName() : TEXT("None"));
			Entry->SetBoolField(TEXT("success"), true);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
#else
			Entry->SetStringField(TEXT("error"), TEXT("set_material_slot 仅在编辑器构建可用"));
#endif
		}
		else if (Action.Equals(TEXT("set_property"), ESearchCase::IgnoreCase))
		{
			FString PropPath, Value;
			if (Arguments.IsValid())
			{
				Arguments->TryGetStringField(TEXT("propertyPath"), PropPath);
				Arguments->TryGetStringField(TEXT("value"), Value);
			}
			if (PropPath.IsEmpty() || Value.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_property 需要 propertyPath 和 value"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			FString OldVal, ActualVal, Err;
			if (!FNexusPropertyUtils::WritePropertyAndEcho(Mesh, { PropPath }, 0, Value, OldVal, ActualVal, Err))
			{
				Entry->SetStringField(TEXT("error"), Err);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			Mesh->MarkPackageDirty();
			Entry->SetStringField(TEXT("propertyPath"), PropPath);
			if (!OldVal.IsEmpty()) Entry->SetStringField(TEXT("oldValue"), OldVal);
			if (!ActualVal.IsEmpty()) Entry->SetStringField(TEXT("newValue"), ActualVal);
			Entry->SetBoolField(TEXT("success"), true);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
		}
		else
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetStaticMeshCapability)
