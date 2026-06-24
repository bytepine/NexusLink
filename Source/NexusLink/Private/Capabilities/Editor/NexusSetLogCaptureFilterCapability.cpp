// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/NexusSetLogCaptureFilterCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Editor/NexusLogCapture.h"
#include "NexusLinkSettings.h"
#include "NexusMcpTool.h"

void FSetLogCaptureFilterCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("set_log_capture_filter");
	Out.Description = TEXT("配置写入缓冲的日志分类。空=全部；影响 get_output_log。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("categories"), FNexusSchema::StrArr(
			TEXT("要捕获的日志分类子串。空数组=全部。"
			     "示例：[\"LogTemp\"]、[\"LogBlueprintUserMessages\",\"LogNexusLink\"]、[]")))
		.Required({ TEXT("categories") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("configure"), TEXT("whitelist"), TEXT("category"), TEXT("include"), TEXT("exclude") };
	Out.RelatedCapabilities = { TEXT("get_output_log") };
}

FCapabilityResult FSetLogCaptureFilterCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		if (!Arguments.IsValid())
		{
			OutError = TEXT("参数无效");
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* CatArrPtr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("categories"), CatArrPtr) || !CatArrPtr)
		{
			OutError = TEXT("缺少 categories（字符串数组）");
			return;
		}

		TArray<FString> Categories;
		for (const TSharedPtr<FJsonValue>& V : *CatArrPtr)
		{
			if (V.IsValid() && V->Type == EJson::String)
			{
				const FString Cat = V->AsString().TrimStartAndEnd();
				if (!Cat.IsEmpty()) Categories.Add(Cat);
			}
		}

		FNexusLogCapture::Get().SetCategoryWhitelist(Categories);

		UNexusLinkSettings* Settings = UNexusLinkSettings::Get();
		if (Settings)
		{
			Settings->LogCaptureCategories = Categories;
			Settings->SaveConfig();
		}

		TArray<TSharedPtr<FJsonValue>> CatArr;
		for (const FString& Cat : Categories)
			CatArr.Add(MakeShared<FJsonValueString>(Cat));

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetBoolField(TEXT("success"), true);
		OutEntry->SetStringField(TEXT("captureFilter"), Categories.Num() == 0 ? TEXT("all") : TEXT("custom"));
		OutEntry->SetArrayField(TEXT("categories"), CatArr);
		OutEntry->SetStringField(TEXT("note"),
			Categories.Num() == 0
				? TEXT("Capturing ALL log categories")
				: FString::Printf(TEXT("正在捕获 %d 个日志分类过滤"), Categories.Num()));
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FSetLogCaptureFilterCapability)

#endif // WITH_EDITOR
