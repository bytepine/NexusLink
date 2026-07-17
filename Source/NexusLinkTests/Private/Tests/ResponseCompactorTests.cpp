// Copyright byteyang. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Utils/NexusResponseCompactorUtils.h"

// ═══════════════════════════════════════════════════════════════════════════
// 响应默认值压缩器单元测试：覆盖 FNexusResponseCompactorUtils 的类型化比较 /
// 三阈值 / 强制默认 / 自动扫描 / 递归 Pass 等全部核心分支。
//
// 这些用例不依赖运行中的 UE 编辑器或 MCP 连接，能稳定触发 Python 端到端
// 测试覆盖不到的边界（Number / Bool / Null 字段、空数组、递归深度、同级
// `<K>_defaults` 去重等），也能在移除手动压缩声明后持续守护算法不变式。
// ═══════════════════════════════════════════════════════════════════════════

// ─── JSON 构造小工具 ──────────────────────────────────────────────
static FORCEINLINE TSharedPtr<FJsonValue> JStr(const TCHAR* S)  { return MakeShared<FJsonValueString>(FString(S)); }
static FORCEINLINE TSharedPtr<FJsonValue> JNum(double N)        { return MakeShared<FJsonValueNumber>(N); }
static FORCEINLINE TSharedPtr<FJsonValue> JBool(bool B)         { return MakeShared<FJsonValueBoolean>(B); }
static FORCEINLINE TSharedPtr<FJsonValue> JNull()               { return MakeShared<FJsonValueNull>(); }

struct FNamedField { const TCHAR* Name; TSharedPtr<FJsonValue> Value; };

/** 用 "字段名 / 值" 对列表构造一个作为 FJsonValue 包装的 object。 */
static TSharedPtr<FJsonValue> JObj(std::initializer_list<FNamedField> Fields)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	for (const FNamedField& F : Fields)
	{
		O->SetField(FString(F.Name), F.Value);
	}
	return MakeShared<FJsonValueObject>(O);
}

/** 取第 i 个条目的 FJsonObject（断言式，用于测试中提取字段）。 */
static TSharedPtr<FJsonObject> EntryObj(const TArray<TSharedPtr<FJsonValue>>& Items, int32 Idx)
{
	if (!Items.IsValidIndex(Idx) || !Items[Idx].IsValid() || Items[Idx]->Type != EJson::Object)
	{
		return TSharedPtr<FJsonObject>();
	}
	return Items[Idx]->AsObject();
}

// ─────────────────────────────────────────────────────────────────────
// 1. 类型化比较覆盖：四种标量类型各自都能被正确压缩
//    （对应 AreScalarJsonValuesEqual 优化：替代 FString::Printf key 比较）
// ─────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkResponseCompactorScalarsTest,
	"NexusLink.Utils.ResponseCompactor.ScalarTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkResponseCompactorScalarsTest::RunTest(const FString& Parameters)
{
	// ─── 字符串字段：5/5 等值 → 应抽取 ───
	{
		TArray<TSharedPtr<FJsonValue>> Items = {
			JObj({ {TEXT("name"), JStr(TEXT("A"))}, {TEXT("category"), JStr(TEXT("Weapon"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("B"))}, {TEXT("category"), JStr(TEXT("Weapon"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("C"))}, {TEXT("category"), JStr(TEXT("Weapon"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("D"))}, {TEXT("category"), JStr(TEXT("Weapon"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("E"))}, {TEXT("category"), JStr(TEXT("Weapon"))} }),
		};
		FNexusResponseCompactorUtils C;
		C.AddCandidate(TEXT("category"));
		C.CompactArray(Items);

		TestTrue(TEXT("string: defaults produced"), C.HasDefaults());
		TestEqual(TEXT("string: default value == 'Weapon'"),
			C.GetDefaults()->GetStringField(TEXT("category")), FString(TEXT("Weapon")));
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			TestFalse(TEXT("string: category stripped from entry"),
				EntryObj(Items, i)->HasField(TEXT("category")));
		}
	}

	// ─── 数字字段：多数为 0、1 条为 100 → 应抽取 0（阈值 70%） ───
	{
		TArray<TSharedPtr<FJsonValue>> Items = {
			JObj({ {TEXT("id"), JStr(TEXT("a"))}, {TEXT("childCount"), JNum(0)} }),
			JObj({ {TEXT("id"), JStr(TEXT("b"))}, {TEXT("childCount"), JNum(0)} }),
			JObj({ {TEXT("id"), JStr(TEXT("c"))}, {TEXT("childCount"), JNum(0)} }),
			JObj({ {TEXT("id"), JStr(TEXT("d"))}, {TEXT("childCount"), JNum(0)} }),
			JObj({ {TEXT("id"), JStr(TEXT("e"))}, {TEXT("childCount"), JNum(100)} }),
		};
		FNexusResponseCompactorUtils C;
		C.AddCandidate(TEXT("childCount"));
		C.CompactArray(Items);

		TestTrue(TEXT("number: defaults produced"), C.HasDefaults());
		TestEqual(TEXT("number: default value == 0"),
			C.GetDefaults()->GetNumberField(TEXT("childCount")), 0.0);
		// 等于 0 的四条被剥离；非 0 的那条保留
		TestFalse(TEXT("number: zero entry stripped"),
			EntryObj(Items, 0)->HasField(TEXT("childCount")));
		TestTrue(TEXT("number: non-default entry retains field"),
			EntryObj(Items, 4)->HasField(TEXT("childCount")));
		TestEqual(TEXT("number: retained value preserved"),
			EntryObj(Items, 4)->GetNumberField(TEXT("childCount")), 100.0);
	}

	// ─── 布尔字段：全 true → 应抽取 ───
	{
		TArray<TSharedPtr<FJsonValue>> Items = {
			JObj({ {TEXT("name"), JStr(TEXT("A"))}, {TEXT("inherited"), JBool(true)} }),
			JObj({ {TEXT("name"), JStr(TEXT("B"))}, {TEXT("inherited"), JBool(true)} }),
			JObj({ {TEXT("name"), JStr(TEXT("C"))}, {TEXT("inherited"), JBool(true)} }),
			JObj({ {TEXT("name"), JStr(TEXT("D"))}, {TEXT("inherited"), JBool(true)} }),
		};
		FNexusResponseCompactorUtils C;
		C.AddCandidate(TEXT("inherited"));
		C.CompactArray(Items);

		TestTrue(TEXT("bool: defaults produced"), C.HasDefaults());
		TestEqual(TEXT("bool: default value == true"),
			C.GetDefaults()->GetBoolField(TEXT("inherited")), true);
	}

	// ─── Null 字段：全 null → 应抽取 ───
	{
		TArray<TSharedPtr<FJsonValue>> Items = {
			JObj({ {TEXT("name"), JStr(TEXT("A"))}, {TEXT("owner"), JNull()} }),
			JObj({ {TEXT("name"), JStr(TEXT("B"))}, {TEXT("owner"), JNull()} }),
			JObj({ {TEXT("name"), JStr(TEXT("C"))}, {TEXT("owner"), JNull()} }),
			JObj({ {TEXT("name"), JStr(TEXT("D"))}, {TEXT("owner"), JNull()} }),
		};
		FNexusResponseCompactorUtils C;
		C.AddCandidate(TEXT("owner"));
		C.CompactArray(Items);

		TestTrue(TEXT("null: defaults produced"), C.HasDefaults());
		TSharedPtr<FJsonValue> Def = C.GetDefaults()->TryGetField(TEXT("owner"));
		TestTrue(TEXT("null: default entry is EJson::Null"),
			Def.IsValid() && Def->Type == EJson::Null);
	}

	// ─── 混合类型（string vs number）：AreScalarJsonValuesEqual 必须把不同类型判为不等 ───
	{
		TArray<TSharedPtr<FJsonValue>> Items = {
			JObj({ {TEXT("name"), JStr(TEXT("A"))}, {TEXT("value"), JStr(TEXT("1"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("B"))}, {TEXT("value"), JNum(1)} }),
			JObj({ {TEXT("name"), JStr(TEXT("C"))}, {TEXT("value"), JStr(TEXT("1"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("D"))}, {TEXT("value"), JNum(1)} }),
			JObj({ {TEXT("name"), JStr(TEXT("E"))}, {TEXT("value"), JStr(TEXT("1"))} }),
		};
		FNexusResponseCompactorUtils C;
		C.AddCandidate(TEXT("value"));
		C.CompactArray(Items);

		// 5 个条目：string"1" × 3 + number 1 × 2 → top=string"1" count=3
		// 占比 3/5 = 60% < 70%，不达 MinMatchRatio，不抽取
		TestFalse(TEXT("mixed types: String \"1\" and Number 1 should NOT merge into one bucket"),
			C.HasDefaults());
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────
// 2. 三阈值：MinCount / MinMatchRatio / MinNetSaveBytes 都能独立触发"不压缩"
// ─────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkResponseCompactorThresholdsTest,
	"NexusLink.Utils.ResponseCompactor.Thresholds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkResponseCompactorThresholdsTest::RunTest(const FString& Parameters)
{
	// ─── MinCount：N=1 < 2 → 不统计抽取，快速 bail ───
	{
		TArray<TSharedPtr<FJsonValue>> Items = {
			JObj({ {TEXT("name"), JStr(TEXT("A"))}, {TEXT("category"), JStr(TEXT("Weapon"))} }),
		};
		FNexusResponseCompactorUtils C;
		C.AddCandidate(TEXT("category"));
		C.CompactArray(Items);

		TestFalse(TEXT("MinCount: N=1 skipped entirely"), C.HasDefaults());
		TestTrue(TEXT("MinCount: fields kept intact"),
			EntryObj(Items, 0)->HasField(TEXT("category")));
	}

	// ─── MinCount：N=2 且同值 → 可抽取 ───
	{
		TArray<TSharedPtr<FJsonValue>> Items = {
			JObj({ {TEXT("name"), JStr(TEXT("A"))}, {TEXT("category"), JStr(TEXT("Weapon"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("B"))}, {TEXT("category"), JStr(TEXT("Weapon"))} }),
		};
		FNexusResponseCompactorUtils C;
		C.AddCandidate(TEXT("category"));
		C.CompactArray(Items);

		TestTrue(TEXT("MinCount: N=2 same value compresses"), C.HasDefaults());
		TestFalse(TEXT("MinCount: N=2 entry omits default category"),
			EntryObj(Items, 0)->HasField(TEXT("category")));
	}

	// ─── MinMatchRatio：6 条里只 3 条等值（50% < 70%）→ 不抽取 ───
	{
		TArray<TSharedPtr<FJsonValue>> Items = {
			JObj({ {TEXT("name"), JStr(TEXT("A"))}, {TEXT("tier"), JStr(TEXT("Common"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("B"))}, {TEXT("tier"), JStr(TEXT("Common"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("C"))}, {TEXT("tier"), JStr(TEXT("Common"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("D"))}, {TEXT("tier"), JStr(TEXT("Rare"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("E"))}, {TEXT("tier"), JStr(TEXT("Epic"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("F"))}, {TEXT("tier"), JStr(TEXT("Legendary"))} }),
		};
		FNexusResponseCompactorUtils C;
		C.AddCandidate(TEXT("tier"));
		C.CompactArray(Items);

		TestFalse(TEXT("MinMatchRatio: 50% < 70% bail out"), C.HasDefaults());
	}

	// ─── MinNetSaveBytes：字段名 + 值太短导致节省字节 < 阈值 → 不抽取 ───
	// "t":1 大概每条 5 字节，N=4 条匹配时 SaveBytes ≈ (4-1)*(KeyCost + ValCost + 4) = 3*(5+16+4) ≈ 75
	// 为触发此阈值，把 MinNetSaveBytes 放大到 1000
	{
		TArray<TSharedPtr<FJsonValue>> Items = {
			JObj({ {TEXT("name"), JStr(TEXT("A"))}, {TEXT("t"), JNum(1)} }),
			JObj({ {TEXT("name"), JStr(TEXT("B"))}, {TEXT("t"), JNum(1)} }),
			JObj({ {TEXT("name"), JStr(TEXT("C"))}, {TEXT("t"), JNum(1)} }),
			JObj({ {TEXT("name"), JStr(TEXT("D"))}, {TEXT("t"), JNum(1)} }),
		};
		FNexusResponseCompactorUtils C;
		C.MinNetSaveBytes = 1000;   // 人为放大阈值
		C.AddCandidate(TEXT("t"));
		C.CompactArray(Items);

		TestFalse(TEXT("MinNetSaveBytes: save < threshold bail out"), C.HasDefaults());
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────
// 3. ForcedDefault：无条件写入，且能在 N<MinCount 的小样本场景也生效
//    （对应 get_output_log categoryFilter 的不变式）
// ─────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkResponseCompactorForcedDefaultTest,
	"NexusLink.Utils.ResponseCompactor.ForcedDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkResponseCompactorForcedDefaultTest::RunTest(const FString& Parameters)
{
	// ─── ForcedDefault 在 N=1 时也命中 ───
	{
		TArray<TSharedPtr<FJsonValue>> Items = {
			JObj({ {TEXT("message"), JStr(TEXT("hello"))}, {TEXT("category"), JStr(TEXT("LogTemp"))} }),
		};
		FNexusResponseCompactorUtils C;
		C.AddForcedDefault(TEXT("category"), FString(TEXT("LogTemp")));
		C.CompactArray(Items);

		TestTrue(TEXT("forced: defaults produced even for N=1"), C.HasDefaults());
		TestEqual(TEXT("forced: default value == LogTemp"),
			C.GetDefaults()->GetStringField(TEXT("category")), FString(TEXT("LogTemp")));
		TestFalse(TEXT("forced: entry stripped"),
			EntryObj(Items, 0)->HasField(TEXT("category")));
	}

	// ─── ForcedDefault 与条目值不匹配时保留条目字段（不盲删） ───
	{
		TArray<TSharedPtr<FJsonValue>> Items = {
			JObj({ {TEXT("message"), JStr(TEXT("a"))}, {TEXT("category"), JStr(TEXT("LogTemp"))} }),
			JObj({ {TEXT("message"), JStr(TEXT("b"))}, {TEXT("category"), JStr(TEXT("LogOther"))} }),
		};
		FNexusResponseCompactorUtils C;
		C.AddForcedDefault(TEXT("category"), FString(TEXT("LogTemp")));
		C.CompactArray(Items);

		TestFalse(TEXT("forced: matching entry stripped"),
			EntryObj(Items, 0)->HasField(TEXT("category")));
		TestTrue(TEXT("forced: non-matching entry keeps field"),
			EntryObj(Items, 1)->HasField(TEXT("category")));
		TestEqual(TEXT("forced: non-matching value preserved"),
			EntryObj(Items, 1)->GetStringField(TEXT("category")), FString(TEXT("LogOther")));
	}

	// ─── ForcedDefault + AutoDiscover 同时生效（模拟 get_output_log 行为） ───
	{
		TArray<TSharedPtr<FJsonValue>> Items = {
			JObj({ {TEXT("message"), JStr(TEXT("a"))}, {TEXT("category"), JStr(TEXT("LogTemp"))}, {TEXT("verbosity"), JStr(TEXT("Log"))} }),
			JObj({ {TEXT("message"), JStr(TEXT("b"))}, {TEXT("category"), JStr(TEXT("LogTemp"))}, {TEXT("verbosity"), JStr(TEXT("Log"))} }),
			JObj({ {TEXT("message"), JStr(TEXT("c"))}, {TEXT("category"), JStr(TEXT("LogTemp"))}, {TEXT("verbosity"), JStr(TEXT("Log"))} }),
			JObj({ {TEXT("message"), JStr(TEXT("d"))}, {TEXT("category"), JStr(TEXT("LogTemp"))}, {TEXT("verbosity"), JStr(TEXT("Warning"))} }),
		};
		FNexusResponseCompactorUtils C;
		C.AddForcedDefault(TEXT("category"), FString(TEXT("LogTemp")));
		C.SetAutoDiscover(true);
		C.CompactArray(Items);

		TestTrue(TEXT("forced+auto: defaults produced"), C.HasDefaults());
		TestEqual(TEXT("forced+auto: category forced"),
			C.GetDefaults()->GetStringField(TEXT("category")), FString(TEXT("LogTemp")));
		TestEqual(TEXT("forced+auto: verbosity auto-discovered"),
			C.GetDefaults()->GetStringField(TEXT("verbosity")), FString(TEXT("Log")));
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────
// 4. AutoDiscover：未显式声明的字段能被自动发现，身份字段严禁进 defaults
//    （对应 GetBuiltinAutoDiscoverExclusions 的 13 个字段全量守护）
// ─────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkResponseCompactorAutoDiscoverTest,
	"NexusLink.Utils.ResponseCompactor.AutoDiscover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkResponseCompactorAutoDiscoverTest::RunTest(const FString& Parameters)
{
	// ─── AutoDiscover 能发现未声明的字段 ───
	{
		TArray<TSharedPtr<FJsonValue>> Items = {
			JObj({ {TEXT("name"), JStr(TEXT("A"))}, {TEXT("rarity"), JStr(TEXT("Common"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("B"))}, {TEXT("rarity"), JStr(TEXT("Common"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("C"))}, {TEXT("rarity"), JStr(TEXT("Common"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("D"))}, {TEXT("rarity"), JStr(TEXT("Common"))} }),
		};
		FNexusResponseCompactorUtils C;
		C.SetAutoDiscover(true);   // 不加任何 AddCandidate
		C.CompactArray(Items);

		TestTrue(TEXT("auto: discovered rarity field"), C.HasDefaults());
		TestEqual(TEXT("auto: default rarity == Common"),
			C.GetDefaults()->GetStringField(TEXT("rarity")), FString(TEXT("Common")));
	}

	// ─── 内置身份字段绝不进 defaults，哪怕值完全相同 ───
	{
		// 13 个身份字段 + 一个可压缩字段
		const TCHAR* Identities[] = {
			TEXT("name"), TEXT("path"), TEXT("assetPath"), TEXT("nodeId"),
			TEXT("tag"), TEXT("message"), TEXT("timestamp"), TEXT("frame"),
			TEXT("id"), TEXT("label"), TEXT("title"), TEXT("text"),
			TEXT("error"),
		};

		TArray<TSharedPtr<FJsonValue>> Items;
		for (int32 i = 0; i < 5; ++i)
		{
			// 所有身份字段值恒定（模拟巧合），同时加一个可压缩字段 status
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			for (const TCHAR* Id : Identities)
			{
				O->SetStringField(FString(Id), TEXT("CONST_VALUE"));
			}
			O->SetStringField(TEXT("status"), TEXT("Ready"));
			Items.Add(MakeShared<FJsonValueObject>(O));
		}

		FNexusResponseCompactorUtils C;
		C.SetAutoDiscover(true);
		C.CompactArray(Items);

		TestTrue(TEXT("identity: status still compacted"), C.HasDefaults());
		TestEqual(TEXT("identity: status in defaults"),
			C.GetDefaults()->GetStringField(TEXT("status")), FString(TEXT("Ready")));

		// 13 个身份字段中任意一个都不能出现在 defaults 里
		for (const TCHAR* Id : Identities)
		{
			TestFalse(
				*FString::Printf(TEXT("identity '%s' must NOT appear in defaults"), Id),
				C.GetDefaults()->HasField(FString(Id)));
		}
	}

	// ─── 业务追加的额外排除字段也被尊重 ───
	{
		TArray<TSharedPtr<FJsonValue>> Items = {
			JObj({ {TEXT("name"), JStr(TEXT("A"))}, {TEXT("secret"), JStr(TEXT("hush"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("B"))}, {TEXT("secret"), JStr(TEXT("hush"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("C"))}, {TEXT("secret"), JStr(TEXT("hush"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("D"))}, {TEXT("secret"), JStr(TEXT("hush"))} }),
		};
		FNexusResponseCompactorUtils C;
		C.SetAutoDiscover(true, { TEXT("secret") });
		C.CompactArray(Items);

		TestFalse(TEXT("extra exclusion: 'secret' must NOT be compacted"),
			C.HasDefaults() && C.GetDefaults()->HasField(TEXT("secret")));
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────
// 5. 边界：空数组、N=0、深度限制
// ─────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkResponseCompactorEdgeCasesTest,
	"NexusLink.Utils.ResponseCompactor.EdgeCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkResponseCompactorEdgeCasesTest::RunTest(const FString& Parameters)
{
	// ─── 空数组：不产生 defaults 且无 crash ───
	{
		TArray<TSharedPtr<FJsonValue>> Items;
		FNexusResponseCompactorUtils C;
		C.AddCandidate(TEXT("foo"));
		C.CompactArray(Items);
		TestFalse(TEXT("empty array: no defaults"), C.HasDefaults());
	}

	// ─── 条目里都没有目标字段：不产生 defaults ───
	{
		TArray<TSharedPtr<FJsonValue>> Items = {
			JObj({ {TEXT("name"), JStr(TEXT("A"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("B"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("C"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("D"))} }),
		};
		FNexusResponseCompactorUtils C;
		C.AddCandidate(TEXT("missingField"));
		C.CompactArray(Items);
		TestFalse(TEXT("no field present: no defaults"), C.HasDefaults());
	}

	// ─── 只有部分条目持有字段：比例按"持有条目数"分母算 ───
	{
		// 6 条：3 条持有 category="Weapon"，3 条无 category → HaveCount=3，Top=3，Ratio=100%
		TArray<TSharedPtr<FJsonValue>> Items = {
			JObj({ {TEXT("name"), JStr(TEXT("A"))}, {TEXT("category"), JStr(TEXT("Weapon"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("B"))}, {TEXT("category"), JStr(TEXT("Weapon"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("C"))}, {TEXT("category"), JStr(TEXT("Weapon"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("D"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("E"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("F"))} }),
		};
		FNexusResponseCompactorUtils C;
		C.AddCandidate(TEXT("category"));
		C.CompactArray(Items);

		TestTrue(TEXT("partial field: still compacted by HaveCount"), C.HasDefaults());
		TestEqual(TEXT("partial field: value"),
			C.GetDefaults()->GetStringField(TEXT("category")),
			FString(TEXT("Weapon")));
	}

	// ─── Emit：空前缀 → "defaults" key，非空 → "<prefix>_defaults" ───
	{
		TArray<TSharedPtr<FJsonValue>> Items = {
			JObj({ {TEXT("name"), JStr(TEXT("A"))}, {TEXT("type"), JStr(TEXT("Actor"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("B"))}, {TEXT("type"), JStr(TEXT("Actor"))} }),
			JObj({ {TEXT("name"), JStr(TEXT("C"))}, {TEXT("type"), JStr(TEXT("Actor"))} }),
		};
		FNexusResponseCompactorUtils C;
		C.AddCandidate(TEXT("type"));
		C.CompactArray(Items);

		TSharedPtr<FJsonObject> P1 = MakeShared<FJsonObject>();
		C.Emit(P1, TEXT("items"));
		TestTrue(TEXT("emit: with prefix -> items_defaults"),
			P1->HasField(TEXT("items_defaults")));

		TSharedPtr<FJsonObject> P2 = MakeShared<FJsonObject>();
		C.Emit(P2, FString());
		TestTrue(TEXT("emit: empty prefix -> 'defaults' key"),
			P2->HasField(TEXT("defaults")));
	}

	// ─── HasDefaults=false 时 Emit 不写入 ───
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		FNexusResponseCompactorUtils C;
		TArray<TSharedPtr<FJsonValue>> Empty;
		C.CompactArray(Empty);
		C.Emit(P, TEXT("items"));
		TestEqual(TEXT("emit: no-op when no defaults"), P->Values.Num(), 0);
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────
// 6. AutoCompactRecursive：Dispatcher 统一 Pass 的递归行为
// ─────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkResponseCompactorRecursiveTest,
	"NexusLink.Utils.ResponseCompactor.AutoRecursive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkResponseCompactorRecursiveTest::RunTest(const FString& Parameters)
{
	// ─── 嵌套对象数组：外层 "results" + 内层 "items" 都各自产出 defaults ───
	{
		// 构造：{ results: [ { name:A, items: [..3 个同类..] }, ..] }
		auto MakeInnerArray = [](const TCHAR* Type) {
			TArray<TSharedPtr<FJsonValue>> Inner;
			for (int32 i = 0; i < 4; ++i)
			{
				Inner.Add(JObj({
					{TEXT("name"), JStr(*FString::Printf(TEXT("inner_%d"), i))},
					{TEXT("type"), JStr(Type)},
				}));
			}
			return MakeShared<FJsonValueArray>(Inner);
		};

		TArray<TSharedPtr<FJsonValue>> Results;
		for (int32 i = 0; i < 3; ++i)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), FString::Printf(TEXT("result_%d"), i));
			Entry->SetStringField(TEXT("status"), TEXT("OK"));
			Entry->SetField(TEXT("items"), MakeInnerArray(TEXT("Widget")));
			Results.Add(MakeShared<FJsonValueObject>(Entry));
		}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("results"), Results);

		FNexusResponseCompactorUtils::AutoCompactRecursive(Root);

		// 外层：results 里 status="OK" 全等 → results_defaults 应含 status
		TestTrue(TEXT("recursive: outer results_defaults emitted"),
			Root->HasField(TEXT("results_defaults")));
		if (Root->HasField(TEXT("results_defaults")))
		{
			TSharedPtr<FJsonObject> OuterDef = Root->GetObjectField(TEXT("results_defaults"));
			TestEqual(TEXT("recursive: outer default status"),
				OuterDef->GetStringField(TEXT("status")), FString(TEXT("OK")));
		}

		// 内层：每个 result 里的 items_defaults 应含 type="Widget"
		TSharedPtr<FJsonObject> FirstResult = EntryObj(Root->GetArrayField(TEXT("results")), 0);
		TestTrue(TEXT("recursive: inner items_defaults emitted on each result"),
			FirstResult->HasField(TEXT("items_defaults")));
		if (FirstResult->HasField(TEXT("items_defaults")))
		{
			TestEqual(TEXT("recursive: inner default type"),
				FirstResult->GetObjectField(TEXT("items_defaults"))->GetStringField(TEXT("type")),
				FString(TEXT("Widget")));
		}
	}

	// ─── 尊重工具侧已手动写入的 <K>_defaults（不覆盖、不双写） ───
	{
		TArray<TSharedPtr<FJsonValue>> Entries;
		for (int32 i = 0; i < 4; ++i)
		{
			Entries.Add(JObj({
				{TEXT("name"), JStr(*FString::Printf(TEXT("e_%d"), i))},
				{TEXT("category"), JStr(TEXT("LogTemp"))},
				{TEXT("verbosity"), JStr(TEXT("Log"))},
			}));
		}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("entries"), Entries);

		// 模拟工具侧已手动写入 entries_defaults（只声明了 category）
		TSharedPtr<FJsonObject> ToolDefaults = MakeShared<FJsonObject>();
		ToolDefaults->SetStringField(TEXT("category"), TEXT("LogTemp"));
		Root->SetObjectField(TEXT("entries_defaults"), ToolDefaults);

		FNexusResponseCompactorUtils::AutoCompactRecursive(Root);

		// Dispatcher 检测到同级 entries_defaults 已存在 → 跳过整段，不修改也不补 verbosity
		TSharedPtr<FJsonObject> FinalDefaults = Root->GetObjectField(TEXT("entries_defaults"));
		TestEqual(TEXT("skip-existing: tool's category preserved"),
			FinalDefaults->GetStringField(TEXT("category")), FString(TEXT("LogTemp")));
		TestFalse(TEXT("skip-existing: auto pass did NOT add verbosity"),
			FinalDefaults->HasField(TEXT("verbosity")));
	}

	// ─── 跳过协议保留字段名：`content` 与 `*_defaults` ───
	{
		// content 数组：哪怕看起来可压缩也绝不压缩（MCP 协议保留）
		TArray<TSharedPtr<FJsonValue>> Content;
		for (int32 i = 0; i < 4; ++i)
		{
			Content.Add(JObj({
				{TEXT("type"), JStr(TEXT("text"))},
				{TEXT("text"), JStr(*FString::Printf(TEXT("line %d"), i))},
			}));
		}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("content"), Content);

		FNexusResponseCompactorUtils::AutoCompactRecursive(Root);

		TestFalse(TEXT("skip-protocol: `content` must never emit content_defaults"),
			Root->HasField(TEXT("content_defaults")));
	}

	// ─── 深度防御：病态深嵌套不崩（只验证不 crash，实际抽取视条目而定） ───
	{
		TSharedPtr<FJsonObject> Current = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> Root = Current;
		for (int32 i = 0; i < 30; ++i)
		{
			TSharedPtr<FJsonObject> Next = MakeShared<FJsonObject>();
			Current->SetObjectField(TEXT("nested"), Next);
			Current = Next;
		}
		FNexusResponseCompactorUtils::AutoCompactRecursive(Root);
		TestTrue(TEXT("depth: recursion survived 30-level deep object chain"), true);
	}

	// ─── 空根：nullptr 与空对象都安全 ───
	{
		FNexusResponseCompactorUtils::AutoCompactRecursive(TSharedPtr<FJsonObject>());
		FNexusResponseCompactorUtils::AutoCompactRecursive(MakeShared<FJsonObject>());
		TestTrue(TEXT("null/empty root: no crash"), true);
	}

	return true;
}

