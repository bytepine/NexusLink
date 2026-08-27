// Copyright byteyang. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 本机共享 MCP 鉴权 token。
 * 落盘 {LocalAppData|Application Support|.config}/NexusLink/mcp-auth-token，
 * 同机 UE / Desktop / Rider / VSCode 复用同一份。
 */
struct FNexusMcpAuth
{
	/** 文件已有则读取；否则生成（或采纳 Seed）并写入。 */
	static FString LoadOrCreateMachineToken(const FString& Seed = FString());

	/**
	 * PresentedRaw 为 Bearer 值（可逗号/空白分隔多个 token）。
	 * 命中 MachineToken 或 ExtraTokens 中任一项即通过。
	 */
	static bool IsTokenAccepted(const FString& PresentedRaw, const FString& MachineToken, const FString& ExtraTokens);
};
