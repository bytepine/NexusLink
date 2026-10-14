// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Material/NexusGetAssetMaterialParameterCollectionCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "Materials/MaterialParameterCollection.h"

void FNexusGetAssetMaterialParameterCollectionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetMaterialParameterCollection");
	Out.SearchAssetTypes = {TEXT("MaterialParameterCollection")};
	Out.Description = TEXT("List MPC scalar/vector params and default values.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("MPC asset path (/Game/…/MPC_Foo)")))
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Material };
	Out.ExtraSearchKeywords = { TEXT("mpc"), TEXT("parameter"), TEXT("collection"), TEXT("Material"), TEXT("global"), TEXT("Scalar"), TEXT("Vector") };
	Out.RelatedCapabilities = { TEXT("ManageAssetMaterialParameterCollection"), TEXT("GetAssetMaterial"), TEXT("ManageAssetMaterial") };
	Out.WhenToUse = TEXT("Read all MPC scalar/vector param names and defaults");
}

FCapabilityResult FNexusGetAssetMaterialParameterCollectionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		const FString AssetPath = A.Str(TEXT("AssetPath"));

		UMaterialParameterCollection* MPC = FNexusAssetUtils::LoadAssetWithFallback<UMaterialParameterCollection>(AssetPath);
		if (!MPC)
		{
			OutError = FString::Printf(TEXT("MaterialParameterCollection not found: %s"), *AssetPath);
			return;
		}

		OutEntry->SetStringField(TEXT("AssetType"), TEXT("MaterialParameterCollection"));
		OutEntry->SetStringField(TEXT("name"),      MPC->GetName());
		OutEntry->SetStringField(TEXT("Path"),      FNexusAssetUtils::PackagePathOf(MPC));
		OutEntry->SetNumberField(TEXT("ScalarParametersCount"), MPC->ScalarParameters.Num());
		OutEntry->SetNumberField(TEXT("VectorParametersCount"), MPC->VectorParameters.Num());

		// 标量参数
		TArray<TSharedPtr<FJsonValue>> ScalarArr;
		for (const FCollectionScalarParameter& P : MPC->ScalarParameters)
		{
			TSharedPtr<FJsonObject> PObj = MakeShared<FJsonObject>();
			PObj->SetStringField(TEXT("name"),         P.ParameterName.ToString());
			PObj->SetNumberField(TEXT("DefaultValue"), P.DefaultValue);
			ScalarArr.Add(MakeShared<FJsonValueObject>(PObj));
		}
		OutEntry->SetArrayField(TEXT("ScalarParameters"), ScalarArr);

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
			PObj->SetObjectField(TEXT("DefaultValue"), ColorObj);
			VectorArr.Add(MakeShared<FJsonValueObject>(PObj));
		}
		OutEntry->SetArrayField(TEXT("VectorParameters"), VectorArr);

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetMaterialParameterCollectionCapability)
