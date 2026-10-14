// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusGetAssetEQSCapability.h"

#if NX_UE_HAS_EQS

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "Utils/NexusEQSUtils.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvironmentQuery/EnvQueryTest.h"

void FNexusGetAssetEQSCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetEQS");
	Out.SearchAssetTypes = {TEXT("EnvQuery")};
	Out.Description = TEXT("Read EQS Options/Generator/Test overview. UE5+.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("EnvQuery asset path")))
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("eqs"), TEXT("Query"), TEXT("environment"), TEXT("generator"), TEXT("test"), TEXT("ai") };
	Out.RelatedCapabilities = { TEXT("ManageAssetEQS"), TEXT("CreateAssetEQS"), TEXT("GetAssetBehaviorTree") };
	Out.WhenToUse = TEXT("Read EQS Option/Generator/Test list and test class names");
}

FCapabilityResult FNexusGetAssetEQSCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		const FString AssetPath = A.Str(TEXT("AssetPath"));

		UEnvQuery* EQ = FNexusAssetUtils::LoadAssetWithFallback<UEnvQuery>(AssetPath);
		if (!EQ)
		{
			OutError = FString::Printf(TEXT("EnvQuery not found: %s"), *AssetPath);
			return;
		}

		OutEntry->SetStringField(TEXT("AssetType"), TEXT("EnvQuery"));
		OutEntry->SetStringField(TEXT("name"),      EQ->GetName());
		OutEntry->SetStringField(TEXT("Path"),      FNexusAssetUtils::PackagePathOf(EQ));

		TArray<UEnvQueryOption*>* OptionsPtr = GetEnvQueryOptionsPtr(EQ);
		const int32 OptionsNum = OptionsPtr ? OptionsPtr->Num() : 0;
		OutEntry->SetNumberField(TEXT("OptionsCount"), OptionsNum);

		TArray<TSharedPtr<FJsonValue>> OptionsArr;
		for (int32 i = 0; i < OptionsNum; ++i)
		{
			UEnvQueryOption* Opt = (*OptionsPtr)[i];
			if (!Opt) continue;

			TSharedPtr<FJsonObject> OObj = MakeShared<FJsonObject>();
			OObj->SetNumberField(TEXT("Index"), i);

			if (Opt->Generator)
			{
				OObj->SetStringField(TEXT("GeneratorClass"), Opt->Generator->GetClass()->GetName());
			}

			TArray<TSharedPtr<FJsonValue>> TestArr;
			for (UEnvQueryTest* Test : Opt->Tests)
			{
				if (Test)
				{
					TSharedPtr<FJsonObject> TObj = MakeShared<FJsonObject>();
					TObj->SetStringField(TEXT("TestClass"),   Test->GetClass()->GetName());
					TObj->SetStringField(TEXT("TestPurpose"), UEnum::GetDisplayValueAsText(Test->TestPurpose).ToString());
					TestArr.Add(MakeShared<FJsonValueObject>(TObj));
				}
			}
			OObj->SetNumberField(TEXT("TestsCount"), TestArr.Num());
			OObj->SetArrayField(TEXT("Tests"), TestArr);
			OptionsArr.Add(MakeShared<FJsonValueObject>(OObj));
		}
		OutEntry->SetArrayField(TEXT("Options"), OptionsArr);

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetEQSCapability)

#endif // NX_UE_HAS_EQS
