// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

/** 旧版 WebSocketNetworking 不提供消息结束标志；按 JSON 根对象边界拼接原始 UTF-8，完整后才解码。 */
struct FNexusJsonMessageBuffer final
{
	TArray<uint8> Bytes;
	int32 Depth = 0;
	bool bInString = false;
	bool bEscaped = false;
	bool bRejected = false;

	bool Append(const uint8* Data, int32 Size, int32 MaxBytes, TArray<TArray<uint8>>& Messages)
	{
		if (bRejected) return true; // 超限连接必须重新连接，不能把尾部当成新请求。
		for (int32 ByteIndex = 0; ByteIndex < Size; ++ByteIndex)
		{
			const uint8 Character = Data[ByteIndex];
			if (Bytes.Num() == 0 && (Character == ' ' || Character == '\r' || Character == '\n' || Character == '\t')) continue;
			if (Messages.Num() >= 128)
			{ Bytes.Empty(); bRejected = true; return false; }
			if (Bytes.Num() == 0 && Character != '{' && Character != '[')
			{
				if (Size - ByteIndex > MaxBytes) { bRejected = true; return false; }
				// 非 JSON 根对象交给协议解析器一次性报错，避免逐字节产生大量错误响应。
				Messages.Add(TArray<uint8>(Data + ByteIndex, Size - ByteIndex));
				return true;
			}
			if (Bytes.Num() >= MaxBytes)
			{
				Bytes.Empty(); bRejected = true;
				return false;
			}
			Bytes.Add(Character);
			if (bInString)
			{
				if (bEscaped) bEscaped = false;
				else if (Character == '\\') bEscaped = true;
				else if (Character == '"') bInString = false;
			}
			else if (Character == '"') bInString = true;
			else if (Character == '{' || Character == '[') ++Depth;
			else if (Character == '}' || Character == ']') --Depth;
			if (!bInString && Depth <= 0)
			{
				Messages.Add(MoveTemp(Bytes));
				Bytes.Reset(); Depth = 0;
			}
		}
		return true;
	}
};
