// Copyright byteyang. All Rights Reserved.

#include "NexusFeedback.h"
#include "NexusFeedbackInternal.h"
#include "NexusLinkSettings.h"
#include "Utils/NexusJsonUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/CriticalSection.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "GenericPlatform/GenericPlatformProperties.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Regex.h"

// ── 进程内节流表（category+capability+errorPrefix → 上次记录时间）─────────────
static FCriticalSection GFeedbackMutex;
static TMap<FString, FDateTime> GThrottleMap;
static constexpr double GThrottleWindowSec = 30.0;

/** 丢掉已过节流窗口的条目，避免长期会话无界增长。调用方须已持 GFeedbackMutex。 */
static void EvictStaleThrottleEntries(const FDateTime& Now)
{
	for (auto It = GThrottleMap.CreateIterator(); It; ++It)
	{
		if ((Now - It.Value()).GetTotalSeconds() >= GThrottleWindowSec)
		{
			It.RemoveCurrent();
		}
	}
}

FString FNexusFeedbackInternal::NormalizeForThrottle(const FString& Input)
{
	FString Result = Input;
	// 引号字符串 → *
	{
		const FRegexPattern Pat(TEXT("\"[^\"]*\""));
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
		const FRegexPattern Pat(TEXT("/(?:Game|Engine|Script|Plugin|Temp)[A-Za-z0-9_/\\.]*"));
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
		const FRegexPattern Pat(TEXT("\\d+"));
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
	const FString NormErr = FNexusFeedbackInternal::NormalizeForThrottle(Fields.ErrorText);
	return Category + TEXT("|") + Fields.Capability + TEXT("|") + NormErr;
}

FString FNexusFeedbackInternal::DateTimeToIso8601(const FDateTime& DT)
{
	return FString::Printf(TEXT("%04d-%02d-%02dT%02d:%02d:%02dZ"),
		DT.GetYear(), DT.GetMonth(), DT.GetDay(),
		DT.GetHour(), DT.GetMinute(), DT.GetSecond());
}

FString FNexusFeedbackInternal::GetLivePluginVersion()
{
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NexusLink")))
	{
		FString Ver = Plugin->GetDescriptor().VersionName;
		if (Ver.IsEmpty())
		{
			Ver = FString::FromInt(Plugin->GetDescriptor().Version);
		}
		if (!Ver.IsEmpty())
		{
			return Ver;
		}
	}
	return TEXT("unknown");
}

FString FNexusFeedbackInternal::GetLiveUeVersion()
{
	const FEngineVersion EngVer = FEngineVersion::Current();
	return FString::Printf(TEXT("%d.%d.%d"),
		EngVer.GetMajor(), EngVer.GetMinor(), EngVer.GetPatch());
}

FString FNexusFeedbackInternal::GetLiveToolsListMode()
{
	const UNexusLinkSettings* S = UNexusLinkSettings::Get();
	return (S && S->ToolsListMode == ENexusToolsListMode::MultiTool)
		? TEXT("MultiTool") : TEXT("SearchMode");
}

/** 将一条反馈序列化为 JSON 字符串（condensed 单行）。 */
static FString BuildJsonLine(const FString& Kind, const FString& Category,
                              const FNexusFeedback::FFields& Fields)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("ts"),       FNexusFeedbackInternal::DateTimeToIso8601(FDateTime::UtcNow()));
	Obj->SetStringField(TEXT("kind"),     Kind);
	Obj->SetStringField(TEXT("category"), Category);

	// 落盘时写入环境指纹，归档后仍可追溯当时版本（不依赖导出时进程）
	Obj->SetStringField(TEXT("pluginVersion"), FNexusFeedbackInternal::GetLivePluginVersion());
	Obj->SetStringField(TEXT("ueVersion"),     FNexusFeedbackInternal::GetLiveUeVersion());
	Obj->SetStringField(TEXT("toolsListMode"), FNexusFeedbackInternal::GetLiveToolsListMode());
	Obj->SetStringField(TEXT("platform"),
		FString(ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName())));

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
	if (!Fields.Proxy.IsEmpty())
		Obj->SetStringField(TEXT("proxy"), Fields.Proxy);
	if (!Fields.AttemptedArgs.IsEmpty())
		Obj->SetStringField(TEXT("attemptedArgs"), Fields.AttemptedArgs.Left(200));
	if (!Fields.ActualError.IsEmpty())
		Obj->SetStringField(TEXT("actualError"), Fields.ActualError.Left(200));
	if (!Fields.ExpectedField.IsEmpty())
		Obj->SetStringField(TEXT("expectedField"), Fields.ExpectedField);

	return FNexusJsonUtils::SerializeCondensed(Obj);
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

	// 节流：同 key 30 秒内只记一次；顺带按窗口淘汰过期 key
	const FString Key = BuildThrottleKey(Category, Fields);
	{
		FScopeLock Lock(&GFeedbackMutex);
		const FDateTime Now = FDateTime::UtcNow();
		EvictStaleThrottleEntries(Now);
		if (const FDateTime* Last = GThrottleMap.Find(Key))
		{
			if ((Now - *Last).GetTotalSeconds() < GThrottleWindowSec)
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

	FString Out = FNexusJsonUtils::SerializeCondensed(Redacted);

	if (Out.Len() > 200)
	{
		Out = Out.Left(200) + TEXT("…");
	}
	return Out;
}
