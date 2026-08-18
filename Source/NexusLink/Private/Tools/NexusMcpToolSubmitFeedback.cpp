// Copyright byteyang. All Rights Reserved.

#include "Tools/NexusMcpToolSubmitFeedback.h"
#include "NexusFeedback.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpToolRegistry.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusArgs.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/DateTime.h"

void FNexusMcpToolSubmitFeedback::BuildDefinition(FNexusMcpToolDefinition& Out) const
{
	Out.Name        = TEXT("submit_feedback");
	Out.Description = TEXT("Report tool/capability friction. Trigger: retry >=2 with no progress, no suitable capability, schema guessing, or forced serial calls >=3. category: wrong_tool|misuse|schema_guess|search_zero|search_overflow|other. Prefer attemptedArgs/actualError/expectedField over long note.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("category"),
			FNexusSchema::Enum(TEXT("Feedback category"),
				{
					TEXT("wrong_tool"),
					TEXT("misuse"),
					TEXT("schema_guess"),
					TEXT("search_zero"),
					TEXT("search_overflow"),
					TEXT("other"),
				}))
		.Prop(TEXT("note"),
			FNexusSchema::Str(TEXT("Free-text problem description (recommended)")))
		.Prop(TEXT("tool"),
			FNexusSchema::Str(TEXT("MCP tool name involved")))
		.Prop(TEXT("capability"),
			FNexusSchema::Str(TEXT("Capability name involved")))
		.Prop(TEXT("query"),
			FNexusSchema::Str(TEXT("search_capabilities query that caused the issue")))
		.Prop(TEXT("attemptedArgs"),
			FNexusSchema::Str(TEXT("Summary of arguments that triggered the issue")))
		.Prop(TEXT("actualError"),
			FNexusSchema::Str(TEXT("Snippet of the actual error received")))
		.Prop(TEXT("expectedField"),
			FNexusSchema::Str(TEXT("Missing, ambiguous, or guessed field name")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
}

FNexusMcpToolResult FNexusMcpToolSubmitFeedback::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	FNexusMcpToolResult Result;
	const FNexusArgs Parsed(Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>());

	const FString Category = Parsed.Str(TEXT("category"));
	if (Category.IsEmpty())
	{
		Result.bIsError = true;
		Result.ErrorText = TEXT("Missing required field: category");
		return Result;
	}

	FNexusFeedback::FFields Fields;
	Fields.Tool          = Parsed.Str(TEXT("tool"));
	Fields.Capability    = Parsed.Str(TEXT("capability"));
	Fields.Query         = Parsed.Str(TEXT("query"));
	Fields.Note          = Parsed.Str(TEXT("note"));
	Fields.AttemptedArgs = Parsed.Str(TEXT("attemptedArgs"));
	Fields.ActualError   = Parsed.Str(TEXT("actualError"));
	Fields.ExpectedField = Parsed.Str(TEXT("expectedField"));

	FNexusFeedback::RecordManual(Category, Fields);

	// 返回确认信息
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("ok"),       true);
	Out->SetStringField(TEXT("recorded"), FDateTime::UtcNow().ToIso8601());
	Out->SetStringField(TEXT("category"), Category);

	Result.StructuredContent = Out;
	Result.OutputText = FNexusJsonUtils::SerializeCondensed(Out);
	return Result;
}

REGISTER_MCP_TOOL(FNexusMcpToolSubmitFeedback)
