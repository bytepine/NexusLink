// Copyright byteyang. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "NexusCapability.h"
#include "NexusActionCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusEditorTransaction.h"
#include "Utils/NexusAssetUtils.h"
#if WITH_EDITOR
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Engine/Blueprint.h"
#endif

// ────────────────────────────────────────────────────────────────────────────
// 测试辅助 Capability/Tool 子类（遵项目规范 §8.3 禁用 namespace）
// ────────────────────────────────────────────────────────────────────────────

/** 实现 BuildDefinition 的测试 Capability，用于验证 GetDefinition 能正确拼装元数据。 */
class FNexusTestMetadataCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override
	{
		Out.Name        = TEXT("MetaCap");
		Out.Description = TEXT("desc-text.");
		TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("type"), TEXT("object"));
		Out.InputSchema = S;
	}
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& /*Arguments*/) const override
	{
		return {};
	}
};

/** 严格 Schema 桩：仅声明 assetPath + operations，用于未知键 / 旧键 arg_invalid。 */
class FNexusTestStrictSchemaCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override
	{
		Out.Name        = TEXT("test_strict_schema");
		Out.Description = TEXT("strict InputSchema stub.");
		Out.InputSchema = FNexusSchema::Object()
			.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("单目标资产路径")))
			.Prop(TEXT("operations"), FNexusSchema::ArrOfObj(TEXT("操作列表")))
			.Required({ TEXT("assetPath") })
			.Build();
	}
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& /*Arguments*/) const override
	{
		FCapabilityResult R;
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("ok"), TEXT("1"));
		R.Entries.Add(MakeShared<FJsonValueObject>(Entry));
		return R;
	}
};

/** 未重写 Execute 的最小工具，用于验证默认兜底报错。 */
class FNexusTestBareMinimumTool : public FNexusMcpTool
{
protected:
	virtual void BuildDefinition(FNexusMcpToolDefinition& Out) const override
	{
		Out.Name        = TEXT("test_bare_minimum");
		Out.Description = TEXT("bare.");
	}
};

/** ActionCapability 流程桩：不 REGISTER，避免污染运行时表。 */
class FNexusTestActionCapability : public FNexusActionCapability
{
public:
	bool bFailPrepare = false;
	mutable int32 HandlerRuns = 0;
	mutable int32 FinalizeCount = 0;

	FCapabilityResult CallExecute(const TSharedPtr<FJsonObject>& Args) const
	{
		return Execute(Args);
	}

protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override
	{
		Out.Name        = TEXT("test_action_flow");
		Out.Description = TEXT("ActionCapability flow stub.");
		Out.InputSchema = FNexusSchema::Object()
			.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("path")))
			.Prop(TEXT("operations"), FNexusSchema::ArrOfObj(TEXT("ops")))
			.Build();
	}

	virtual void RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const override
	{
		OutHandlers.Add(TEXT("ping"), [this](const TSharedPtr<FJsonObject>&, FNexusActionContext& Ctx)
		{
			++HandlerRuns;
			Ctx.Entry->SetBoolField(TEXT("ok"), true);
		});
	}

	virtual bool PrepareTarget(
		const TSharedPtr<FJsonObject>& /*Args*/,
		TSharedPtr<FJsonObject>& /*Entry*/,
		void*& OutTarget,
		FString& OutError) const override
	{
		if (bFailPrepare)
		{
			OutError = TEXT("prepare failed");
			return false;
		}
		OutTarget = reinterpret_cast<void*>(static_cast<UPTRINT>(1));
		return true;
	}

	virtual void FinalizeTarget(void* /*Target*/) const override
	{
		++FinalizeCount;
	}
};

static TSharedPtr<FJsonObject> MakeTestActionArgs(const TCHAR* Action /* nullptr = omit field */)
{
	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("assetPath"), TEXT("/Game/X"));
	TSharedPtr<FJsonObject> Op = MakeShared<FJsonObject>();
	if (Action)
	{
		Op->SetStringField(TEXT("action"), Action);
	}
	TArray<TSharedPtr<FJsonValue>> Ops;
	Ops.Add(MakeShared<FJsonValueObject>(Op));
	Args->SetArrayField(TEXT("operations"), Ops);
	return Args;
}

static FString FirstEntryError(const FCapabilityResult& R)
{
	if (R.Entries.Num() < 1 || !R.Entries[0].IsValid())
	{
		return FString();
	}
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (!R.Entries[0]->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
	{
		return FString();
	}
	FString Err;
	(*Obj)->TryGetStringField(TEXT("error"), Err);
	return Err;
}

// ────────────────────────────────────────────────────────────────────────────
// 0. GetDefinition：拼装 Name / Description / InputSchema 的元数据（实例级 lazy 缓存）
// ────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkCapabilityGetDefinitionTest,
	"NexusLink.Capability.GetDefinition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkCapabilityGetDefinitionTest::RunTest(const FString& Parameters)
{
	FNexusTestMetadataCapability Cap;
	const FNexusCapabilityDefinition& Def1 = Cap.GetDefinition();
	TestEqual(TEXT("Definition.Name"),        Def1.Name,        FString(TEXT("MetaCap")));
	TestEqual(TEXT("Definition.Description"), Def1.Description, FString(TEXT("desc-text.")));
	TestTrue (TEXT("Definition.InputSchema present"), Def1.InputSchema.IsValid());
	if (Def1.InputSchema.IsValid())
	{
		TestEqual(TEXT("schema.type"),
			Def1.InputSchema->GetStringField(TEXT("type")), FString(TEXT("object")));
	}

	// 实例级 lazy 缓存：第二次调用返回同一个对象（地址不变）
	const FNexusCapabilityDefinition& Def2 = Cap.GetDefinition();
	TestTrue(TEXT("Cached: same ptr"), &Def1 == &Def2);

	return true;
}

// ────────────────────────────────────────────────────────────────────────────
// 1. 默认 Execute 兜底：未重写 Execute → 正确报错
// ────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkToolDefaultExecuteTest,
	"NexusLink.Capability.ToolDefaultExecute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkToolDefaultExecuteTest::RunTest(const FString& Parameters)
{
	FNexusTestBareMinimumTool Tool;
	const FNexusMcpToolResult R = Tool.Execute(MakeShared<FJsonObject>());
	TestTrue(TEXT("bare tool → bIsError"), R.bIsError);
	TestFalse(TEXT("error text non-empty"), R.ErrorText.IsEmpty());
	return true;
}

// ────────────────────────────────────────────────────────────────────────────
// 2. Run 严格校验：未知键 / 旧键 → arg_invalid；合法键通过
// ────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkCapabilityStrictSchemaArgInvalidTest,
	"NexusLink.Capability.StrictSchemaArgInvalid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkCapabilityStrictSchemaArgInvalidTest::RunTest(const FString& Parameters)
{
	FNexusTestStrictSchemaCapability Cap;

	// 合法入参
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("assetPath"), TEXT("/Game/X"));
		TArray<TSharedPtr<FJsonValue>> Ops;
		TSharedPtr<FJsonObject> Op = MakeShared<FJsonObject>();
		Op->SetStringField(TEXT("action"), TEXT("noop"));
		Ops.Add(MakeShared<FJsonValueObject>(Op));
		Args->SetArrayField(TEXT("operations"), Ops);
		const FCapabilityResult Ok = Cap.Run(Args);
		TestTrue(TEXT("合法参数 FatalError 空"), Ok.FatalError.IsEmpty());
		TestFalse(TEXT("合法参数非 arg_invalid"), Ok.bIsArgInvalid);
	}

	// 未知键
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("assetPath"), TEXT("/Game/X"));
		Args->SetStringField(TEXT("unknownKey"), TEXT("x"));
		const FCapabilityResult R = Cap.Run(Args);
		TestTrue(TEXT("未知键 → arg_invalid"), R.bIsArgInvalid);
		TestTrue(TEXT("未知键 FatalError 非空"), !R.FatalError.IsEmpty());
		TestTrue(TEXT("未知键文案含 unknownKey"), R.FatalError.Contains(TEXT("unknownKey")));
	}

	// 旧键（Breaking：不再静默映射）
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("assetPath"), TEXT("/Game/X"));
		Args->SetStringField(TEXT("propertyPath"), TEXT("Foo")); // get 侧旧单数
		const FCapabilityResult R = Cap.Run(Args);
		TestTrue(TEXT("旧键 propertyPath → arg_invalid"), R.bIsArgInvalid);
		TestTrue(TEXT("旧键文案含 propertyPath"), R.FatalError.Contains(TEXT("propertyPath")));
	}
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("assetPath"), TEXT("/Game/X"));
		Args->SetStringField(TEXT("newPath"), TEXT("/Game/Y"));
		const FCapabilityResult R = Cap.Run(Args);
		TestTrue(TEXT("旧键 newPath → arg_invalid"), R.bIsArgInvalid);
	}
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("assetPath"), TEXT("/Game/X"));
		Args->SetArrayField(TEXT("ops"), {}); // 旧 operations 别名
		const FCapabilityResult R = Cap.Run(Args);
		TestTrue(TEXT("旧键 ops → arg_invalid"), R.bIsArgInvalid);
	}

	return true;
}

// ────────────────────────────────────────────────────────────────────────────
// 3. ExtractOperations：仅认 operations[]，不认 ops / 顶层 action
// ────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkExtractOperationsOnlyNewFieldTest,
	"NexusLink.Capability.ExtractOperationsOnlyOperations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkExtractOperationsOnlyNewFieldTest::RunTest(const FString& Parameters)
{
	// operations[] 命中
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Ops;
		TSharedPtr<FJsonObject> Op = MakeShared<FJsonObject>();
		Op->SetStringField(TEXT("action"), TEXT("a"));
		Ops.Add(MakeShared<FJsonValueObject>(Op));
		Args->SetArrayField(TEXT("operations"), Ops);
		const TArray<TSharedPtr<FJsonValue>> Got = FNexusJsonUtils::ExtractOperations(Args);
		TestEqual(TEXT("operations → 1"), Got.Num(), 1);
	}

	// ops 不再回退
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Ops;
		TSharedPtr<FJsonObject> Op = MakeShared<FJsonObject>();
		Op->SetStringField(TEXT("action"), TEXT("a"));
		Ops.Add(MakeShared<FJsonValueObject>(Op));
		Args->SetArrayField(TEXT("ops"), Ops);
		const TArray<TSharedPtr<FJsonValue>> Got = FNexusJsonUtils::ExtractOperations(Args);
		TestEqual(TEXT("ops → 空"), Got.Num(), 0);
	}

	// 顶层 action 不再合成
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("action"), TEXT("a"));
		Args->SetStringField(TEXT("assetPath"), TEXT("/Game/X"));
		const TArray<TSharedPtr<FJsonValue>> Got = FNexusJsonUtils::ExtractOperations(Args);
		TestEqual(TEXT("顶层 action → 空"), Got.Num(), 0);
	}

	return true;
}

// ────────────────────────────────────────────────────────────────────────────
// 4. manage 收尾 mixin：saveToDisk 全量注入；compile 仅 BP/ABP/WBP
// ────────────────────────────────────────────────────────────────────────────

/** manage 桩：名 manage_asset_dummy，应注入 saveToDisk、不注入 compile。 */
class FNexusTestManageSaveMixinCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override
	{
		Out.Name        = TEXT("manage_asset_dummy");
		Out.Description = TEXT("test manage save mixin.");
		Out.InputSchema = FNexusSchema::Object()
			.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("路径")))
			.Required({ TEXT("assetPath") })
			.Build();
	}
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& /*Arguments*/) const override
	{
		FCapabilityResult R;
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("ok"), TEXT("1"));
		R.Entries.Add(MakeShared<FJsonValueObject>(Entry));
		return R;
	}
};

/** manage 桩：名 manage_asset_blueprint，应同时注入 compile。 */
class FNexusTestManageCompileMixinCapability : public FNexusCapability
{
protected:
	virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override
	{
		Out.Name        = TEXT("manage_asset_blueprint");
		Out.Description = TEXT("test manage compile mixin.");
		Out.InputSchema = FNexusSchema::Object()
			.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("路径")))
			.Required({ TEXT("assetPath") })
			.Build();
	}
	virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& /*Arguments*/) const override
	{
		FCapabilityResult R;
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("ok"), TEXT("1"));
		R.Entries.Add(MakeShared<FJsonValueObject>(Entry));
		return R;
	}
};

static bool SchemaHasTopProp(const FNexusCapabilityDefinition& Def, const TCHAR* Name)
{
	if (!Def.InputSchema.IsValid())
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Props = nullptr;
	if (!Def.InputSchema->TryGetObjectField(TEXT("properties"), Props) || !Props || !Props->IsValid())
	{
		return false;
	}
	return (*Props)->HasField(Name);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkCapabilityManageFinalizeMixinTest,
	"NexusLink.Capability.ManageFinalizeMixin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkCapabilityManageFinalizeMixinTest::RunTest(const FString& Parameters)
{
	FNexusTestManageSaveMixinCapability SaveCap;
	const FNexusCapabilityDefinition& SaveDef = SaveCap.GetDefinition();
	TestTrue(TEXT("dummy 注入 saveToDisk"), SchemaHasTopProp(SaveDef, TEXT("saveToDisk")));
	TestFalse(TEXT("dummy 不注入 compile"), SchemaHasTopProp(SaveDef, TEXT("compile")));

	FNexusTestManageCompileMixinCapability CompileCap;
	const FNexusCapabilityDefinition& CompileDef = CompileCap.GetDefinition();
	TestTrue(TEXT("BP manage 注入 saveToDisk"), SchemaHasTopProp(CompileDef, TEXT("saveToDisk")));
	TestTrue(TEXT("BP manage 注入 compile"), SchemaHasTopProp(CompileDef, TEXT("compile")));

	// 非 BP manage 传 compile → arg_invalid
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("assetPath"), TEXT("/Game/DoesNotExist"));
		Args->SetBoolField(TEXT("compile"), true);
		const FCapabilityResult R = SaveCap.Run(Args);
		TestTrue(TEXT("dummy+compile → arg_invalid"), R.bIsArgInvalid);
	}

	// saveToDisk 对不存在的包：Execute 成功，TopFields 记 saveError
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("assetPath"), TEXT("/Game/DoesNotExist_NexusMixin"));
		Args->SetBoolField(TEXT("saveToDisk"), true);
		const FCapabilityResult R = SaveCap.Run(Args);
		TestTrue(TEXT("dummy+save 非 Fatal"), R.FatalError.IsEmpty());
		TestFalse(TEXT("dummy+save 非 arg_invalid"), R.bIsArgInvalid);
		TestTrue(TEXT("dummy+save 有 TopFields"), R.TopFields.IsValid());
		if (R.TopFields.IsValid())
		{
			bool bSaved = true;
			R.TopFields->TryGetBoolField(TEXT("saved"), bSaved);
			TestFalse(TEXT("dummy+save saved=false"), bSaved);
			TestTrue(TEXT("dummy+save 含 saveError"), R.TopFields->HasField(TEXT("saveError")));
		}
	}

	// 注册表：生产 cap 同样注入
	if (const FCapRecord* Dt = FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("manage_asset_data_table")))
	{
		TestTrue(TEXT("DT manage 有 saveToDisk"), SchemaHasTopProp(Dt->Def, TEXT("saveToDisk")));
		TestFalse(TEXT("DT manage 无 compile"), SchemaHasTopProp(Dt->Def, TEXT("compile")));
	}
	else
	{
		AddError(TEXT("未注册 manage_asset_data_table"));
	}

	if (const FCapRecord* Bp = FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("manage_asset_blueprint")))
	{
		TestTrue(TEXT("生产 BP manage 有 saveToDisk"), SchemaHasTopProp(Bp->Def, TEXT("saveToDisk")));
		TestTrue(TEXT("生产 BP manage 有 compile"), SchemaHasTopProp(Bp->Def, TEXT("compile")));
	}
	else
	{
		AddError(TEXT("未注册 manage_asset_blueprint"));
	}

	if (const FCapRecord* GetBp = FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("get_asset_blueprint")))
	{
		TestFalse(TEXT("get 不注入 saveToDisk"), SchemaHasTopProp(GetBp->Def, TEXT("saveToDisk")));
		TestFalse(TEXT("get 不注入 compile"), SchemaHasTopProp(GetBp->Def, TEXT("compile")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkCapabilityEditorTransactionTest,
	"NexusLink.Capability.EditorTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkCapabilityEditorTransactionTest::RunTest(const FString& Parameters)
{
	const TArray<FString> WriteEditor = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	const TArray<FString> ReadEditor = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	const TArray<FString> WriteRuntime = { FNexusMcpTags::Write, FNexusMcpTags::Runtime };

	TestTrue(TEXT("manage 写路径应包事务"),
		FNexusEditorTransaction::ShouldTransact(TEXT("manage_asset_blueprint"), WriteEditor));
	TestFalse(TEXT("get 只读不包事务"),
		FNexusEditorTransaction::ShouldTransact(TEXT("get_asset_blueprint"), ReadEditor));
	TestFalse(TEXT("save_asset 不包事务"),
		FNexusEditorTransaction::ShouldTransact(TEXT("save_asset"), WriteEditor));
	TestFalse(TEXT("compile_blueprint 不包事务"),
		FNexusEditorTransaction::ShouldTransact(TEXT("compile_blueprint"), WriteEditor));
	TestFalse(TEXT("runtime 不包事务"),
		FNexusEditorTransaction::ShouldTransact(TEXT("list_runtime_actors"), WriteRuntime));
	TestFalse(TEXT("lua 不包事务"),
		FNexusEditorTransaction::ShouldTransact(TEXT("eval_runtime_lua"), WriteEditor));
	TestFalse(TEXT("control_pie 不包事务"),
		FNexusEditorTransaction::ShouldTransact(TEXT("control_pie"), WriteEditor));
	TestFalse(TEXT("control_movie_pipeline 不包事务"),
		FNexusEditorTransaction::ShouldTransact(TEXT("control_movie_pipeline"), WriteEditor));

#if WITH_EDITOR
	if (GEditor)
	{
		TUniquePtr<FScopedTransaction> Tx = FNexusEditorTransaction::Begin(TEXT("EditorTransactionTest"));
		TestTrue(TEXT("Begin 后事务进行中"), FNexusEditorTransaction::IsTransactionActive());
		if (Tx.IsValid())
		{
			Tx->Cancel();
			Tx.Reset();
		}
		TestFalse(TEXT("Cancel 后事务结束"), FNexusEditorTransaction::IsTransactionActive());
	}
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkCapabilityEditorTransactionUndoTest,
	"NexusLink.Capability.EditorTransactionUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkCapabilityEditorTransactionUndoTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
	if (!GEditor)
	{
		return true;
	}

	const FString Path = TEXT("/Game/NexusLinkAuto/BP_UndoTxn");
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Path);
	if (!BP)
	{
		const FNexusAssetUtils::FAssetCreateOutcome Created = FNexusAssetUtils::CreateBlueprintAsset(
			Path, TEXT("Actor"), UObject::StaticClass(), nullptr, nullptr, false);
		if (!Created.Ok())
		{
			AddError(Created.Error);
			return false;
		}
		BP = Cast<UBlueprint>(Created.Asset);
	}
	if (!BP)
	{
		AddError(TEXT("无法创建 Undo 测试蓝图"));
		return false;
	}

	const FCapRecord* Rec = FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("manage_asset_blueprint"));
	if (!Rec)
	{
		AddError(TEXT("未注册 manage_asset_blueprint"));
		return false;
	}

	const FString VarName = FString::Printf(TEXT("UndoProbe_%d"),
		static_cast<int32>(FDateTime::UtcNow().ToUnixTimestamp()));
	const int32 Before = BP->NewVariables.Num();

	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("assetPath"), Path);
	TSharedPtr<FJsonObject> Op = MakeShared<FJsonObject>();
	Op->SetStringField(TEXT("action"), TEXT("add_variable"));
	Op->SetStringField(TEXT("variableName"), VarName);
	Op->SetStringField(TEXT("variableType"), TEXT("float"));
	TArray<TSharedPtr<FJsonValue>> Ops;
	Ops.Add(MakeShared<FJsonValueObject>(Op));
	Args->SetArrayField(TEXT("operations"), Ops);

	const FCapabilityResult R = Rec->Instance->Run(Args);
	TestTrue(TEXT("add_variable 无 Fatal"), R.FatalError.IsEmpty());
	TestTrue(TEXT("Run 后变量数 +1"), BP->NewVariables.Num() == Before + 1);

	TestTrue(TEXT("UndoTransaction"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo 后变量数恢复"), BP->NewVariables.Num(), Before);
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkCapabilitySettingsGroupPathTest,
	"NexusLink.Capability.SettingsGroupPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkCapabilitySettingsGroupPathTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("unix caps path"),
		FNexusCapabilityRegistry::MakeSettingsGroupPath(
			TEXT("/repo/Source/NexusLink/Private/Capabilities/Asset/Blueprint/NexusGetAssetBlueprintCapability.cpp")),
		FString(TEXT("Asset/Blueprint")));
	TestEqual(TEXT("windows caps path"),
		FNexusCapabilityRegistry::MakeSettingsGroupPath(
			TEXT("E:\\repo\\Source\\NexusLink\\Private\\Capabilities\\Runtime\\Actor\\NexusListRuntimeActorsCapability.cpp")),
		FString(TEXT("Runtime/Actor")));
	TestEqual(TEXT("asset root file"),
		FNexusCapabilityRegistry::MakeSettingsGroupPath(
			TEXT("/repo/Private/Capabilities/Asset/NexusSearchAssetCapability.cpp")),
		FString(TEXT("Asset")));
	TestTrue(TEXT("tools path unreachable"),
		FNexusCapabilityRegistry::MakeSettingsGroupPath(
			TEXT("/repo/Private/Tools/NexusMcpToolCallCapability.cpp")).IsEmpty());
	TestTrue(TEXT("null file"), FNexusCapabilityRegistry::MakeSettingsGroupPath(nullptr).IsEmpty());
	TestTrue(TEXT("unrelated path"),
		FNexusCapabilityRegistry::MakeSettingsGroupPath(TEXT("/tmp/Foo.cpp")).IsEmpty());

	if (const FCapRecord* Bp = FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("get_asset_blueprint")))
	{
		TestEqual(TEXT("registered BP group"), Bp->SourceRelDir, FString(TEXT("Asset/Blueprint")));
	}
	else
	{
		AddError(TEXT("未注册 get_asset_blueprint"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkArgsFacadeTest,
	"NexusLink.Utils.NexusArgs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkArgsFacadeTest::RunTest(const FString& Parameters)
{
	{
		const TSharedPtr<FJsonObject> NullObj;
		const FNexusArgs Empty(NullObj);
		const TArray<FString> Ab = { TEXT("a"), TEXT("b") };
		TestTrue(TEXT("null Str default"), Empty.Str(TEXT("k"), TEXT("d")) == TEXT("d"));
		TestEqual(TEXT("null Num default"), Empty.Num(TEXT("k"), 3.0), 3.0);
		TestEqual(TEXT("null Bool default"), Empty.Bool(TEXT("k"), true), true);
		TestEqual(TEXT("null StrArr empty"), Empty.StrArr(TEXT("k")).Num(), 0);
		TestEqual(TEXT("null EnumInt default"), Empty.EnumInt(TEXT("k"), Ab, 7), 7);
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("name"), TEXT("foo"));
	Obj->SetNumberField(TEXT("zero"), 0.0);
	Obj->SetBoolField(TEXT("flag"), true);
	TArray<TSharedPtr<FJsonValue>> Arr;
	Arr.Add(MakeShared<FJsonValueString>(TEXT("x")));
	Obj->SetArrayField(TEXT("tags"), Arr);
	Obj->SetStringField(TEXT("kind"), TEXT("B"));
	const FNexusArgs A(Obj);

	TestEqual(TEXT("Str hit"), A.Str(TEXT("name")), FString(TEXT("foo")));
	TestTrue(TEXT("Str miss default"), A.Str(TEXT("missing"), TEXT("d")) == TEXT("d"));
	TestEqual(TEXT("Num 0"), A.Num(TEXT("zero")), 0.0);
	TestEqual(TEXT("Bool true"), A.Bool(TEXT("flag")), true);
	TestEqual(TEXT("Bool miss default"), A.Bool(TEXT("nope")), false);
	TestEqual(TEXT("StrArr n"), A.StrArr(TEXT("tags")).Num(), 1);
	const TArray<FString> KindAB = { TEXT("a"), TEXT("b") };
	const TArray<FString> KindXY = { TEXT("x"), TEXT("y") };
	TestEqual(TEXT("EnumInt case"), A.EnumInt(TEXT("kind"), KindAB, -1), 1);
	TestEqual(TEXT("EnumInt miss"), A.EnumInt(TEXT("kind"), KindXY, 4), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkActionCapabilityFlowTest,
	"NexusLink.Capability.ActionCapabilityFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkActionCapabilityFlowTest::RunTest(const FString& Parameters)
{
	{
		FNexusTestActionCapability Cap;
		const FCapabilityResult R = Cap.CallExecute(MakeShared<FJsonObject>());
		TestEqual(TEXT("empty ops fatal"), R.FatalError, FString(TEXT("Missing or empty operations")));
		TestEqual(TEXT("empty ops no finalize"), Cap.FinalizeCount, 0);
		TestEqual(TEXT("empty ops no handler"), Cap.HandlerRuns, 0);
	}

	{
		FNexusTestActionCapability Cap;
		Cap.bFailPrepare = true;
		const FCapabilityResult R = Cap.CallExecute(MakeTestActionArgs(TEXT("ping")));
		TestEqual(TEXT("prepare fail fatal"), R.FatalError, FString(TEXT("prepare failed")));
		TestEqual(TEXT("prepare fail no handler"), Cap.HandlerRuns, 0);
		TestEqual(TEXT("prepare fail no finalize"), Cap.FinalizeCount, 0);
	}

	{
		FNexusTestActionCapability Cap;
		const FCapabilityResult R = Cap.CallExecute(MakeTestActionArgs(TEXT("nope")));
		TestTrue(TEXT("unknown no fatal"), R.FatalError.IsEmpty());
		TestEqual(TEXT("unknown error"), FirstEntryError(R), FString(TEXT("Unknown action: nope")));
		TestEqual(TEXT("unknown finalize"), Cap.FinalizeCount, 1);
		TestEqual(TEXT("unknown no handler"), Cap.HandlerRuns, 0);
	}

	{
		FNexusTestActionCapability Cap;
		const FCapabilityResult R = Cap.CallExecute(MakeTestActionArgs(nullptr));
		TestTrue(TEXT("missing action no fatal"), R.FatalError.IsEmpty());
		TestEqual(TEXT("missing action error"), FirstEntryError(R), FString(TEXT("Missing action")));
		TestEqual(TEXT("missing action finalize"), Cap.FinalizeCount, 1);
	}

	{
		FNexusTestActionCapability Cap;
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("assetPath"), TEXT("/Game/X"));
		TArray<TSharedPtr<FJsonValue>> Ops;
		TSharedPtr<FJsonObject> Bad = MakeShared<FJsonObject>();
		Bad->SetStringField(TEXT("action"), TEXT("nope"));
		TSharedPtr<FJsonObject> Good = MakeShared<FJsonObject>();
		Good->SetStringField(TEXT("action"), TEXT("ping"));
		Ops.Add(MakeShared<FJsonValueObject>(Bad));
		Ops.Add(MakeShared<FJsonValueObject>(Good));
		Args->SetArrayField(TEXT("operations"), Ops);
		const FCapabilityResult R = Cap.CallExecute(Args);
		TestTrue(TEXT("continue no fatal"), R.FatalError.IsEmpty());
		TestEqual(TEXT("continue entries"), R.Entries.Num(), 2);
		TestEqual(TEXT("continue handler once"), Cap.HandlerRuns, 1);
		TestEqual(TEXT("continue finalize"), Cap.FinalizeCount, 1);
	}

	{
		FNexusTestActionCapability Cap;
		const FCapabilityResult R = Cap.CallExecute(MakeTestActionArgs(TEXT("ping")));
		TestTrue(TEXT("ping no fatal"), R.FatalError.IsEmpty());
		TestEqual(TEXT("ping handler"), Cap.HandlerRuns, 1);
		TestEqual(TEXT("ping finalize"), Cap.FinalizeCount, 1);
		TestEqual(TEXT("ping entries"), R.Entries.Num(), 1);
		if (R.Entries.Num() > 0 && R.Entries[0].IsValid())
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (R.Entries[0]->TryGetObject(Obj) && Obj && (*Obj).IsValid())
			{
				TestTrue(TEXT("ping ok"), (*Obj)->GetBoolField(TEXT("ok")));
			}
		}
	}

	return true;
}
