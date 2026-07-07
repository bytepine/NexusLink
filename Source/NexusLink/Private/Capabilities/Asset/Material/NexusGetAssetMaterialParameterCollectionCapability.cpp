// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Material/NexusGetAssetMaterialParameterCollectionCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "Materials/MaterialParameterCollection.h"

void FGetAssetMaterialParameterCollectionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_material_parameter_collection");
	Out.Description = TEXT("列举 MaterialParameterCollection 的标量/向量参数及其默认值。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("MPC 资产路径（/Game/…/MPC_Foo）")))
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Material };
	Out.ExtraSearchKeywords = { TEXT("mpc"), TEXT("parameter"), TEXT("collection"), TEXT("material"), TEXT("global"), TEXT("scalar"), TEXT("vector") };
	Out.RelatedCapabilities = { TEXT("manage_asset_material_parameter_collection"), TEXT("get_asset_material"), TEXT("manage_asset_material") };
	Out.WhenToUse = TEXT("读 MPC 的全部标量/向量参数名与默认值");
}

FCapabilityResult FGetAssetMaterialParameterCollectionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		UMaterialParameterCollection* MPC = FNexusAssetUtils::LoadAssetWithFallback<UMaterialParameterCollection>(AssetPath);
		if (!MPC)
		{
			OutError = FString::Printf(TEXT("MaterialParameterCollection 未找到: %s"), *AssetPath);
			return;
		}

		OutEntry->SetStringField(TEXT("assetType"), TEXT("MaterialParameterCollection"));
		OutEntry->SetStringField(TEXT("name"),      MPC->GetName());
		OutEntry->SetStringField(TEXT("path"),      FNexusAssetUtils::PackagePathOf(MPC));
		OutEntry->SetNumberField(TEXT("scalarParametersCount"), MPC->ScalarParameters.Num());
		OutEntry->SetNumberField(TEXT("vectorParametersCount"), MPC->VectorParameters.Num());

		// 标量参数
		TArray<TSharedPtr<FJsonValue>> ScalarArr;
		for (const FCollectionScalarParameter& P : MPC->ScalarParameters)
		{
			TSharedPtr<FJsonObject> PObj = MakeShared<FJsonObject>();
			PObj->SetStringField(TEXT("name"),         P.ParameterName.ToString());
			PObj->SetNumberField(TEXT("defaultValue"), P.DefaultValue);
			ScalarArr.Add(MakeShared<FJsonValueObject>(PObj));
		}
		OutEntry->SetArrayField(TEXT("scalarParameters"), ScalarArr);

		// 向量参数
		TArray<TSharedPtr<FJsonValue>> VectorArr;
		for (const FCollectionVectorParameter& P : MPC->VectorParameters)
		{
			TSharedPtr<FJsonObject> PObj = MakeShared<FJsonObject>();
			PObj->SetStringField(TEXT("name"), P.ParameterName.ToString());
			TSharedPtr<FJsonObject> ColorObj = MakeShared<FJsonObject>();
			ColorObj->SetNumberField(TEXT("r"), P.DefaultValue.R);
			ColorObj->SetNumberField(TEXT("g"), P.DefaultValue.G);
			ColorObj->SetNumberField(TEXT("b"), P.DefaultValue.B);
			ColorObj->SetNumberField(TEXT("a"), P.DefaultValue.A);
			PObj->SetObjectField(TEXT("defaultValue"), ColorObj);
			VectorArr.Add(MakeShared<FJsonValueObject>(PObj));
		}
		OutEntry->SetArrayField(TEXT("vectorParameters"), VectorArr);

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetMaterialParameterCollectionCapability)
