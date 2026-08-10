// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Mesh/NexusManageAssetPhysicalMaterialCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "NexusMcpTool.h"

void FManageAssetPhysicalMaterialCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_physical_material");
	Out.SearchAssetTypes = {TEXT("PhysicalMaterial")};
	Out.Description = TEXT("设置 PhysicalMaterial 属性：friction / restitution / density / surfaceType / raiseMassToPower。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),           FNexusSchema::Enum(TEXT("操作"), { TEXT("set") }))
		.Prop(TEXT("friction"),         FNexusSchema::Num(TEXT("摩擦系数 [0,1]")))
		.Prop(TEXT("restitution"),      FNexusSchema::Num(TEXT("弹性系数 [0,1]")))
		.Prop(TEXT("density"),          FNexusSchema::Num(TEXT("密度 g/cm³")))
		.Prop(TEXT("raiseMassToPower"), FNexusSchema::Num(TEXT("质量幂次修正 [0,1]")))
		.Prop(TEXT("surfaceType"),      FNexusSchema::Int(TEXT("表面类型枚举值（EPhysicalSurface int）")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("PhysicalMaterial 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
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

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("缺少 operations 或为空");
			return;
		}

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject>* OpPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpPtr) || !OpPtr)
			{
				Entry->SetStringField(TEXT("error"), TEXT("无效的 operation 项"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			const TSharedPtr<FJsonObject>& Op = *OpPtr;

			const FString Action = Op->HasField(TEXT("action")) ? Op->GetStringField(TEXT("action")).ToLower() : TEXT("");
			Entry->SetStringField(TEXT("action"), Action);
			if (Action != TEXT("set"))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的操作: '%s'（仅 set）"), *Action));
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
