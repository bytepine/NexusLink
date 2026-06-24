// Copyright byteyang. All Rights Reserved.

#include "NexusFeedback.h"
#include "NexusLinkSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "HAL/FileManager.h"
#include "HAL/CriticalSection.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Internationalization/Regex.h"

// ── 进程内节流表（category+capability+errorPrefix → 上次记录时间）─────────────
static FCriticalSection GFeedbackMutex;
static TMap<FString, FDateTime> GThrottleMap;

/**
 * 规则化字符串：将数字序列、引号内字符串、UE 包路径（/Game/... /Engine/...）替换为 *，
 * 使同类但参数不同的错误命中同一节流 key。
 */
static FString NormalizeForThrottle(const FString& Input)
{
	FString Result = Input;
	// 引号字符串 → *
	{
		static const FRegexPattern Pat(TEXT("\"[^\"]*\""));
		FRegexMatcher M(Pat, Result);
		FString Out;
		int32 Last = 0;
		while (M.FindNext())
		{
			Out += Result.Mid(Last, M.GetMatchBeginning() - Last);
			Out += TEXT("*");
			Last = M.GetMatchEnding();
		}
		Out += Result.Mid(Last);
		Result = Out;
	}
	// UE 包路径 /Game/... /Engine/... /Script/... → *
	{
		static const FRegexPattern Pat(TEXT("/(?:Game|Engine|Script|Plugin|Temp)[A-Za-z0-9_/\\.]*"));
		FRegexMatcher M(Pat, Result);
		FString Out;
		int32 Last = 0;
		while (M.FindNext())
		{
			Out += Result.Mid(Last, M.GetMatchBeginning() - Last);
			Out += TEXT("*");
			Last = M.GetMatchEnding();
		}
		Out += Result.Mid(Last);
		Result = Out;
	}
	// 数字序列 → *
	{
		static const FRegexPattern Pat(TEXT("\\d+"));
		FRegexMatcher M(Pat, Result);
		FString Out;
		int32 Last = 0;
		while (M.FindNext())
		{
			Out += Result.Mid(Last, M.GetMatchBeginning() - Last);
			Out += TEXT("*");
			Last = M.GetMatchEnding();
		}
		Out += Result.Mid(Last);
		Result = Out;
	}
	return Result.Left(60);
}

/** 构建节流 key（规则化后取前 60 字符，避免同类不同参数错误穿透节流）。 */
static FString BuildThrottleKey(const FString& Category, const FNexusFeedback::FFields& Fields)
{
	const FString NormErr = NormalizeForThrottle(Fields.ErrorText);
	return Category + TEXT("|") + Fields.Capability + TEXT("|") + NormErr;
}

/** 将 FDateTime 序列化为 ISO 8601 UTC 字符串。 */
static FString DateTimeToIso8601(const FDateTime& DT)
{
	return FString::Printf(TEXT("%04d-%02d-%02dT%02d:%02d:%02dZ"),
		DT.GetYear(), DT.GetMonth(), DT.GetDay(),
		DT.GetHour(), DT.GetMinute(), DT.GetSecond());
}

/** 将一条反馈序列化为 JSON 字符串（condensed 单行）。 */
static FString BuildJsonLine(const FString& Kind, const FString& Category,
                              const FNexusFeedback::FFields& Fields)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("ts"),       DateTimeToIso8601(FDateTime::UtcNow()));
	Obj->SetStringField(TEXT("kind"),     Kind);
	Obj->SetStringField(TEXT("category"), Category);

	if (!Fields.Tool.IsEmpty())
		Obj->SetStringField(TEXT("tool"), Fields.Tool);
	if (!Fields.Capability.IsEmpty())
		Obj->SetStringField(TEXT("capability"), Fields.Capability);
	if (!Fields.Query.IsEmpty())
		Obj->SetStringField(TEXT("query"), Fields.Query);
	if (Fields.MatchCount > 0)
		Obj->SetNumberField(TEXT("matchCount"), Fields.MatchCount);
	if (!Fields.ArgsDigest.IsEmpty())
		Obj->SetStringField(TEXT("argsDigest"), Fields.ArgsDigest);
	if (!Fields.ErrorText.IsEmpty())
		Obj->SetStringField(TEXT("errorText"), Fields.ErrorText.Left(120));
	if (!Fields.Note.IsEmpty())
		Obj->SetStringField(TEXT("note"), Fields.Note);
	if (!Fields.AttemptedArgs.IsEmpty())
		Obj->SetStringField(TEXT("attemptedArgs"), Fields.AttemptedArgs.Left(200));
	if (!Fields.ActualError.IsEmpty())
		Obj->SetStringField(TEXT("actualError"), Fields.ActualError.Left(200));
	if (!Fields.ExpectedField.IsEmpty())
		Obj->SetStringField(TEXT("expectedField"), Fields.ExpectedField);

	FString Line;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
	return Line;
}

/** 追加一行到 feedback.jsonl（FArchive 追加写，全版本兼容）。 */
static void AppendLine(const FString& Line)
{
	const FString Dir  = FNexusFeedback::GetFeedbackDir();
	const FString File = Dir / TEXT("feedback.jsonl");

	IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);

	// FILEWRITE_Append：在文件末尾追加，不存在时创建
	FArchive* Ar = IFileManager::Get().CreateFileWriter(*File, FILEWRITE_Append | FILEWRITE_AllowRead);
	if (Ar)
	{
		const FString LineNL = Line + TEXT("\n");
		FTCHARToUTF8 Converter(*LineNL);
		Ar->Serialize(const_cast<ANSICHAR*>(Converter.Get()), Converter.Length());
		Ar->Close();
		delete Ar;
	}
}

// ── Public API ────────────────────────────────────────────────────────────────

void FNexusFeedback::RecordAuto(const FString& Category, const FFields& Fields)
{
	const UNexusLinkSettings* S = UNexusLinkSettings::Get();
	if (!S || !S->bEnableFeedback) return;

	// 节流：同 key 30 秒内只记一次
	const FString Key = BuildThrottleKey(Category, Fields);
	{
		FScopeLock Lock(&GFeedbackMutex);
		const FDateTime Now = FDateTime::UtcNow();
		if (const FDateTime* Last = GThrottleMap.Find(Key))
		{
			if ((Now - *Last).GetTotalSeconds() < 30.0)
				return;
		}
		GThrottleMap.Add(Key, Now);
	}

	AppendLine(BuildJsonLine(TEXT("auto"), Category, Fields));
}

void FNexusFeedback::RecordManual(const FString& Category, const FFields& Fields)
{
	const UNexusLinkSettings* S = UNexusLinkSettings::Get();
	if (!S || !S->bEnableFeedback) return;

	AppendLine(BuildJsonLine(TEXT("manual"), Category, Fields));
}

FString FNexusFeedback::GetFeedbackDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT(".nexus-feedback"));
}

int32 FNexusFeedback::GetRecordCount()
{
	const FString File = GetFeedbackDir() / TEXT("feedback.jsonl");
	if (!IFileManager::Get().FileExists(*File)) return 0;

	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *File);
	int32 Count = 0;
	for (const FString& L : Lines)
	{
		if (!L.TrimStartAndEnd().IsEmpty()) ++Count;
	}
	return Count;
}

void FNexusFeedback::Clear()
{
	const FString File = GetFeedbackDir() / TEXT("feedback.jsonl");
	if (IFileManager::Get().FileExists(*File))
	{
		IFileManager::Get().Delete(*File);
	}
	// 清空节流表，避免清空后 30 秒内新事件被误压制
	FScopeLock Lock(&GFeedbackMutex);
	GThrottleMap.Empty();
}

// ── ExportReport ─────────────────────────────────────────────────────────────

namespace NexusFeedbackInternal
{
	/** 从 JSONL 行数组解析出所有有效 JSON 对象。 */
	static TArray<TSharedPtr<FJsonObject>> ParseLines(const TArray<FString>& Lines)
	{
		TArray<TSharedPtr<FJsonObject>> Result;
		for (const FString& Line : Lines)
		{
			FString Trimmed = Line;
			Trimmed.TrimStartAndEndInline();
			if (Trimmed.IsEmpty()) continue;
			TSharedPtr<FJsonObject> Obj;
			TSharedRef<TJsonReader<TCHAR>> R = TJsonReaderFactory<TCHAR>::Create(Trimmed);
			if (FJsonSerializer::Deserialize(R, Obj) && Obj.IsValid())
				Result.Add(Obj);
		}
		return Result;
	}

	/** 从 JSON 对象安全取字符串，缺失返回 ""。 */
	static FString GetStr(const TSharedPtr<FJsonObject>& O, const FString& Key)
	{
		FString V;
		O->TryGetStringField(Key, V);
		return V;
	}

	/** 构建 category → count 摘要 JSON 对象（用于 last_summary.json）。 */
	static TSharedPtr<FJsonObject> BuildCategorySummary(const TArray<TSharedPtr<FJsonObject>>& Records)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		TMap<FString, int32> CatCount;
		for (const auto& R : Records)
		{
			const FString Cat = GetStr(R, TEXT("category"));
			int32& V = CatCount.FindOrAdd(Cat);
			V++;
		}
		for (const auto& P : CatCount)
		{
			Obj->SetNumberField(P.Key, P.Value);
		}
		return Obj;
	}

	/** 构建 Markdown 报告正文。
	 *  PrevCatSummary：上期 category→count JSON，可为 nullptr（无历史）。
	 */
	static FString BuildMarkdown(const TArray<TSharedPtr<FJsonObject>>& Records,
	                              const TSharedPtr<FJsonObject>& PrevCatSummary)
	{
		if (Records.Num() == 0)
		{
			return TEXT("# NexusLink Capability 反馈报告\n\n_无数据_\n");
		}

		// 当期 category 计数
		TMap<FString, int32> CatCount;
		for (const auto& R : Records)
		{
			const FString Cat = GetStr(R, TEXT("category"));
			int32& V = CatCount.FindOrAdd(Cat);
			V++;
		}

		FString Md;
		const FString Ts = DateTimeToIso8601(FDateTime::UtcNow());
		Md += FString::Printf(TEXT("# NexusLink Capability 反馈报告\n\n生成时间：%s\n\n"), *Ts);

		// §0 趋势（仅在有上期数据时输出）
		if (PrevCatSummary.IsValid())
		{
			Md += TEXT("## §0 趋势（与上期对比）\n\n");
			TSet<FString> AllKeys;
			for (const auto& P : CatCount) AllKeys.Add(P.Key);
			for (const auto& P : PrevCatSummary->Values) AllKeys.Add(FString(*P.Key));
			TArray<FString> KeyArr = AllKeys.Array();
			KeyArr.Sort();
			Md += TEXT("| category | 上期 | 本期 | 变化 |\n|---|---|---|---|\n");
			for (const FString& K : KeyArr)
			{
				int32 Prev = 0;
				int32 Curr = CatCount.FindRef(K);
				PrevCatSummary->TryGetNumberField(K, Prev);
				const int32 Delta = Curr - Prev;
				const FString Arrow = Delta > 0 ? TEXT("↑") : (Delta < 0 ? TEXT("↓") : TEXT("→"));
				Md += FString::Printf(TEXT("| `%s` | %d | %d | %s%d |\n"), *K, Prev, Curr, *Arrow, FMath::Abs(Delta));
			}
			Md += TEXT("\n---\n\n");
		}

		// §1 概览
		Md += TEXT("## §1 概览\n\n");
		Md += FString::Printf(TEXT("总条数：**%d**\n\n"), Records.Num());
		Md += TEXT("| category | 次数 |\n|---|---|\n");
		{
			TArray<FString> CatKeys;
			CatCount.GetKeys(CatKeys);
			CatKeys.Sort();
			for (const FString& Key : CatKeys)
			{
				Md += FString::Printf(TEXT("| `%s` | %d |\n"), *Key, CatCount[Key]);
			}
		}
		Md += TEXT("\n---\n\n");

		// §2 搜索失败 Top 10（search_zero）
		{
			TMap<FString, int32> QueryCount;
			for (const auto& R : Records)
			{
				if (GetStr(R, TEXT("category")) != TEXT("search_zero")) continue;
				const FString Q = GetStr(R, TEXT("query"));
				if (!Q.IsEmpty()) { int32& V = QueryCount.FindOrAdd(Q); V++; }
			}
			Md += TEXT("## §2 搜索失败 Top 10（search_zero）\n\n");
			if (QueryCount.Num() == 0)
			{
				Md += TEXT("_无数据_\n\n");
			}
			else
			{
				TArray<TPair<FString, int32>> Arr;
				for (const auto& P : QueryCount) Arr.Add(P);
				Arr.Sort([](const TPair<FString,int32>& A, const TPair<FString,int32>& B){ return A.Value > B.Value; });
				Md += TEXT("| query | 次数 |\n|---|---|\n");
				int32 Cnt = 0;
				for (const auto& P : Arr)
				{
					if (++Cnt > 10) break;
					Md += FString::Printf(TEXT("| `%s` | %d |\n"), *P.Key, P.Value);
				}
				Md += TEXT("\n");
			}
			Md += TEXT("---\n\n");
		}

		// §3 搜索过载 Top 10（search_overflow）
		{
			struct FOverflowStat { int32 Count = 0; int64 TotalMatch = 0; };
			TMap<FString, FOverflowStat> Stats;
			for (const auto& R : Records)
			{
				if (GetStr(R, TEXT("category")) != TEXT("search_overflow")) continue;
				const FString Q = GetStr(R, TEXT("query"));
				if (Q.IsEmpty()) continue;
				int32 MC = 0;
				R->TryGetNumberField(TEXT("matchCount"), MC);
				FOverflowStat& S = Stats.FindOrAdd(Q);
				S.Count++;
				S.TotalMatch += MC;
			}
			Md += TEXT("## §3 搜索过载 Top 10（search_overflow）\n\n");
			if (Stats.Num() == 0)
			{
				Md += TEXT("_无数据_\n\n");
			}
			else
			{
				TArray<TPair<FString, FOverflowStat>> Arr;
				for (const auto& P : Stats) Arr.Add(P);
				Arr.Sort([](const TPair<FString,FOverflowStat>& A, const TPair<FString,FOverflowStat>& B){ return A.Value.Count > B.Value.Count; });
				Md += TEXT("| query | 次数 | 平均命中数 |\n|---|---|---|\n");
				int32 Cnt = 0;
				for (const auto& P : Arr)
				{
					if (++Cnt > 10) break;
					const double Avg = P.Value.Count > 0
						? static_cast<double>(P.Value.TotalMatch) / P.Value.Count : 0.0;
					Md += FString::Printf(TEXT("| `%s` | %d | %.1f |\n"), *P.Key, P.Value.Count, Avg);
				}
				Md += TEXT("\n");
			}
			Md += TEXT("---\n\n");
		}

		// §4 Capability 误用 Top 10（call_*），二级分组：capability × 规则化错误前缀 + 原始原因样本
		{
			// 外层 key: capability；内层 key: 规则化错误前缀
			TMap<FString, TMap<FString, int32>> CapErrMap;
			TMap<FString, int32> CapTotal;
			// 每个 capability 保留最多 3 条去重原始 errorText 样本
			TMap<FString, TArray<FString>> CapRawSamples;
			for (const auto& R : Records)
			{
				const FString Cat = GetStr(R, TEXT("category"));
				if (!Cat.StartsWith(TEXT("call_"))) continue;
				const FString Cap    = GetStr(R, TEXT("capability"));
				const FString CapKey = Cap.IsEmpty() ? TEXT("(unknown)") : Cap;
				const FString Err    = NormalizeForThrottle(GetStr(R, TEXT("errorText")));
				TMap<FString, int32>& Inner2 = CapErrMap.FindOrAdd(CapKey);
				int32& IV = Inner2.FindOrAdd(Err);
				IV++;
				int32& TV = CapTotal.FindOrAdd(CapKey);
				TV++;

				// 收集原始 errorText 样本（去重，最多 3 条）
				const FString RawErr = GetStr(R, TEXT("errorText"));
				if (!RawErr.IsEmpty())
				{
					TArray<FString>& Samples = CapRawSamples.FindOrAdd(CapKey);
					if (Samples.Num() < 3 && !Samples.Contains(RawErr))
					{
						Samples.Add(RawErr);
					}
				}
			}
			Md += TEXT("## §4 Capability 误用 Top 10（call_*）\n\n");
			if (CapErrMap.Num() == 0)
			{
				Md += TEXT("_无数据_\n\n");
			}
			else
			{
				// 按 capability 总次数排序
				TArray<TPair<FString, int32>> CapArr;
				for (const auto& P : CapTotal) CapArr.Add(P);
				CapArr.Sort([](const TPair<FString,int32>& A, const TPair<FString,int32>& B){ return A.Value > B.Value; });
				int32 CapCnt = 0;
				for (const auto& CP : CapArr)
				{
					if (++CapCnt > 10) break;
					Md += FString::Printf(TEXT("**%s** (%d 次)\n\n"), *CP.Key, CP.Value);
					Md += TEXT("| 错误类型（规则化） | 次数 |\n|---|---|\n");
					// 按次数排序该 capability 下的错误
					const TMap<FString, int32>& ErrMap = CapErrMap[CP.Key];
					TArray<TPair<FString, int32>> ErrArr;
					for (const auto& EP : ErrMap) ErrArr.Add(EP);
					ErrArr.Sort([](const TPair<FString,int32>& A, const TPair<FString,int32>& B){ return A.Value > B.Value; });
					int32 ErrCnt = 0;
					for (const auto& EP : ErrArr)
					{
						if (++ErrCnt > 5) break;
						const FString ErrLabel = EP.Key.Len() > 60 ? EP.Key.Left(60) + TEXT("…") : EP.Key;
						Md += FString::Printf(TEXT("| `%s` | %d |\n"), *ErrLabel, EP.Value);
					}

					// 原始原因样本（最多 3 条，保留真实 errorText 便于排查）
					if (const TArray<FString>* Samples = CapRawSamples.Find(CP.Key))
					{
						Md += TEXT("\n原因样本：\n\n");
						for (const FString& S : *Samples)
						{
							const FString Snip = S.Len() > 150 ? S.Left(150) + TEXT("…") : S;
							Md += FString::Printf(TEXT("> %s\n"), *Snip);
						}
					}
					Md += TEXT("\n");
				}
			}
			Md += TEXT("---\n\n");
		}

		// §5 工具选错/用错样本（wrong_tool / schema_guess / misuse）
		{
			TArray<FString> Notes;
			for (const auto& R : Records)
			{
				const FString Cat = GetStr(R, TEXT("category"));
				if (Cat != TEXT("wrong_tool") && Cat != TEXT("schema_guess") && Cat != TEXT("misuse")) continue;
				const FString Note = GetStr(R, TEXT("note"));
				if (!Note.IsEmpty()) Notes.Add(Note);
			}
			Md += TEXT("## §5 工具选错/用错样本（wrong_tool / misuse / schema_guess）\n\n");
			if (Notes.Num() == 0)
			{
				Md += TEXT("_无数据_\n\n");
			}
			else
			{
				int32 Shown = 0;
				for (const FString& Note : Notes)
				{
					if (++Shown > 10) break;
					const FString Snip = Note.Len() > 120 ? Note.Left(120) + TEXT("…") : Note;
					Md += FString::Printf(TEXT("- %s\n"), *Snip);
				}
				Md += TEXT("\n");
			}
			Md += TEXT("---\n\n");
		}

		// §6 按 tool 维度统计
		{
			TMap<FString, int32> ToolCount;
			for (const auto& R : Records)
			{
				const FString T = GetStr(R, TEXT("tool"));
				if (!T.IsEmpty()) { int32& V = ToolCount.FindOrAdd(T); V++; }
			}
			Md += TEXT("## §6 按 MCP Tool 统计\n\n");
			if (ToolCount.Num() == 0)
			{
				Md += TEXT("_无数据_\n\n");
			}
			else
			{
				TArray<TPair<FString, int32>> Arr;
				for (const auto& P : ToolCount) Arr.Add(P);
				Arr.Sort([](const TPair<FString,int32>& A, const TPair<FString,int32>& B){ return A.Value > B.Value; });
				Md += TEXT("| tool | 次数 |\n|---|---|\n");
				for (const auto& P : Arr)
				{
					Md += FString::Printf(TEXT("| `%s` | %d |\n"), *P.Key, P.Value);
				}
				Md += TEXT("\n");
			}
			Md += TEXT("---\n\n");
		}

		// §7 慢调用 Top 10（slow_call）：capability × 次数 / 平均耗时 / 最大耗时
		{
			struct FSlowStat { int32 Count = 0; int64 TotalMs = 0; int32 MaxMs = 0; };
			TMap<FString, FSlowStat> Stats;
			for (const auto& R : Records)
			{
				if (GetStr(R, TEXT("category")) != TEXT("slow_call")) continue;
				const FString Cap = GetStr(R, TEXT("capability"));
				if (Cap.IsEmpty()) continue;
				const FString Note = GetStr(R, TEXT("note"));
				int32 ElapsedMs = 0;
				if (Note.StartsWith(TEXT("slow_call:")))
				{
					// 兼容旧格式（若存在）
					ElapsedMs = FCString::Atoi(*Note.Mid(10));
				}
				else
				{
					// 标准格式："8371ms > threshold 1500ms"
					int32 MsPos = Note.Find(TEXT("ms"));
					if (MsPos > 0)
					{
						ElapsedMs = FCString::Atoi(*Note.Left(MsPos));
					}
				}
				FSlowStat& S = Stats.FindOrAdd(Cap);
				S.Count++;
				S.TotalMs += ElapsedMs;
				S.MaxMs = FMath::Max(S.MaxMs, ElapsedMs);
			}
			Md += TEXT("## §7 慢调用 Top 10（slow_call）\n\n");
			if (Stats.Num() == 0)
			{
				Md += TEXT("_无数据_\n\n");
			}
			else
			{
				TArray<TPair<FString, FSlowStat>> Arr;
				for (const auto& P : Stats) Arr.Add(P);
				Arr.Sort([](const TPair<FString,FSlowStat>& A, const TPair<FString,FSlowStat>& B)
				{
					if (A.Value.Count != B.Value.Count) return A.Value.Count > B.Value.Count;
					return A.Value.MaxMs > B.Value.MaxMs;
				});
				Md += TEXT("| capability | 次数 | 平均耗时 | 最大耗时 |\n|---|---|---|---|\n");
				int32 Cnt = 0;
				for (const auto& P : Arr)
				{
					if (++Cnt > 10) break;
					const double AvgMs = P.Value.Count > 0
						? static_cast<double>(P.Value.TotalMs) / P.Value.Count : 0.0;
					Md += FString::Printf(TEXT("| `%s` | %d | %.0f ms | %d ms |\n"),
						*P.Key, P.Value.Count, AvgMs, P.Value.MaxMs);
				}
				Md += TEXT("\n");
			}
		}

		return Md;
	}
} // namespace NexusFeedbackInternal

FString FNexusFeedback::ExportReport()
{
	const FString Dir       = GetFeedbackDir();
	const FString JsonlFile = Dir / TEXT("feedback.jsonl");

	if (!IFileManager::Get().FileExists(*JsonlFile))
	{
		return FString();
	}

	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *JsonlFile))
	{
		return FString();
	}

	const TArray<TSharedPtr<FJsonObject>> Records = NexusFeedbackInternal::ParseLines(Lines);

	// 读取上期摘要（用于 §0 趋势对比）
	const FString ArchiveDir     = Dir / TEXT("archive");
	const FString LastSummaryFile = ArchiveDir / TEXT("last_summary.json");
	TSharedPtr<FJsonObject> PrevSummary;
	if (IFileManager::Get().FileExists(*LastSummaryFile))
	{
		FString SummaryStr;
		if (FFileHelper::LoadFileToString(SummaryStr, *LastSummaryFile))
		{
			TSharedRef<TJsonReader<TCHAR>> SR = TJsonReaderFactory<TCHAR>::Create(SummaryStr);
			TSharedPtr<FJsonObject> SummaryObj;
			if (FJsonSerializer::Deserialize(SR, SummaryObj) && SummaryObj.IsValid())
			{
				PrevSummary = SummaryObj->GetObjectField(TEXT("categories"));
			}
		}
	}

	// 生成报告
	const FString Markdown = NexusFeedbackInternal::BuildMarkdown(Records, PrevSummary);

	// 时间戳用于文件名
	const FDateTime Now = FDateTime::UtcNow();
	const FString TsTag = FString::Printf(TEXT("%04d%02d%02d_%02d%02d%02d"),
		Now.GetYear(), Now.GetMonth(), Now.GetDay(),
		Now.GetHour(), Now.GetMinute(), Now.GetSecond());

	// 写 Markdown 报告
	const FString ReportFile = Dir / FString::Printf(TEXT("report_%s.md"), *TsTag);
	FFileHelper::SaveStringToFile(Markdown, *ReportFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	// 归档 jsonl
	IFileManager::Get().MakeDirectory(*ArchiveDir, /*Tree=*/true);
	const FString ArchiveFile = ArchiveDir / FString::Printf(TEXT("feedback_%s.jsonl"), *TsTag);
	IFileManager::Get().Copy(*ArchiveFile, *JsonlFile);

	// 构建本期 category 摘要，写 tool 维度摘要，生成 JSON 摘要副产物
	TSharedPtr<FJsonObject> CatSummary = NexusFeedbackInternal::BuildCategorySummary(Records);
	{
		// 按 tool 统计
		TMap<FString, int32> ToolCount;
		for (const auto& R : Records)
		{
			FString T;
			R->TryGetStringField(TEXT("tool"), T);
			if (!T.IsEmpty()) { int32& V = ToolCount.FindOrAdd(T); V++; }
		}
		TSharedPtr<FJsonObject> ToolSummary = MakeShared<FJsonObject>();
		for (const auto& P : ToolCount) ToolSummary->SetNumberField(P.Key, P.Value);

		// 写 report_<ts>.json
		TSharedPtr<FJsonObject> JsonSummary = MakeShared<FJsonObject>();
		JsonSummary->SetStringField(TEXT("timestamp"),  DateTimeToIso8601(Now));
		JsonSummary->SetNumberField(TEXT("totalCount"), Records.Num());
		JsonSummary->SetObjectField(TEXT("categories"), CatSummary);
		JsonSummary->SetObjectField(TEXT("tools"),      ToolSummary);
		FString JsonSummaryStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JW =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonSummaryStr);
		FJsonSerializer::Serialize(JsonSummary.ToSharedRef(), JW);
		const FString JsonReportFile = Dir / FString::Printf(TEXT("report_%s.json"), *TsTag);
		FFileHelper::SaveStringToFile(JsonSummaryStr, *JsonReportFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

		// 覆盖写 last_summary.json（包装在 categories 字段下，便于后续扩展）
		TSharedPtr<FJsonObject> LastSummaryObj = MakeShared<FJsonObject>();
		LastSummaryObj->SetStringField(TEXT("timestamp"), DateTimeToIso8601(Now));
		LastSummaryObj->SetObjectField(TEXT("categories"), CatSummary);
		FString LastSummaryStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> LW =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&LastSummaryStr);
		FJsonSerializer::Serialize(LastSummaryObj.ToSharedRef(), LW);
		FFileHelper::SaveStringToFile(LastSummaryStr, *LastSummaryFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	// 清空主 jsonl
	Clear();

	// 归档文件数量上限清理
	{
		const UNexusLinkSettings* S = UNexusLinkSettings::Get();
		const int32 MaxArchive = (S && S->MaxArchiveCount > 0) ? S->MaxArchiveCount : 20;
		TArray<FString> ArchiveFiles;
		IFileManager::Get().FindFiles(ArchiveFiles, *(ArchiveDir / TEXT("feedback_*.jsonl")), /*Files=*/true, /*Dirs=*/false);
		if (ArchiveFiles.Num() > MaxArchive)
		{
			// GetFileAgeSeconds 升序 = 最旧最大；取超出部分删除
			ArchiveFiles.Sort([&ArchiveDir](const FString& A, const FString& B)
			{
				const double AgeA = IFileManager::Get().GetFileAgeSeconds(*(ArchiveDir / A));
				const double AgeB = IFileManager::Get().GetFileAgeSeconds(*(ArchiveDir / B));
				return AgeA < AgeB; // 最新在前
			});
			for (int32 i = MaxArchive; i < ArchiveFiles.Num(); ++i)
			{
				IFileManager::Get().Delete(*(ArchiveDir / ArchiveFiles[i]));
			}
		}
	}

	return FPaths::ConvertRelativePathToFull(ReportFile);
}
