// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Foliage/NexusCreateAssetFoliageTypeCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "Engine/StaticMesh.h"
#include "NexusMcpTool.h"

void FCreateAssetFoliageTypeCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_foliage_type");
	Out.Description = TEXT("创建 FoliageType（InstancedStaticMesh）。可选 meshPath。不含关卡刷草。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径")))
		.Prop(TEXT("meshPath"), FNexusSchema::Str(TEXT("StaticMesh 路径（可选）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("foliage"), TEXT("grass"), TEXT("instanced"), TEXT("vegetation") };
	Out.RelatedCapabilities = { TEXT("get_asset_foliage_type"), TEXT("manage_asset_foliage_type") };
	Out.WhenToUse = TEXT("新建植被类型资产；关卡内绘制走编辑器");
}

FCapabilityResult FCreateAssetFoliageTypeCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		if (LoadObject<UFoliageType_InstancedStaticMesh>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("FoliageType already exists: %s"), *AssetPath));
			return;
		}
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UFoliageType_InstancedStaticMesh* Type = NewObject<UFoliageType_InstancedStaticMesh>(
			Package, *AssetName, RF_Public | RF_Standalone);
		if (!Type) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建失败")); return; }

		FString MeshPath;
		if (Arguments->TryGetStringField(TEXT("meshPath"), MeshPath) && !MeshPath.IsEmpty())
		{
			if (UStaticMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<UStaticMesh>(MeshPath))
			{
				Type->Mesh = Mesh;
			}
		}
		FNexusAssetUtils::NotifyAndSaveCreated(Package, Type, AssetPath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Type->GetName());
		Entry->SetStringField(TEXT("path"), Type->GetPathName());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetFoliageTypeCapability)
