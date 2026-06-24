// Copyright byteyang. All Rights Reserved.

#include "Tools/NexusMcpToolSubmitFeedback.h"
#include "NexusFeedback.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpToolRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/DateTime.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

void FNexusMcpToolSubmitFeedback::BuildDefinition(FNexusMcpToolDefinition& Out) const
{
	Out.Name        = TEXT("submit_feedback");
	Out.Description = TEXT("上报工具/Capability 使用摩擦。触发：重试≥2 次无进展、找不到合适 Capability、Schema 需猜测、被迫串行≥3 次。category：wrong_tool|misuse|schema_guess|search_zero|search_overflow|other。优先填 attemptedArgs/actualError/expectedField。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("category"),
			FNexusSchema::Enum(TEXT("反馈分类"),
				{
					TEXT("wrong_tool"),
					TEXT("misuse"),
					TEXT("schema_guess"),
					TEXT("search_zero"),
					TEXT("search_overflow"),
					TEXT("other"),
				}))
		.Prop(TEXT("note"),
			FNexusSchema::Str(TEXT("问题自由文本描述（建议填写）")))
		.Prop(TEXT("tool"),
			FNexusSchema::Str(TEXT("涉及的 MCP 工具名")))
		.Prop(TEXT("capability"),
			FNexusSchema::Str(TEXT("涉及的 Capability 名称")))
		.Prop(TEXT("query"),
			FNexusSchema::Str(TEXT("引发问题的 search_capabilities 查询词")))
		.Prop(TEXT("attemptedArgs"),
			FNexusSchema::Str(TEXT("触发问题的参数摘要")))
		.Prop(TEXT("actualError"),
			FNexusSchema::Str(TEXT("实际收到的错误信息片段")))
		.Prop(TEXT("expectedField"),
			FNexusSchema::Str(TEXT("缺失、歧义或需猜测的字段名")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
}

FNexusMcpToolResult FNexusMcpToolSubmitFeedback::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	FNexusMcpToolResult Result;
	const TSharedPtr<FJsonObject> Args = Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>();

	FString Category;
	if (!Args->TryGetStringField(TEXT("category"), Category) || Category.IsEmpty())
	{
		Result.bIsError = true;
		Result.ErrorText = TEXT("缺少必填字段：category");
		return Result;
	}

	FNexusFeedback::FFields Fields;
	Args->TryGetStringField(TEXT("tool"),          Fields.Tool);
	Args->TryGetStringField(TEXT("capability"),    Fields.Capability);
	Args->TryGetStringField(TEXT("query"),         Fields.Query);
	Args->TryGetStringField(TEXT("note"),          Fields.Note);
	Args->TryGetStringField(TEXT("attemptedArgs"), Fields.AttemptedArgs);
	Args->TryGetStringField(TEXT("actualError"),   Fields.ActualError);
	Args->TryGetStringField(TEXT("expectedField"), Fields.ExpectedField);

	FNexusFeedback::RecordManual(Category, Fields);

	// 返回确认信息
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("ok"),       true);
	Out->SetStringField(TEXT("recorded"), FDateTime::UtcNow().ToIso8601());
	Out->SetStringField(TEXT("category"), Category);

	Result.StructuredContent = Out;
	FString OutStr;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
	FJsonSerializer::Serialize(Out.ToSharedRef(), W);
	Result.OutputText = OutStr;
	return Result;
}

REGISTER_MCP_TOOL(FNexusMcpToolSubmitFeedback)
