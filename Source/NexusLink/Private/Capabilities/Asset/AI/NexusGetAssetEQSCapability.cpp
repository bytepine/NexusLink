// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusGetAssetEQSCapability.h"

#if NX_UE_HAS_APP_STYLE

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "Capabilities/Asset/AI/NexusEQSUtils.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvironmentQuery/EnvQueryTest.h"

void FGetAssetEQSCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_eqs");
	Out.SearchAssetTypes = {TEXT("EnvQuery")};
	Out.Description = TEXT("读取 EQS 的 Options/Generator/Test 概览。UE5+。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("EnvQuery 资产路径")))
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("eqs"), TEXT("query"), TEXT("environment"), TEXT("generator"), TEXT("test"), TEXT("ai") };
	Out.RelatedCapabilities = { TEXT("manage_asset_eqs"), TEXT("create_asset_eqs"), TEXT("get_asset_behavior_tree") };
	Out.WhenToUse = TEXT("读 EQS 的 Option/Generator/Test 列表及测试类名");
}

FCapabilityResult FGetAssetEQSCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
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

		UEnvQuery* EQ = FNexusAssetUtils::LoadAssetWithFallback<UEnvQuery>(AssetPath);
		if (!EQ)
		{
			OutError = FString::Printf(TEXT("EnvQuery 未找到: %s"), *AssetPath);
			return;
		}

		OutEntry->SetStringField(TEXT("assetType"), TEXT("EnvQuery"));
		OutEntry->SetStringField(TEXT("name"),      EQ->GetName());
		OutEntry->SetStringField(TEXT("path"),      FNexusAssetUtils::PackagePathOf(EQ));

		TArray<UEnvQueryOption*>* OptionsPtr = GetEnvQueryOptionsPtr(EQ);
		const int32 OptionsNum = OptionsPtr ? OptionsPtr->Num() : 0;
		OutEntry->SetNumberField(TEXT("optionsCount"), OptionsNum);

		TArray<TSharedPtr<FJsonValue>> OptionsArr;
		for (int32 i = 0; i < OptionsNum; ++i)
		{
			UEnvQueryOption* Opt = (*OptionsPtr)[i];
			if (!Opt) continue;

			TSharedPtr<FJsonObject> OObj = MakeShared<FJsonObject>();
			OObj->SetNumberField(TEXT("index"), i);

			if (Opt->Generator)
			{
				OObj->SetStringField(TEXT("generatorClass"), Opt->Generator->GetClass()->GetName());
			}

			TArray<TSharedPtr<FJsonValue>> TestArr;
			for (UEnvQueryTest* Test : Opt->Tests)
			{
				if (Test)
				{
					TSharedPtr<FJsonObject> TObj = MakeShared<FJsonObject>();
					TObj->SetStringField(TEXT("testClass"),   Test->GetClass()->GetName());
					TObj->SetStringField(TEXT("testPurpose"), UEnum::GetDisplayValueAsText(Test->TestPurpose).ToString());
					TestArr.Add(MakeShared<FJsonValueObject>(TObj));
				}
			}
			OObj->SetNumberField(TEXT("testsCount"), TestArr.Num());
			OObj->SetArrayField(TEXT("tests"), TestArr);
			OptionsArr.Add(MakeShared<FJsonValueObject>(OObj));
		}
		OutEntry->SetArrayField(TEXT("options"), OptionsArr);

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetEQSCapability)

#endif // NX_UE_HAS_APP_STYLE
