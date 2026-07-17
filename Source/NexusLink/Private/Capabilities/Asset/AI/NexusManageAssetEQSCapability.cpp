// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusManageAssetEQSCapability.h"

#if NX_UE_HAS_APP_STYLE

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpTool.h"
#include "Capabilities/Asset/AI/NexusEQSUtils.h"
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
	Out.Description = TEXT("编辑 EQS：add_option/remove_option/set_generator/add_test/remove_test。");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(
			TEXT("操作类型"),
			{ TEXT("add_option"), TEXT("remove_option"), TEXT("set_generator"), TEXT("add_test"), TEXT("remove_test") }))
		.Prop(TEXT("optionIndex"),    FNexusSchema::Int(TEXT("Option 索引（0-based）")))
		.Prop(TEXT("generatorClass"), FNexusSchema::Str(TEXT("set_generator：生成器类名（EnvQueryGenerator_ActorsOfClass 等）")))
		.Prop(TEXT("testClass"),      FNexusSchema::Str(TEXT("add/remove_test：测试类名（EnvQueryTest_Distance 等）")))
		.Prop(TEXT("testIndex"),      FNexusSchema::Int(TEXT("remove_test：测试在 Option 内的索引")))
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),  FNexusSchema::Str(TEXT("EnvQuery 资产路径")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("操作列表"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("eqs"), TEXT("query"), TEXT("generator"), TEXT("test"), TEXT("distance"), TEXT("ai") };
	Out.RelatedCapabilities = { TEXT("get_asset_eqs"), TEXT("create_asset_eqs") };
	Out.WhenToUse = TEXT("往 EQS 里添加/删除 Option、设置 Generator、添加/删除 Test");
}

FCapabilityResult FManageAssetEQSCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
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

		const TArray<TSharedPtr<FJsonValue>>* Ops;
		if (!Arguments->TryGetArrayField(TEXT("operations"), Ops) || !Ops)
		{
			OutError = TEXT("operations 为必填数组");
			return;
		}

		TArray<UEnvQueryOption*>* Options = GetEnvQueryOptionsPtr(EQ);
		if (!Options)
		{
			OutError = TEXT("无法访问 EnvQuery::Options（UE 版本不支持）");
			return;
		}

		bool bDirty = false;

		for (const TSharedPtr<FJsonValue>& OpVal : *Ops)
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
					OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("optionIndex %lld 超出范围 [0, %d)"), Idx, Options->Num()));
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
					OpResult->SetStringField(TEXT("error"), TEXT("set_generator 需要 generatorClass"));
				}
				else if (Idx < 0 || Idx >= Options->Num())
				{
					OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("optionIndex %lld 无效"), Idx));
				}
				else
				{
					UClass* GenClass = FindEQSClassByName(GenClassName);
					if (!GenClass || !GenClass->IsChildOf(UEnvQueryGenerator::StaticClass()))
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("未找到 Generator 类: %s"), *GenClassName));
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
					OpResult->SetStringField(TEXT("error"), TEXT("add_test 需要 testClass"));
				}
				else if (Idx < 0 || Idx >= Options->Num())
				{
					OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("optionIndex %lld 无效"), Idx));
				}
				else
				{
					UClass* TestClass = FindEQSClassByName(TestClassName);
					if (!TestClass || !TestClass->IsChildOf(UEnvQueryTest::StaticClass()))
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("未找到 Test 类: %s"), *TestClassName));
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
					OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("optionIndex %lld 无效"), OptIdx));
				}
				else
				{
					UEnvQueryOption* Opt = (*Options)[static_cast<int32>(OptIdx)];
					if (TestIdx < 0 || TestIdx >= Opt->Tests.Num())
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("testIndex %lld 超出范围 [0, %d)"), TestIdx, Opt->Tests.Num()));
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
				OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
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

#endif // NX_UE_HAS_APP_STYLE
