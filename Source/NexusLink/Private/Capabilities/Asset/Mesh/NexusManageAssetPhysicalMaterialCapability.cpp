// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusManageAssetPhysicalMaterialCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "NexusMcpTool.h"

void FManageAssetPhysicalMaterialCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_physical_material");
	Out.Description = TEXT("设置 PhysicalMaterial 属性：friction / restitution / density / surfaceType / raiseMassToPower。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),       FNexusSchema::Str(TEXT("PhysicalMaterial 资产路径")))
		.Prop(TEXT("friction"),            FNexusSchema::Num(TEXT("摩擦系数 [0,1]")))
		.Prop(TEXT("restitution"),         FNexusSchema::Num(TEXT("弹性系数 [0,1]")))
		.Prop(TEXT("density"),             FNexusSchema::Num(TEXT("密度 g/cm³")))
		.Prop(TEXT("raiseMassToPower"),    FNexusSchema::Num(TEXT("质量幂次修正 [0,1]")))
		.Prop(TEXT("surfaceType"),         FNexusSchema::Int(TEXT("表面类型枚举值（EPhysicalSurface int）")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("physical"), TEXT("material"), TEXT("friction"), TEXT("surface"), TEXT("density") };
	Out.RelatedCapabilities = { TEXT("get_asset_physical_material") };
}

FCapabilityResult FManageAssetPhysicalMaterialCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		UPhysicalMaterial* PM = LoadObject<UPhysicalMaterial>(nullptr, *AssetPath);
		if (!PM)
		{
			OutError = FString::Printf(TEXT("加载 PhysicalMaterial 失败: %s"), *AssetPath);
			return;
		}

		if (Arguments->HasField(TEXT("friction")))         PM->Friction         = (float)Arguments->GetNumberField(TEXT("friction"));
		if (Arguments->HasField(TEXT("restitution")))      PM->Restitution      = (float)Arguments->GetNumberField(TEXT("restitution"));
		if (Arguments->HasField(TEXT("density")))          PM->Density          = (float)Arguments->GetNumberField(TEXT("density"));
		if (Arguments->HasField(TEXT("raiseMassToPower"))) PM->RaiseMassToPower = (float)Arguments->GetNumberField(TEXT("raiseMassToPower"));
		if (Arguments->HasField(TEXT("surfaceType")))
		{
			const int32 SurfVal = (int32)Arguments->GetNumberField(TEXT("surfaceType"));
			PM->SurfaceType = EPhysicalSurface(SurfVal);
		}

		PM->MarkPackageDirty();

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),        PM->GetName());
		Entry->SetNumberField(TEXT("friction"),    PM->Friction);
		Entry->SetNumberField(TEXT("restitution"), PM->Restitution);
		Entry->SetNumberField(TEXT("density"),     PM->Density);
		Entry->SetNumberField(TEXT("surfaceType"), (double)(int32)PM->SurfaceType.GetValue());
		Entry->SetBoolField(TEXT("success"),       true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetPhysicalMaterialCapability)
