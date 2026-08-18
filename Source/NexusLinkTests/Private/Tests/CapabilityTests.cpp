// Copyright byteyang. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "NexusCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Utils/NexusJsonUtils.h"

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
