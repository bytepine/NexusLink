// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusManageAssetStaticMeshCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
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
	Out.SearchAssetTypes = {TEXT("StaticMesh")};
	Out.Description = TEXT("批量编辑 StaticMesh 属性。operations[].action=set_material_slot|set_property。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),         FNexusSchema::Enum(TEXT("操作"), { TEXT("set_material_slot"), TEXT("set_property") }))
		.Prop(TEXT("slotIndex"),      FNexusSchema::Int(TEXT("材质槽索引（set_material_slot）")))
		.Prop(TEXT("materialPath"),   FNexusSchema::Str(TEXT("材质资产路径（set_material_slot）")))
		.Prop(TEXT("propertyPath"),   FNexusSchema::Str(TEXT("属性路径（set_property）")))
		.Prop(TEXT("value"),          FNexusSchema::Str(TEXT("属性新值（set_property）")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("StaticMesh 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
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
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UStaticMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<UStaticMesh>(AssetPath);
		if (!Mesh)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("StaticMesh 未找到: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("缺少 operations 或为空"));
			return;
		}

		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpObjPtr;

			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), AssetPath);
			Entry->SetStringField(TEXT("action"), Action);

			if (Action.Equals(TEXT("set_material_slot"), ESearchCase::IgnoreCase))
			{
				int32 SlotIndex = 0;
				FString MaterialPath;
				if (Op->HasField(TEXT("slotIndex")))
					SlotIndex = static_cast<int32>(Op->GetNumberField(TEXT("slotIndex")));
				Op->TryGetStringField(TEXT("materialPath"), MaterialPath);
				if (MaterialPath.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_material_slot 需要 materialPath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
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
					continue;
				}
#if WITH_EDITOR
				Mesh->SetMaterial(SlotIndex, Material);
				bDirty = true;
				Entry->SetNumberField(TEXT("slotIndex"), SlotIndex);
				Entry->SetStringField(TEXT("materialClass"), Material ? Material->GetClass()->GetName() : TEXT("None"));
				Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
#else
				Entry->SetStringField(TEXT("error"), TEXT("set_material_slot 仅在编辑器构建可用"));
#endif
			}
			else if (Action.Equals(TEXT("set_property"), ESearchCase::IgnoreCase))
			{
				FString PropPath, Value;
				Op->TryGetStringField(TEXT("propertyPath"), PropPath);
				Op->TryGetStringField(TEXT("value"), Value);
				if (PropPath.IsEmpty() || Value.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_property 需要 propertyPath 和 value"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				FString OldVal, ActualVal, Err;
				if (!FNexusPropertyUtils::WritePropertyAndEcho(Mesh, { PropPath }, 0, Value, OldVal, ActualVal, Err))
				{
					Entry->SetStringField(TEXT("error"), Err);
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				bDirty = true;
				Entry->SetStringField(TEXT("propertyPath"), PropPath);
				if (!OldVal.IsEmpty()) Entry->SetStringField(TEXT("oldValue"), OldVal);
				if (!ActualVal.IsEmpty()) Entry->SetStringField(TEXT("newValue"), ActualVal);
				Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
			}
			else
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}

		if (bDirty) Mesh->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetStaticMeshCapability)
