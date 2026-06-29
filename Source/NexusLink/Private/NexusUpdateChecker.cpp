// Copyright byteyang. All Rights Reserved.

#include "NexusUpdateChecker.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Async/Async.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusUpdateChecker, Log, All);

namespace
{
	// GitHub 官方「最新 Release」重定向端点（非 REST API，无速率限制，无需 Auth）。
	// curl 自动跟随 302 至 .../releases/tag/<tag>，再从落地页 HTML 解析 tag。
	static const TCHAR* GReleasesLatestUrl =
		TEXT("https://github.com/bytepine/NexusLink/releases/latest");

	/** 从 .../releases/tag/<tag> 路径起始位置提取 tag（遇到引号/尖括号/查询符等终止）。 */
	static FString ExtractTagAfter(const FString& Html, int32 From)
	{
		static const FString Marker = TEXT("/releases/tag/");
		const int32 Idx = Html.Find(Marker, ESearchCase::IgnoreCase, ESearchDir::FromStart, From);
		if (Idx == INDEX_NONE) { return FString(); }

		const int32 Start = Idx + Marker.Len();
		int32 End = Start;
		while (End < Html.Len())
		{
			const TCHAR C = Html[End];
			if (C == TEXT('"') || C == TEXT('\'') || C == TEXT('<') || C == TEXT('>')
				|| C == TEXT('?') || C == TEXT('&') || C == TEXT(' ')
				|| C == TEXT('\n') || C == TEXT('\r') || C == TEXT('\\'))
			{
				break;
			}
			++End;
		}
		return Html.Mid(Start, End - Start);
	}

	/**
	 * 从 Release 落地页 HTML 中解析 GitHub 标记的「Latest」版本 tag。
	 * 优先 og:url / canonical（唯一且稳定），兜底取全文首个 /releases/tag/。
	 * 返回去除前导 "v" 的版本号；未找到时返回空字符串。
	 */
	static FString ParseLatestTagFromReleasePage(const FString& Html)
	{
		FString Tag;

		const int32 OgIdx = Html.Find(TEXT("og:url"), ESearchCase::IgnoreCase);
		if (OgIdx != INDEX_NONE)
		{
			Tag = ExtractTagAfter(Html, OgIdx);
		}
		if (Tag.IsEmpty())
		{
			const int32 CanonIdx = Html.Find(TEXT("rel=\"canonical\""), ESearchCase::IgnoreCase);
			if (CanonIdx != INDEX_NONE)
			{
				Tag = ExtractTagAfter(Html, CanonIdx);
			}
		}
		if (Tag.IsEmpty())
		{
			Tag = ExtractTagAfter(Html, 0);
		}

		Tag = Tag.TrimStartAndEnd();
		if (Tag.StartsWith(TEXT("v"), ESearchCase::IgnoreCase))
		{
			Tag = Tag.Mid(1);
		}
		return Tag;
	}
}

FString FNexusUpdateChecker::GetCurrentVersion()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NexusLink"));
	if (!Plugin.IsValid())
	{
		return TEXT("unknown");
	}
	const FString VersionFile = FPaths::Combine(Plugin->GetBaseDir(), TEXT("VERSION"));
	FString Version;
	if (FFileHelper::LoadFileToString(Version, *VersionFile))
	{
		return Version.TrimStartAndEnd();
	}
	return TEXT("unknown");
}

bool FNexusUpdateChecker::IsNewerVersion(const FString& A, const FString& B)
{
	TArray<FString> PartsA, PartsB;
	A.ParseIntoArray(PartsA, TEXT("."), true);
	B.ParseIntoArray(PartsB, TEXT("."), true);

	const int32 Len = FMath::Max(PartsA.Num(), PartsB.Num());
	for (int32 i = 0; i < Len; ++i)
	{
		const int32 Va = (i < PartsA.Num()) ? FCString::Atoi(*PartsA[i]) : 0;
		const int32 Vb = (i < PartsB.Num()) ? FCString::Atoi(*PartsB[i]) : 0;
		if (Va != Vb)
		{
			return Va > Vb;
		}
	}
	return false; // 相等视为"不是更新"
}

void FNexusUpdateChecker::CheckAsync(
	TFunction<void(bool bHasUpdate, FString LatestVersion, FString CurrentVersion)> OnSuccess,
	TFunction<void(FString ErrorDetail)> OnError)
{
	FString CurrentVersion = GetCurrentVersion();

	auto Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(GReleasesLatestUrl);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("User-Agent"), TEXT("NexusLink-UpdateChecker/") + CurrentVersion);

	Request->OnProcessRequestComplete().BindLambda(
		[CurrentVersion, OnSuccess = MoveTemp(OnSuccess), OnError = MoveTemp(OnError)]
		(FHttpRequestPtr /*Req*/, FHttpResponsePtr Response, bool bConnected) mutable
		{
			auto DispatchError = [&](FString Detail)
			{
				UE_LOG(LogNexusUpdateChecker, Warning, TEXT("%s"), *Detail);
				if (OnError)
				{
					TFunction<void(FString)> Cb = MoveTemp(OnError);
					AsyncTask(ENamedThreads::GameThread,
						[Cb = MoveTemp(Cb), Detail = MoveTemp(Detail)]() { Cb(Detail); });
				}
			};

			if (!bConnected)
			{
				DispatchError(TEXT("无法连接到 github.com（连接超时或被防火墙拦截）"));
				return;
			}
			if (!Response.IsValid())
			{
				DispatchError(TEXT("HTTP 响应无效"));
				return;
			}
			const int32 Code = Response->GetResponseCode();
			if (Code != 200)
			{
				DispatchError(FString::Printf(TEXT("releases/latest 返回 HTTP %d"), Code));
				return;
			}

			// 从重定向后的 Release 落地页解析 GitHub 标记的最新版本
			const FString LatestVersion = ParseLatestTagFromReleasePage(Response->GetContentAsString());
			if (LatestVersion.IsEmpty())
			{
				DispatchError(TEXT("无法从 releases/latest 页面解析版本信息（可能尚无任何 Release）"));
				return;
			}

			const bool bHasUpdate = IsNewerVersion(LatestVersion, CurrentVersion);

			TFunction<void(bool, FString, FString)> Cb = MoveTemp(OnSuccess);
			AsyncTask(ENamedThreads::GameThread,
				[bHasUpdate, LatestVersion, CurrentVersion, Cb = MoveTemp(Cb)]()
				{
					Cb(bHasUpdate, LatestVersion, CurrentVersion);
				}
			);
		}
	);

	Request->ProcessRequest();
}
