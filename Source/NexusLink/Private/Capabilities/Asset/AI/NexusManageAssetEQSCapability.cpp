// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusManageAssetEQSCapability.h"

#if NX_UE_HAS_EQS

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusJsonUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpTool.h"
#include "Utils/NexusEQSUtils.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvironmentQuery/EnvQueryTest.h"

// ── 辅助：按类名查找 UClass（兼容 4.x / 5.x）────────────────────────────────

static UClass* FindEQSClassByName(const FString& ClassName)
{
#if NX_UE_HAS_FIND_FIRST_OBJECT
	UClass* C = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst);
	if (!C)
		C = FindFirstObject<UClass>(*(TEXT("U") + ClassName), EFindFirstObjectOptions::NativeFirst);
#else
	UClass* C = FindObject<UClass>(ANY_PACKAGE, *ClassName);
	if (!C)
		C = FindObject<UClass>(ANY_PACKAGE, *(TEXT("U") + ClassName));
#endif
	return C;
}

// ── Capability ────────────────────────────────────────────────────────────────

void FManageAssetEQSCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_eqs");
	Out.SearchAssetTypes = {TEXT("EnvQuery")};
	Out.Description = TEXT("Edit EQS Option/Generator/Test. UE5+ (4.26 file gated).");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(
			TEXT("Operation type"),
			{ TEXT("add_option"), TEXT("remove_option"), TEXT("set_generator"), TEXT("add_test"), TEXT("remove_test") }))
		.Prop(TEXT("optionIndex"),    FNexusSchema::Int(TEXT("Option index (0-based)")))
		.Prop(TEXT("generatorClass"), FNexusSchema::Str(TEXT("set_generator: generator class (EnvQueryGenerator_ActorsOfClass, etc.)")))
		.Prop(TEXT("testClass"),      FNexusSchema::Str(TEXT("add/remove_test: test class (EnvQueryTest_Distance, etc.)")))
		.Prop(TEXT("testIndex"),      FNexusSchema::Int(TEXT("remove_test: test index within Option")))
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),  FNexusSchema::Str(TEXT("EnvQuery asset path")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("eqs"), TEXT("query"), TEXT("generator"), TEXT("test"), TEXT("distance"), TEXT("ai") };
	Out.RelatedCapabilities = { TEXT("get_asset_eqs"), TEXT("create_asset_eqs") };
	Out.WhenToUse = TEXT("Add/remove EQS Options, set Generator, add/remove Tests");
}

FCapabilityResult FManageAssetEQSCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));

		UEnvQuery* EQ = FNexusAssetUtils::LoadAssetWithFallback<UEnvQuery>(AssetPath);
		if (!EQ)
		{
			OutError = FString::Printf(TEXT("EnvQuery not found: %s"), *AssetPath);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("operations is a required array");
			return;
		}

		TArray<UEnvQueryOption*>* Options = GetEnvQueryOptionsPtr(EQ);
		if (!Options)
		{
			OutError = TEXT("Unable to access EnvQuery::Options (UE version unsupported)");
			return;
		}

		bool bDirty = false;

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			TSharedPtr<FJsonObject> Op = OpVal->AsObject();
			if (!Op.IsValid()) continue;

			TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);

			if (Action == TEXT("add_option"))
			{
				UEnvQueryOption* NewOpt = NewObject<UEnvQueryOption>(EQ, NAME_None, RF_Transactional);
				Options->Add(NewOpt);
				OpResult->SetNumberField(TEXT("optionIndex"), Options->Num() - 1);
				bDirty = true;
			}
			else if (Action == TEXT("remove_option"))
			{
				int64 Idx = -1;
				Op->TryGetNumberField(TEXT("optionIndex"), Idx);
				if (Idx < 0 || Idx >= Options->Num())
				{
					OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("optionIndex %lld out of range [0, %d)"), Idx, Options->Num()));
				}
				else
				{
					Options->RemoveAt(static_cast<int32>(Idx));
					bDirty = true;
				}
			}
			else if (Action == TEXT("set_generator"))
			{
				int64 Idx = 0;
				Op->TryGetNumberField(TEXT("optionIndex"), Idx);
				FString GenClassName;
				if (!Op->TryGetStringField(TEXT("generatorClass"), GenClassName))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("set_generator requires generatorClass"));
				}
				else if (Idx < 0 || Idx >= Options->Num())
				{
					OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid optionIndex %lld"), Idx));
				}
				else
				{
					UClass* GenClass = FindEQSClassByName(GenClassName);
					if (!GenClass || !GenClass->IsChildOf(UEnvQueryGenerator::StaticClass()))
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Generator class not found: %s"), *GenClassName));
					}
					else
					{
						UEnvQueryOption* Opt = (*Options)[static_cast<int32>(Idx)];
						Opt->Generator = NewObject<UEnvQueryGenerator>(Opt, GenClass, NAME_None, RF_Transactional);
						bDirty = true;
					}
				}
			}
			else if (Action == TEXT("add_test"))
			{
				int64 Idx = 0;
				Op->TryGetNumberField(TEXT("optionIndex"), Idx);
				FString TestClassName;
				if (!Op->TryGetStringField(TEXT("testClass"), TestClassName))
				{
					OpResult->SetStringField(TEXT("error"), TEXT("add_test requires testClass"));
				}
				else if (Idx < 0 || Idx >= Options->Num())
				{
					OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid optionIndex %lld"), Idx));
				}
				else
				{
					UClass* TestClass = FindEQSClassByName(TestClassName);
					if (!TestClass || !TestClass->IsChildOf(UEnvQueryTest::StaticClass()))
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Test class not found: %s"), *TestClassName));
					}
					else
					{
						UEnvQueryOption* Opt = (*Options)[static_cast<int32>(Idx)];
						UEnvQueryTest* NewTest = NewObject<UEnvQueryTest>(Opt, TestClass, NAME_None, RF_Transactional);
						Opt->Tests.Add(NewTest);
						OpResult->SetNumberField(TEXT("testIndex"), Opt->Tests.Num() - 1);
						bDirty = true;
					}
				}
			}
			else if (Action == TEXT("remove_test"))
			{
				int64 OptIdx = 0, TestIdx = -1;
				Op->TryGetNumberField(TEXT("optionIndex"), OptIdx);
				Op->TryGetNumberField(TEXT("testIndex"),   TestIdx);
				if (OptIdx < 0 || OptIdx >= Options->Num())
				{
					OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid optionIndex %lld"), OptIdx));
				}
				else
				{
					UEnvQueryOption* Opt = (*Options)[static_cast<int32>(OptIdx)];
					if (TestIdx < 0 || TestIdx >= Opt->Tests.Num())
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("testIndex %lld out of range [0, %d)"), TestIdx, Opt->Tests.Num()));
					}
					else
					{
						Opt->Tests.RemoveAt(static_cast<int32>(TestIdx));
						bDirty = true;
					}
				}
			}
			else
			{
				OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: %s"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(OpResult));
		}

		if (bDirty)
		{
			EQ->MarkPackageDirty();
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetEQSCapability)

#endif // NX_UE_HAS_EQS
