// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** 仅接收截图实现显式提供的可信文件，不从业务 JSON 或用户参数提取路径。 */
struct NEXUSLINK_API FNexusMcpImageUtils final
{
	/** 保留已有 text/structuredContent；单响应最多 4 张、合计 4 MiB 原始图片。返回 base64 字符数。 */
	static int32 AppendContent(const TArray<FString>& GeneratedFiles, const TSharedPtr<FJsonObject>& Result);
};
