// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusManageAssetSkeletalMeshCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#if NX_UE_HAS_SKELETAL_MATERIAL_COMMON_HEADER
#include "Engine/SkinnedAssetCommon.h"
#endif
#include "NexusMcpTool.h"

void FManageAssetSkeletalMeshCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_skeletal_mesh");
	Out.Description = TEXT("编辑 SkeletalMesh 属性。action=set_material_slot|set_property。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),      FNexusSchema::Str(TEXT("SkeletalMesh 资产路径")))
		.Prop(TEXT("action"),         FNexusSchema::Enum(TEXT("操作"), { TEXT("set_material_slot"), TEXT("set_property") }))
		.Prop(TEXT("slotIndex"),      FNexusSchema::Int(TEXT("材质槽索引（set_material_slot）")))
		.Prop(TEXT("materialPath"),   FNexusSchema::Str(TEXT("材质资产路径（set_material_slot）")))
		.Prop(TEXT("propertyPath"),   FNexusSchema::Str(TEXT("属性路径（set_property）")))
		.Prop(TEXT("value"),          FNexusSchema::Str(TEXT("属性新值（set_property）")))
		.Required({ TEXT("assetPath"), TEXT("action") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mesh"), TEXT("material"), TEXT("skeletal"), TEXT("bone") };
	Out.RelatedCapabilities = { TEXT("get_asset_skeletal_mesh"), TEXT("get_asset_skeleton") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("改 SkeletalMesh 材质槽/属性；修改后需 save_asset 落盘");
}

FCapabilityResult FManageAssetSkeletalMeshCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath, Action;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;
		if (!FNexusCapability::RequireString(Arguments, TEXT("action"), Action, OutEntries, {{TEXT("assetPath"), AssetPath}})) return;

		USkeletalMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<USkeletalMesh>(AssetPath);
		if (!Mesh)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}},
				FString::Printf(TEXT("SkeletalMesh 未找到: %s"), *AssetPath));
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
#if NX_UE_HAS_SKELETAL_MESH_ACCESSORS
			const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
#else
			const TArray<FSkeletalMaterial>& Materials = Mesh->Materials;
#endif
			if (!Materials.IsValidIndex(SlotIndex))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("材质槽索引 %d 超出范围 [0, %d)"), SlotIndex, Materials.Num()));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			// USkeletalMesh 在 UE4.26 没有 SetMaterial，通过反射修改 Materials 数组
			FSkeletalMaterial& MatEntry = const_cast<FSkeletalMaterial&>(Materials[SlotIndex]);
			MatEntry.MaterialInterface = Material;
			Mesh->MarkPackageDirty();
			Entry->SetNumberField(TEXT("slotIndex"), SlotIndex);
			Entry->SetStringField(TEXT("materialClass"), Material ? Material->GetClass()->GetName() : TEXT("None"));
			Entry->SetBoolField(TEXT("success"), true);
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
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

REGISTER_MCP_CAPABILITY(FManageAssetSkeletalMeshCapability)
