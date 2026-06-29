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
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "GenericPlatform/GenericPlatformProperties.h"
#include "Interfaces/IPluginManager.h"
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

	/** 读取 jsonl 文件为记录数组；文件不存在或为空时返回空数组。 */
	static TArray<TSharedPtr<FJsonObject>> LoadRecordsFromJsonl(const FString& JsonlFile)
	{
		TArray<TSharedPtr<FJsonObject>> Result;
		if (!IFileManager::Get().FileExists(*JsonlFile))
		{
			return Result;
		}
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *JsonlFile))
		{
			return Result;
		}
		return ParseLines(Lines);
	}

	static TArray<TSharedPtr<FJsonObject>> LoadCurrentRecords()
	{
		return LoadRecordsFromJsonl(FNexusFeedback::GetFeedbackDir() / TEXT("feedback.jsonl"));
	}

	/** 解析设置中的仓库字段，得到 issues/new 基础 URL。 */
	static FString ResolveIssueNewBaseUrl()
	{
		const UNexusLinkSettings* S = UNexusLinkSettings::Get();
		FString Input = S ? S->FeedbackIssueRepo.TrimStartAndEnd() : FString();
		if (Input.IsEmpty())
		{
			Input = TEXT("bytepine/NexusLink");
		}

		auto BuildFromOwnerRepo = [](const FString& OwnerRepo) -> FString
		{
			return FString::Printf(TEXT("https://github.com/%s/issues/new"), *OwnerRepo);
		};

		if (Input.Contains(TEXT("github.com"), ESearchCase::IgnoreCase))
		{
			FString OwnerRepo;
			const int32 HostIdx = Input.Find(TEXT("github.com"), ESearchCase::IgnoreCase);
			FString AfterHost = Input.Mid(HostIdx + FString(TEXT("github.com")).Len());
			AfterHost.TrimStartInline();
			if (AfterHost.StartsWith(TEXT("/"))) AfterHost = AfterHost.Mid(1);
			if (AfterHost.StartsWith(TEXT(":"))) AfterHost = AfterHost.Mid(1);
			TArray<FString> Parts;
			AfterHost.ParseIntoArray(Parts, TEXT("/"), true);
			if (Parts.Num() >= 2)
			{
				FString Repo = Parts[1];
				Repo.RemoveFromEnd(TEXT(".git"));
				int32 QPos = INDEX_NONE;
				if (Repo.FindChar(TEXT('?'), QPos)) Repo = Repo.Left(QPos);
				if (Repo.FindChar(TEXT('#'), QPos)) Repo = Repo.Left(QPos);
				OwnerRepo = Parts[0] / Repo;
				return BuildFromOwnerRepo(OwnerRepo);
			}
		}

		if (!Input.Contains(TEXT("://")))
		{
			return BuildFromOwnerRepo(Input);
		}

		return BuildFromOwnerRepo(TEXT("bytepine/NexusLink"));
	}

	/** 环境指纹行（Issue 正文头），可选接受记录集合以填充条数与时间窗。 */
	static FString BuildEnvironmentBlock(
		const TArray<TSharedPtr<FJsonObject>>* Records = nullptr)
	{
		FString PluginVer = TEXT("unknown");
		if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NexusLink")))
		{
			PluginVer = Plugin->GetDescriptor().VersionName;
			if (PluginVer.IsEmpty())
			{
				PluginVer = FString::FromInt(Plugin->GetDescriptor().Version);
			}
		}

		const UNexusLinkSettings* S = UNexusLinkSettings::Get();
		const FString ToolsMode = (S && S->ToolsListMode == ENexusToolsListMode::MultiTool)
			? TEXT("MultiTool") : TEXT("SearchMode");

		const FEngineVersion EngVer     = FEngineVersion::Current();
		const FString PlatformName      = FString(ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName()));
		const TCHAR*  ProjectName       = FApp::GetProjectName();
		FString Md;
		Md += TEXT("## 环境\n\n");
		Md += FString::Printf(
			TEXT("- UE %d.%d.%d · NexusLink %s · %s\n"),
			EngVer.GetMajor(), EngVer.GetMinor(), EngVer.GetPatch(),
			*PluginVer, *PlatformName);
		Md += FString::Printf(TEXT("- 项目: %s · ToolsListMode: %s\n"),
			ProjectName ? ProjectName : TEXT("(unknown)"), *ToolsMode);

		// 条数 + 时间窗（仅在有记录时输出）
		if (Records && Records->Num() > 0)
		{
			FString EarliestTs, LatestTs;
			for (const auto& R : *Records)
			{
				const FString Ts = GetStr(R, TEXT("ts"));
				if (Ts.IsEmpty()) continue;
				if (EarliestTs.IsEmpty() || Ts < EarliestTs) EarliestTs = Ts;
				if (LatestTs.IsEmpty()   || Ts > LatestTs)   LatestTs   = Ts;
			}
			Md += FString::Printf(TEXT("- 反馈条数: %d"), Records->Num());
			if (!EarliestTs.IsEmpty())
			{
				Md += FString::Printf(TEXT(" · 时间窗: %s ~ %s"), *EarliestTs, *LatestTs);
			}
			Md += TEXT("\n");
		}

		Md += TEXT("\n");
		return Md;
	}

	/** 加权优先级：category → int，值越大越优先作为最小复现代表。 */
	static int32 CategoryWeight(const FString& Cat)
	{
		if (Cat == TEXT("call_arg_invalid")) return 30;
		if (Cat == TEXT("call_fatal"))       return 30;
		if (Cat == TEXT("misuse"))           return 20;
		if (Cat == TEXT("schema_guess"))     return 20;
		if (Cat == TEXT("wrong_tool"))       return 20;
		if (Cat == TEXT("call_unknown"))     return 15;
		if (Cat == TEXT("call_disabled"))    return 10;
		if (Cat == TEXT("redundant_call"))   return  5;
		if (Cat == TEXT("slow_call"))        return  5;
		return 1;
	}

	/** 从记录生成 Issue 标题与正文（正文不含 URL 长度兜底截断）。 */
	static void BuildIssueDraftFromRecords(const TArray<TSharedPtr<FJsonObject>>& Records,
	                                       FString& OutTitle, FString& OutBody)
	{
		OutTitle.Reset();
		OutBody.Reset();
		if (Records.Num() == 0)
		{
			return;
		}

		// ── 分类计数 ──────────────────────────────────────────────────────────
		TMap<FString, int32> CatCount;
		for (const auto& R : Records)
		{
			const FString Cat = GetStr(R, TEXT("category"));
			if (!Cat.IsEmpty())
			{
				int32& V = CatCount.FindOrAdd(Cat);
				V++;
			}
		}

		TArray<TPair<FString, int32>> CatArr;
		for (const auto& P : CatCount) CatArr.Add(P);
		CatArr.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
		{
			const int32 WA = CategoryWeight(A.Key);
			const int32 WB = CategoryWeight(B.Key);
			if (WA != WB) return WA > WB;
			return A.Value > B.Value;
		});

		// ── 选最小复现代表记录（按权重）────────────────────────────────────
		TSharedPtr<FJsonObject> BestRecord;
		int32 BestWeight = -1;
		for (const auto& R : Records)
		{
			const FString Cat = GetStr(R, TEXT("category"));
			const int32 W = CategoryWeight(Cat);
			if (W > BestWeight)
			{
				BestWeight = W;
				BestRecord = R;
			}
		}

		// ── 标题：优先 [NexusLink] <capability>: <规则化错误> ─────────────
		if (BestRecord.IsValid())
		{
			const FString Cap = GetStr(BestRecord, TEXT("capability"));
			const FString Err = GetStr(BestRecord, TEXT("errorText"));
			if (!Cap.IsEmpty() && !Err.IsEmpty())
			{
				const FString NormErr = ::NormalizeForThrottle(Err);
				OutTitle = FString::Printf(TEXT("[NexusLink] %s: %s"), *Cap, *NormErr);
			}
			else if (!Cap.IsEmpty())
			{
				const FString Cat = GetStr(BestRecord, TEXT("category"));
				OutTitle = FString::Printf(TEXT("[NexusLink] %s (%s)"), *Cap, *Cat);
			}
		}

		// 回退：category×N 形式
		if (OutTitle.IsEmpty())
		{
			OutTitle = TEXT("[NexusLink Feedback] ");
			int32 TitleParts = 0;
			for (const auto& P : CatArr)
			{
				if (TitleParts >= 3) break;
				if (TitleParts > 0) OutTitle += TEXT(" · ");
				OutTitle += FString::Printf(TEXT("%s×%d"), *P.Key, P.Value);
				TitleParts++;
			}
		}
		if (OutTitle.Len() > 120) OutTitle = OutTitle.Left(117) + TEXT("...");

		// ── 环境 ──────────────────────────────────────────────────────────────
		OutBody += BuildEnvironmentBlock(&Records);

		// ── 最小复现（自动选取）──────────────────────────────────────────────
		if (BestRecord.IsValid())
		{
			OutBody += TEXT("## 最小复现（自动选取）\n\n");
			const auto AppendField = [&](const TCHAR* Label, const FString& Val)
			{
				if (!Val.IsEmpty())
				{
					const FString Snip = Val.Len() > 200 ? Val.Left(200) + TEXT("…") : Val;
					OutBody += FString::Printf(TEXT("- **%s**: `%s`\n"), Label, *Snip);
				}
			};
			AppendField(TEXT("时间"),         GetStr(BestRecord, TEXT("ts")));
			AppendField(TEXT("category"),     GetStr(BestRecord, TEXT("category")));
			AppendField(TEXT("tool"),         GetStr(BestRecord, TEXT("tool")));
			AppendField(TEXT("capability"),   GetStr(BestRecord, TEXT("capability")));
			AppendField(TEXT("errorText"),    GetStr(BestRecord, TEXT("errorText")));
			AppendField(TEXT("argsDigest"),   GetStr(BestRecord, TEXT("argsDigest")));
			AppendField(TEXT("attemptedArgs"),GetStr(BestRecord, TEXT("attemptedArgs")));
			AppendField(TEXT("actualError"),  GetStr(BestRecord, TEXT("actualError")));
			AppendField(TEXT("expectedField"),GetStr(BestRecord, TEXT("expectedField")));
			AppendField(TEXT("query"),        GetStr(BestRecord, TEXT("query")));
			OutBody += TEXT("\n");
		}

		// ── 摘要 ──────────────────────────────────────────────────────────────
		OutBody += FString::Printf(TEXT("## 摘要\n\n总条数：**%d**\n\n"), Records.Num());
		OutBody += TEXT("| category | 次数 |\n|---|---|\n");
		for (const auto& P : CatArr)
		{
			OutBody += FString::Printf(TEXT("| `%s` | %d |\n"), *P.Key, P.Value);
		}
		OutBody += TEXT("\n");

		// ── search_zero Top query（仅在 Issue 草稿中展示，Markdown §2 有更详细版本）──
		{
			TMap<FString, int32> QueryCount;
			for (const auto& R : Records)
			{
				if (GetStr(R, TEXT("category")) != TEXT("search_zero")) continue;
				const FString Q = GetStr(R, TEXT("query"));
				if (!Q.IsEmpty()) { int32& V = QueryCount.FindOrAdd(Q); V++; }
			}
			if (QueryCount.Num() > 0)
			{
				TArray<TPair<FString, int32>> QArr;
				for (const auto& P : QueryCount) QArr.Add(P);
				QArr.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
				{
					return A.Value > B.Value;
				});
				OutBody += TEXT("## 搜索无命中 Top Query\n\n");
				int32 Shown = 0;
				for (const auto& P : QArr)
				{
					if (++Shown > 5) break;
					OutBody += FString::Printf(TEXT("- `%s` ×%d\n"), *P.Key, P.Value);
				}
				OutBody += TEXT("\n");
			}
		}

		// ── Top Capability 误用（call_*）─────────────────────────────────────
		{
			TMap<FString, int32> CapTotal;
			TMap<FString, FString> CapSample;
			for (const auto& R : Records)
			{
				const FString Cat = GetStr(R, TEXT("category"));
				if (!Cat.StartsWith(TEXT("call_"))) continue;
				const FString Cap    = GetStr(R, TEXT("capability"));
				const FString CapKey = Cap.IsEmpty() ? TEXT("(unknown)") : Cap;
				int32& V = CapTotal.FindOrAdd(CapKey);
				V++;
				if (!CapSample.Contains(CapKey))
				{
					const FString Err = GetStr(R, TEXT("errorText"));
					if (!Err.IsEmpty()) CapSample.Add(CapKey, Err);
				}
			}
			if (CapTotal.Num() > 0)
			{
				OutBody += TEXT("## Top Capability 误用\n\n");
				TArray<TPair<FString, int32>> CapArr;
				for (const auto& P : CapTotal) CapArr.Add(P);
				CapArr.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
				{
					return A.Value > B.Value;
				});
				int32 Shown = 0;
				for (const auto& P : CapArr)
				{
					if (++Shown > 5) break;
					OutBody += FString::Printf(TEXT("- `%s` ×%d"), *P.Key, P.Value);
					if (const FString* Sample = CapSample.Find(P.Key))
					{
						const FString Snip = Sample->Len() > 100 ? Sample->Left(100) + TEXT("…") : *Sample;
						OutBody += FString::Printf(TEXT(" — %s"), *Snip);
					}
					OutBody += TEXT("\n");
				}
				OutBody += TEXT("\n");
			}
		}

		// ── AI 手动上报（结构化字段优先）────────────────────────────────────
		{
			struct FManualEntry
			{
				FString Category;
				FString AttemptedArgs;
				FString ActualError;
				FString ExpectedField;
				FString Note;
			};
			TArray<FManualEntry> ManualEntries;
			for (const auto& R : Records)
			{
				const FString Cat = GetStr(R, TEXT("category"));
				if (Cat != TEXT("wrong_tool") && Cat != TEXT("schema_guess") && Cat != TEXT("misuse")) continue;
				FManualEntry E;
				E.Category      = Cat;
				E.AttemptedArgs = GetStr(R, TEXT("attemptedArgs"));
				E.ActualError   = GetStr(R, TEXT("actualError"));
				E.ExpectedField = GetStr(R, TEXT("expectedField"));
				E.Note          = GetStr(R, TEXT("note"));
				ManualEntries.Add(E);
			}
			if (ManualEntries.Num() > 0)
			{
				OutBody += TEXT("## AI 上报\n\n");
				int32 Shown = 0;
				for (const FManualEntry& E : ManualEntries)
				{
					if (++Shown > 3) break;
					OutBody += FString::Printf(TEXT("**%s**\n"), *E.Category);
					if (!E.AttemptedArgs.IsEmpty())
					{
						const FString V = E.AttemptedArgs.Len() > 150 ? E.AttemptedArgs.Left(150) + TEXT("…") : E.AttemptedArgs;
						OutBody += FString::Printf(TEXT("- attemptedArgs: `%s`\n"), *V);
					}
					if (!E.ActualError.IsEmpty())
					{
						const FString V = E.ActualError.Len() > 150 ? E.ActualError.Left(150) + TEXT("…") : E.ActualError;
						OutBody += FString::Printf(TEXT("- actualError: `%s`\n"), *V);
					}
					if (!E.ExpectedField.IsEmpty())
					{
						OutBody += FString::Printf(TEXT("- expectedField: `%s`\n"), *E.ExpectedField);
					}
					if (!E.Note.IsEmpty())
					{
						const FString V = E.Note.Len() > 150 ? E.Note.Left(150) + TEXT("…") : E.Note;
						OutBody += FString::Printf(TEXT("- note: %s\n"), *V);
					}
					OutBody += TEXT("\n");
				}
			}
		}

		OutBody += TEXT("## 复现步骤\n\n（请补充）\n\n");
		OutBody += TEXT("---\n\n由 NexusLink 反馈系统自动生成。\n");
	}

	/** GitHub GET 预填 URL 安全长度：正文过长时截断并提示查看本地报告。 */
	static constexpr int32 GIssueBodyMaxChars = 3500;

	static FString TruncateIssueBodyForUrl(const FString& Body)
	{
		if (Body.Len() <= GIssueBodyMaxChars)
		{
			return Body;
		}
		return Body.Left(GIssueBodyMaxChars)
			+ TEXT("\n\n…（正文已截断，完整数据请导出 Markdown 报告或查看 .nexus-feedback/）");
	}

	static FString BuildIssuePrefillUrlInternal(const FString& Title, const FString& Body)
	{
		const FString BaseUrl = ResolveIssueNewBaseUrl();
		const FString EncTitle = FGenericPlatformHttp::UrlEncode(Title);
		const FString EncBody  = FGenericPlatformHttp::UrlEncode(TruncateIssueBodyForUrl(Body));
		return FString::Printf(TEXT("%s?title=%s&body=%s"), *BaseUrl, *EncTitle, *EncBody);
	}

	/** 在 Markdown 报告末尾追加 Issue 预填段。 */
	static void AppendIssuePrefillSection(FString& Md, const TArray<TSharedPtr<FJsonObject>>& Records)
	{
		if (Records.Num() == 0)
		{
			return;
		}

		FString Title, Body;
		BuildIssueDraftFromRecords(Records, Title, Body);
		const FString PrefillUrl = BuildIssuePrefillUrlInternal(Title, Body);
		const FString BaseUrl = ResolveIssueNewBaseUrl();

		Md += TEXT("---\n\n");
		Md += TEXT("## §8 GitHub Issue 预填\n\n");
		Md += FString::Printf(
			TEXT("目标仓库：[issues/new](%s)\n\n"),
			*BaseUrl);
		Md += TEXT("在设置面板点击 **创建 GitHub Issue** 可在浏览器打开预填页；或复制下方草稿：\n\n");
		Md += FString::Printf(TEXT("**标题：** %s\n\n"), *Title);
		Md += TEXT("```markdown\n");
		Md += Body;
		Md += TEXT("```\n\n");
		if (PrefillUrl.Len() <= 6000)
		{
			Md += FString::Printf(TEXT("预填链接：<%s>\n"), *PrefillUrl);
		}
		else
		{
			Md += TEXT("预填链接过长，请使用设置面板 **创建 GitHub Issue** 按钮打开浏览器。\n");
		}
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

		// §8 错误指纹 Top 5（跨 capability 去重聚合）
		{
			TMap<FString, int32> FingerprintCount;
			TMap<FString, FString> FingerprintSample;  // 指纹 → 代表原始 errorText
			for (const auto& R : Records)
			{
				const FString Cat = GetStr(R, TEXT("category"));
				if (!Cat.StartsWith(TEXT("call_")) && Cat != TEXT("call_arg_invalid")
					&& Cat != TEXT("call_fatal")) continue;
				const FString Err = GetStr(R, TEXT("errorText"));
				if (Err.IsEmpty()) continue;
				const FString FP = NormalizeForThrottle(Err);
				int32& V = FingerprintCount.FindOrAdd(FP);
				V++;
				if (!FingerprintSample.Contains(FP)) FingerprintSample.Add(FP, Err);
			}
			Md += TEXT("## §8 错误指纹 Top 5（去重聚合）\n\n");
			if (FingerprintCount.Num() == 0)
			{
				Md += TEXT("_无数据_\n\n");
			}
			else
			{
				TArray<TPair<FString, int32>> FPArr;
				for (const auto& P : FingerprintCount) FPArr.Add(P);
				FPArr.Sort([](const TPair<FString,int32>& A, const TPair<FString,int32>& B)
				{
					return A.Value > B.Value;
				});
				Md += TEXT("| 指纹（规则化） | 次数 | 代表错误样本 |\n|---|---|---|\n");
				int32 Cnt = 0;
				for (const auto& P : FPArr)
				{
					if (++Cnt > 5) break;
					const FString FPLabel = P.Key.Len() > 50 ? P.Key.Left(50) + TEXT("…") : P.Key;
					const FString Sample  = FingerprintSample.FindRef(P.Key);
					const FString Snip    = Sample.Len() > 80 ? Sample.Left(80) + TEXT("…") : Sample;
					Md += FString::Printf(TEXT("| `%s` | %d | %s |\n"), *FPLabel, P.Value, *Snip);
				}
				Md += TEXT("\n");
			}
		}

		AppendIssuePrefillSection(Md, Records);
		return Md;
	}
} // namespace NexusFeedbackInternal

bool FNexusFeedback::BuildIssueDraft(FIssueDraft& OutDraft)
{
	const TArray<TSharedPtr<FJsonObject>> Records = NexusFeedbackInternal::LoadCurrentRecords();
	if (Records.Num() == 0)
	{
		OutDraft.Title.Reset();
		OutDraft.Body.Reset();
		return false;
	}
	NexusFeedbackInternal::BuildIssueDraftFromRecords(Records, OutDraft.Title, OutDraft.Body);
	return !OutDraft.Title.IsEmpty();
}

FString FNexusFeedback::BuildIssuePrefillUrl(const FString& Title, const FString& Body)
{
	return NexusFeedbackInternal::BuildIssuePrefillUrlInternal(Title, Body);
}

FString FNexusFeedback::BuildRedactedArgsSnapshot(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid() || Args->Values.Num() == 0)
	{
		return FString();
	}

	auto IsSensitiveKey = [](const FString& Key) -> bool
	{
		const FString Lower = Key.ToLower();
		return Lower.Contains(TEXT("password"))
			|| Lower.Contains(TEXT("token"))
			|| Lower.Contains(TEXT("secret"))
			|| Lower.Contains(TEXT("apikey"))
			|| Lower.Contains(TEXT("api_key"));
	};

	TSharedPtr<FJsonObject> Redacted = MakeShared<FJsonObject>();
	for (const auto& Pair : Args->Values)
	{
		// FJsonObject::Values key 在 UE 5.8+ 为 UE::FSharedString，不能隐式转 FString；
		// operator*() 两者均返回 const TCHAR*，用此方式跨版本兼容。
		const FString PairKey(*Pair.Key);
		if (IsSensitiveKey(PairKey))
		{
			Redacted->SetStringField(PairKey, TEXT("<redacted>"));
		}
		else
		{
			Redacted->SetField(PairKey, Pair.Value);
		}
	}

	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Redacted.ToSharedRef(), W);

	if (Out.Len() > 200)
	{
		Out = Out.Left(200) + TEXT("…");
	}
	return Out;
}

bool FNexusFeedback::OpenIssuePrefillInBrowser()
{
	FIssueDraft Draft;
	if (!BuildIssueDraft(Draft))
	{
		return false;
	}
	const FString Url = BuildIssuePrefillUrl(Draft.Title, Draft.Body);
	FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
	return true;
}

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
