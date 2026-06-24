// Copyright byteyang. All Rights Reserved.

#include "NexusCapabilityRegistry.h"
#include "NexusMcpTool.h"           // FNexusMcpTags
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"

// ── 辅助：深拷贝 FJsonObject（序列化往返，注册期一次性调用）────────────────────
static TSharedPtr<FJsonObject> DeepCloneJsonObject(const TSharedPtr<FJsonObject>& Src)
{
	if (!Src.IsValid()) return nullptr;

	FString JsonStr;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonStr);
	if (!FJsonSerializer::Serialize(Src.ToSharedRef(), Writer)) return Src;

	TSharedPtr<FJsonObject> Clone;
	TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(JsonStr);
	FJsonSerializer::Deserialize(Reader, Clone);
	return Clone;
}

// ── 辅助：将字符串按非字母数字分词（下划线也作为分隔符）──────────────────────
static TArray<FString> TokenizeIdentifier(const FString& Str)
{
	TArray<FString> Tokens;
	FString Lower = Str.ToLower();
	// 下划线替换为空格后按空白分词
	Lower.ReplaceInline(TEXT("_"), TEXT(" "));
	Lower.ParseIntoArrayWS(Tokens);
	return Tokens;
}

// ── 辅助：从 cap 构建预算关键词列表（注册期一次性计算）─────────────────────────
static TArray<FString> BuildKeywords(const FNexusCapabilityDefinition& Def,
                                     const TArray<FString>& ExtraKeywords)
{
	// 功能分类标签集合（从 Tags 中提取）
	static const TArray<FString> CategoryTags = {
		FNexusMcpTags::Editor,
		FNexusMcpTags::Blueprint,
		FNexusMcpTags::Material,
		FNexusMcpTags::Struct,
		FNexusMcpTags::Data,
		FNexusMcpTags::Widget,
		FNexusMcpTags::Runtime,
	};

	// Name 的 token 集合（用于剥离 ExtraKeywords 重叠）
	TSet<FString> NameTokenSet;
	for (const FString& T : TokenizeIdentifier(Def.Name))
	{
		NameTokenSet.Add(T);
	}

	TArray<FString> Words;
	// Name：保留原始形态（精确名兜底）+ 按下划线拆词入索引
	Words.Add(Def.Name.ToLower());
	for (const FString& T : TokenizeIdentifier(Def.Name))
		Words.Add(T);
	// Description 分词
	Def.Description.ToLower().ParseIntoArrayWS(Words);
	// 子类额外词（自动剥离与 name token 重叠的词，减少噪音）
	for (const FString& E : ExtraKeywords)
	{
		FString ELow = E.ToLower();
		if (!NameTokenSet.Contains(ELow))
		{
			Words.Add(ELow);
		}
	}
	// Tags 中的功能分类自动入搜索词
	for (const FString& Tag : Def.Tags)
	{
		FString TagLow = Tag.ToLower();
		if (CategoryTags.Contains(TagLow))
		{
			Words.Add(TagLow);
		}
	}

	// 去重，保持首次出现顺序
	TSet<FString> Seen;
	TArray<FString> Result;
	Result.Reserve(Words.Num());
	for (const FString& W : Words)
	{
		if (W.IsEmpty()) continue;
		bool bAlreadyIn = false;
		Seen.Add(W, &bAlreadyIn);
		if (!bAlreadyIn) Result.Add(W);
	}
	return Result;
}

// ── 辅助：注册期元数据硬校验（仅保留会阻塞启动的项；格式质量改由 audit_capability_naming.py CI 门禁）──
static void ValidateCapabilityMetadata(const FNexusCapabilityDefinition& Def)
{
	// 描述长度硬上限：超长会撑爆 tools/list token 预算
	ensureMsgf(Def.Description.Len() <= 100,
		TEXT("[NexusCap] '%s' description too long (%d chars, max 100): \"%s\""),
		*Def.Name, Def.Description.Len(), *Def.Description);
}

// ── §6 命名：首段动词须在标准词表内 ───────────────────────────────────────────
static const TSet<FString>& GetAllowedCapabilityVerbs()
{
	static TSet<FString> Verbs;
	if (Verbs.Num() == 0)
	{
		const TCHAR* Allowed[] = {
			TEXT("search"), TEXT("list"), TEXT("get"), TEXT("set"), TEXT("manage"),
			TEXT("create"), TEXT("delete"), TEXT("rename"), TEXT("duplicate"), TEXT("save"),
			TEXT("spawn"), TEXT("destroy"), TEXT("diff"), TEXT("interact"), TEXT("control"),
			TEXT("exec"), TEXT("eval"), TEXT("dofile"), TEXT("gc"), TEXT("hotreload"), TEXT("capture"),
			TEXT("compile"), TEXT("export"), TEXT("reimport"),
		};
		for (const TCHAR* V : Allowed)
		{
			Verbs.Add(V);
		}
	}
	return Verbs;
}

static void ValidateCapabilityName(const FNexusCapabilityDefinition& Def)
{
	const int32 UnderscoreIdx = Def.Name.Find(TEXT("_"));
	const FString Verb = UnderscoreIdx == INDEX_NONE
		? Def.Name
		: Def.Name.Left(UnderscoreIdx);

	ensureMsgf(GetAllowedCapabilityVerbs().Contains(Verb),
		TEXT("[NexusCap] '%s' first token '%s' is not an allowed capability verb (see CapabilitySpec §6)"),
		*Def.Name, *Verb);

	static const TSet<FString> ForbiddenNames = {
		TEXT("manage_animation"),
		TEXT("set_runtime_actor_animation"),
		TEXT("get_asset_generic"),
	};
	ensureMsgf(!ForbiddenNames.Contains(Def.Name),
		TEXT("[NexusCap] forbidden legacy name '%s' (see CapabilitySpec §6.5)"),
		*Def.Name);
}

// ─────────────────────────────────────────────────────────────────────────────

FNexusCapabilityRegistry& FNexusCapabilityRegistry::Get()
{
	static FNexusCapabilityRegistry Instance;
	return Instance;
}

void FNexusCapabilityRegistry::Register(TSharedRef<FNexusCapability> Cap)
{
	// 触发 BuildDefinition（首次调用，结果缓存在实例上）
	FNexusCapabilityDefinition Def = Cap->GetDefinition();

	// 重名检查 O(1)
	const FString NameLow = Def.Name.ToLower();
	if (NameIndex.Contains(NameLow))
	{
		ensureMsgf(false,
			TEXT("Duplicate Capability name '%s' registered; first wins."), *Def.Name);
		return;
	}

	// 元数据质量校验（违反 → ensureMsgf，Dev 构建可见）
	ValidateCapabilityMetadata(Def);
	ValidateCapabilityName(Def);

	// InputSchema deep-clone，防止 cap 子类持有同一指针被外部修改污染
	Def.InputSchema = DeepCloneJsonObject(Def.InputSchema);

	// 预算关键词（含 Tags 功能分类自动追加；ExtraKeywords 中 name token 会被自动剥离）
	TArray<FString> Keywords = BuildKeywords(Def, Def.ExtraSearchKeywords);

	// 构建 record，存入注册表
	FCapRecord Record(Cap);
	Record.Def      = MoveTemp(Def);
	Record.Keywords = MoveTemp(Keywords);

	const int32 Idx = Records.Add(MoveTemp(Record));
	NameIndex.Add(NameLow, Idx);
}

const FCapRecord* FNexusCapabilityRegistry::FindRecordByName(const FString& CapabilityName) const
{
	if (CapabilityName.IsEmpty()) return nullptr;
	const int32* Idx = NameIndex.Find(CapabilityName.ToLower());
	if (!Idx) return nullptr;
	return &Records[*Idx];
}

void FNexusCapabilityRegistry::Reset()
{
	Records.Reset();
	NameIndex.Reset();
}
