// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/AI/NexusManageAssetEQSCapability.h"

#if NX_UE_HAS_EQS

#include "Utils/NexusArgs.h"
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

void FNexusManageAssetEQSCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetEQS");
	Out.SearchAssetTypes = {TEXT("EnvQuery")};
	Out.Description = TEXT("Edit EQS Option/Generator/Test. UE5+ (4.26 file gated).");

	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("Action"), FNexusSchema::Enum(
			TEXT("Operation type"),
			{ TEXT("AddOption"), TEXT("RemoveOption"), TEXT("SetGenerator"), TEXT("AddTest"), TEXT("RemoveTest") }))
		.Prop(TEXT("OptionIndex"),    FNexusSchema::Int(TEXT("Option index (0-based)")))
		.Prop(TEXT("GeneratorClass"), FNexusSchema::Str(TEXT("SetGenerator: generator class (EnvQueryGenerator_ActorsOfClass, etc.)")))
		.Prop(TEXT("TestClass"),      FNexusSchema::Str(TEXT("add/RemoveTest: test class (EnvQueryTest_Distance, etc.)")))
		.Prop(TEXT("TestIndex"),      FNexusSchema::Int(TEXT("RemoveTest: test index within Option")))
		.Build();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("EnvQuery asset path")))
		.Required(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("eqs"), TEXT("Query"), TEXT("generator"), TEXT("test"), TEXT("distance"), TEXT("ai") };
	Out.RelatedCapabilities = { TEXT("GetAssetEQS"), TEXT("CreateAssetEQS") };
	Out.WhenToUse = TEXT("Add/remove EQS Options, set Generator, add/remove Tests");
}

struct FEQSActionState
{
	UEnvQuery* EQ = nullptr;
	TArray<UEnvQueryOption*>* Options = nullptr;
	bool bDirty = false;
};

static FEQSActionState* EQSState(FNexusActionContext& Ctx)
{
	return static_cast<FEQSActionState*>(Ctx.Target);
}

static void MarkEQSDirty(FNexusActionContext& Ctx)
{
	if (FEQSActionState* S = EQSState(Ctx))
	{
		S->bDirty = true;
	}
}

static void HandleEQS_AddOption(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	(void)Op;
	FEQSActionState* S = EQSState(Ctx);
	UEnvQueryOption* NewOpt = NewObject<UEnvQueryOption>(S->EQ, NAME_None, RF_Transactional);
	S->Options->Add(NewOpt);
	Ctx.Entry->SetNumberField(TEXT("OptionIndex"), S->Options->Num() - 1);
	MarkEQSDirty(Ctx);
}

static void HandleEQS_RemoveOption(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FEQSActionState* S = EQSState(Ctx);
	int64 Idx = -1;
	Op->TryGetNumberField(TEXT("OptionIndex"), Idx);
	if (Idx < 0 || Idx >= S->Options->Num())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("OptionIndex %lld out of range [0, %d)"), Idx, S->Options->Num()));
		return;
	}
	S->Options->RemoveAt(static_cast<int32>(Idx));
	MarkEQSDirty(Ctx);
}

static void HandleEQS_SetGenerator(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FEQSActionState* S = EQSState(Ctx);
	int64 Idx = 0;
	Op->TryGetNumberField(TEXT("OptionIndex"), Idx);
	FString GenClassName;
	if (!Op->TryGetStringField(TEXT("GeneratorClass"), GenClassName))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetGenerator requires GeneratorClass"));
		return;
	}
	if (Idx < 0 || Idx >= S->Options->Num())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Invalid OptionIndex %lld"), Idx));
		return;
	}
	UClass* GenClass = FindEQSClassByName(GenClassName);
	if (!GenClass || !GenClass->IsChildOf(UEnvQueryGenerator::StaticClass()))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Generator class not found: %s"), *GenClassName));
		return;
	}
	UEnvQueryOption* Opt = (*S->Options)[static_cast<int32>(Idx)];
	Opt->Generator = NewObject<UEnvQueryGenerator>(Opt, GenClass, NAME_None, RF_Transactional);
	MarkEQSDirty(Ctx);
}

static void HandleEQS_AddTest(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FEQSActionState* S = EQSState(Ctx);
	int64 Idx = 0;
	Op->TryGetNumberField(TEXT("OptionIndex"), Idx);
	FString TestClassName;
	if (!Op->TryGetStringField(TEXT("TestClass"), TestClassName))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddTest requires TestClass"));
		return;
	}
	if (Idx < 0 || Idx >= S->Options->Num())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Invalid OptionIndex %lld"), Idx));
		return;
	}
	UClass* TestClass = FindEQSClassByName(TestClassName);
	if (!TestClass || !TestClass->IsChildOf(UEnvQueryTest::StaticClass()))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Test class not found: %s"), *TestClassName));
		return;
	}
	UEnvQueryOption* Opt = (*S->Options)[static_cast<int32>(Idx)];
	UEnvQueryTest* NewTest = NewObject<UEnvQueryTest>(Opt, TestClass, NAME_None, RF_Transactional);
	Opt->Tests.Add(NewTest);
	Ctx.Entry->SetNumberField(TEXT("TestIndex"), Opt->Tests.Num() - 1);
	MarkEQSDirty(Ctx);
}

static void HandleEQS_RemoveTest(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FEQSActionState* S = EQSState(Ctx);
	int64 OptIdx = 0, TestIdx = -1;
	Op->TryGetNumberField(TEXT("OptionIndex"), OptIdx);
	Op->TryGetNumberField(TEXT("TestIndex"),   TestIdx);
	if (OptIdx < 0 || OptIdx >= S->Options->Num())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Invalid OptionIndex %lld"), OptIdx));
		return;
	}
	UEnvQueryOption* Opt = (*S->Options)[static_cast<int32>(OptIdx)];
	if (TestIdx < 0 || TestIdx >= Opt->Tests.Num())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("TestIndex %lld out of range [0, %d)"), TestIdx, Opt->Tests.Num()));
		return;
	}
	Opt->Tests.RemoveAt(static_cast<int32>(TestIdx));
	MarkEQSDirty(Ctx);
}

bool FNexusManageAssetEQSCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UEnvQuery* EQ = FNexusAssetUtils::LoadAssetWithFallback<UEnvQuery>(AssetPath);
	if (!EQ)
	{
		OutError = FString::Printf(TEXT("EnvQuery not found: %s"), *AssetPath);
		return false;
	}
	TArray<UEnvQueryOption*>* Options = GetEnvQueryOptionsPtr(EQ);
	if (!Options)
	{
		OutError = TEXT("Unable to access EnvQuery::Options (UE version unsupported)");
		return false;
	}
	FEQSActionState* State = new FEQSActionState();
	State->EQ = EQ;
	State->Options = Options;
	OutTarget = State;
	return true;
}

void FNexusManageAssetEQSCapability::FinalizeTarget(void* Target) const
{
	FEQSActionState* State = static_cast<FEQSActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->EQ)
	{
		State->EQ->MarkPackageDirty();
	}
	delete State;
}

void FNexusManageAssetEQSCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("AddOption"),     &HandleEQS_AddOption);
	OutHandlers.Add(TEXT("RemoveOption"),  &HandleEQS_RemoveOption);
	OutHandlers.Add(TEXT("SetGenerator"),  &HandleEQS_SetGenerator);
	OutHandlers.Add(TEXT("AddTest"),       &HandleEQS_AddTest);
	OutHandlers.Add(TEXT("RemoveTest"),    &HandleEQS_RemoveTest);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetEQSCapability)

#endif // NX_UE_HAS_EQS
