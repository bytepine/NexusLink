// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Material/NexusGetAssetMaterialParameterCollectionCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "Materials/MaterialParameterCollection.h"

void FGetAssetMaterialParameterCollectionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_material_parameter_collection");
	Out.SearchAssetTypes = {TEXT("MaterialParameterCollection")};
	Out.Description = TEXT("List MPC scalar/vector params and default values.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("MPC asset path (/Game/…/MPC_Foo)")))
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Material };
	Out.ExtraSearchKeywords = { TEXT("mpc"), TEXT("parameter"), TEXT("collection"), TEXT("material"), TEXT("global"), TEXT("scalar"), TEXT("vector") };
	Out.RelatedCapabilities = { TEXT("manage_asset_material_parameter_collection"), TEXT("get_asset_material"), TEXT("manage_asset_material") };
	Out.WhenToUse = TEXT("Read all MPC scalar/vector param names and defaults");
}

FCapabilityResult FGetAssetMaterialParameterCollectionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		const FString AssetPath = A.Str(TEXT("assetPath"));

		UMaterialParameterCollection* MPC = FNexusAssetUtils::LoadAssetWithFallback<UMaterialParameterCollection>(AssetPath);
		if (!MPC)
		{
			OutError = FString::Printf(TEXT("MaterialParameterCollection not found: %s"), *AssetPath);
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
