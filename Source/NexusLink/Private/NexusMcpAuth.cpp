// Copyright byteyang. All Rights Reserved.

#include "NexusMcpAuth.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"

#if PLATFORM_MAC || PLATFORM_LINUX
#include <sys/stat.h>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNexusMcpAuth, Log, All);

static bool IsValidAuthToken(const FString& S)
{
	const int32 N = S.Len();
	if (N < 32 || N > 128)
	{
		return false;
	}
	for (int32 i = 0; i < N; ++i)
	{
		const TCHAR C = S[i];
		const bool bHex = (C >= '0' && C <= '9')
			|| (C >= 'a' && C <= 'f')
			|| (C >= 'A' && C <= 'F');
		if (!bHex)
		{
			return false;
		}
	}
	return true;
}

static FString GenerateAuthToken()
{
	return (FGuid::NewGuid().ToString(EGuidFormats::Digits)
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits)).ToLower();
}

static FString GetMachineAuthTokenPath()
{
#if PLATFORM_WINDOWS
	FString Base = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
	if (Base.IsEmpty())
	{
		Base = FPlatformProcess::UserSettingsDir();
	}
#elif PLATFORM_MAC
	const FString Base = FPaths::Combine(FPlatformProcess::UserHomeDir(), TEXT("Library/Application Support"));
#else
	FString Base = FPlatformMisc::GetEnvironmentVariable(TEXT("XDG_CONFIG_HOME"));
	if (Base.IsEmpty())
	{
		Base = FPaths::Combine(FPlatformProcess::UserHomeDir(), TEXT(".config"));
	}
#endif
	return FPaths::Combine(Base, TEXT("NexusLink"), TEXT("mcp-auth-token"));
}

// token 是长期凭证，POSIX 上默认 0644 同机其他用户可读，收敛为 0600
static void RestrictTokenFilePermissions(const FString& Path)
{
#if PLATFORM_MAC || PLATFORM_LINUX
	chmod(TCHAR_TO_UTF8(*Path), S_IRUSR | S_IWUSR);
#endif
}

static bool TryReadTokenFile(const FString& Path, FString& OutToken)
{
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *Path))
	{
		return false;
	}
	Raw.TrimStartAndEndInline();
	if (!IsValidAuthToken(Raw))
	{
		return false;
	}
	OutToken = Raw.ToLower();
	return true;
}

FString FNexusMcpAuth::LoadOrCreateMachineToken(const FString& Seed)
{
	const FString Path = GetMachineAuthTokenPath();
	FString Existing;
	if (TryReadTokenFile(Path, Existing))
	{
		return Existing;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (PlatformFile.FileExists(*Path))
	{
		PlatformFile.DeleteFile(*Path);
	}

	FString TrimmedSeed = Seed;
	TrimmedSeed.TrimStartAndEndInline();
	const FString Token = IsValidAuthToken(TrimmedSeed) ? TrimmedSeed.ToLower() : GenerateAuthToken();

	PlatformFile.CreateDirectoryTree(*FPaths::GetPath(Path));

	const FString Tmp = Path + FString::Printf(TEXT(".%u.tmp"), FPlatformProcess::GetCurrentProcessId());
	if (!FFileHelper::SaveStringToFile(Token, *Tmp))
	{
		// 落盘失败时 token 只存在于本进程内存：每次启动都会变，跨机/跨端配置随之失效
		UE_LOG(LogNexusMcpAuth, Warning,
			TEXT("无法写入本机鉴权 token（%s）：本次仅内存有效，重启后 token 会变，跨机配置需重新复制"), *Path);
		return Token;
	}

	RestrictTokenFilePermissions(Tmp);

	if (!PlatformFile.MoveFile(*Path, *Tmp))
	{
		PlatformFile.DeleteFile(*Tmp);
		// 并发创建时以先落盘者为准
		FString Winner;
		if (TryReadTokenFile(Path, Winner))
		{
			return Winner;
		}
		UE_LOG(LogNexusMcpAuth, Warning,
			TEXT("无法写入本机鉴权 token（%s）：本次仅内存有效，重启后 token 会变，跨机配置需重新复制"), *Path);
		return Token;
	}

	UE_LOG(LogNexusMcpAuth, Log, TEXT("本机鉴权 token 已就绪：%s"), *Path);
	return Token;
}

static bool TokensEqual(const FString& A, const FString& B)
{
	if (A.Len() != B.Len())
	{
		return false;
	}
	int32 Acc = 0;
	for (int32 i = 0; i < A.Len(); ++i)
	{
		Acc |= static_cast<int32>(A[i]) ^ static_cast<int32>(B[i]);
	}
	return Acc == 0;
}

void FNexusMcpAuth::ParseAuthTokens(const FString& Raw, TArray<FString>& Out)
{
	// 不能用 ParseIntoArrayWS(TEXT(",;"))：其 extra delim 是「单个分隔串」而非字符集，
	// 只会拆字面 ",;"，单独的逗号/分号不生效。
	static const TCHAR* const Delims[] = {
		TEXT(" "), TEXT("\t"), TEXT("\r"), TEXT("\n"), TEXT(","), TEXT(";"),
	};
	TArray<FString> Parts;
	Raw.ParseIntoArray(Parts, Delims, UE_ARRAY_COUNT(Delims), true);
	for (const FString& Part : Parts)
	{
		FString T = Part;
		T.TrimStartAndEndInline();
		if (!IsValidAuthToken(T))
		{
			continue;
		}
		T = T.ToLower();
		if (!Out.Contains(T))
		{
			Out.Add(T);
		}
	}
}

bool FNexusMcpAuth::IsTokenAccepted(const FString& PresentedRaw, const FString& MachineToken, const FString& ExtraTokens)
{
	TArray<FString> Presented;
	ParseAuthTokens(PresentedRaw, Presented);
	if (Presented.Num() == 0)
	{
		return false;
	}
	TArray<FString> Accepted;
	FString Machine = MachineToken;
	Machine.TrimStartAndEndInline();
	if (IsValidAuthToken(Machine))
	{
		Accepted.Add(Machine.ToLower());
	}
	ParseAuthTokens(ExtraTokens, Accepted);
	for (const FString& P : Presented)
	{
		for (const FString& A : Accepted)
		{
			if (TokensEqual(P, A))
			{
				return true;
			}
		}
	}
	return false;
}
