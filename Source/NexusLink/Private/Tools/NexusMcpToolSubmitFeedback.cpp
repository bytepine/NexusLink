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
	Out.Name        = TEXT("SubmitFeedback");
	Out.Description = TEXT("Report tool/capability friction. Trigger: retry >=2 with no progress, no suitable capability, schema guessing, or forced serial calls >=3. category: WrongTool|misuse|SchemaGuess|SearchZero|SearchOverflow|other. Prefer AttemptedArgs/ActualError/ExpectedField over long note.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("Category"),
			FNexusSchema::Enum(TEXT("Feedback category"),
				{
					TEXT("WrongTool"),
					TEXT("Misuse"),
					TEXT("SchemaGuess"),
					TEXT("SearchZero"),
					TEXT("SearchOverflow"),
					TEXT("Other"),
				}))
		.Prop(TEXT("Note"),
			FNexusSchema::Str(TEXT("Free-text problem description (recommended)")))
		.Prop(TEXT("Tool"),
			FNexusSchema::Str(TEXT("MCP tool name involved")))
		.Prop(TEXT("Capability"),
			FNexusSchema::Str(TEXT("Capability name involved")))
		.Prop(TEXT("Query"),
			FNexusSchema::Str(TEXT("SearchCapabilities query that caused the issue")))
		.Prop(TEXT("AttemptedArgs"),
			FNexusSchema::Str(TEXT("Summary of arguments that triggered the issue")))
		.Prop(TEXT("ActualError"),
			FNexusSchema::Str(TEXT("Snippet of the actual error received")))
		.Prop(TEXT("ExpectedField"),
			FNexusSchema::Str(TEXT("Missing, ambiguous, or guessed field name")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
}

FNexusMcpToolResult FNexusMcpToolSubmitFeedback::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	FNexusMcpToolResult Result;
	const FNexusArgs Parsed(Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>());

	const FString Category = Parsed.Str(TEXT("Category"));
	if (Category.IsEmpty())
	{
		Result.bIsError = true;
		Result.ErrorText = TEXT("Missing required field: category");
		return Result;
	}

	FNexusFeedback::FFields Fields;
	Fields.Tool          = Parsed.Str(TEXT("Tool"));
	Fields.Capability    = Parsed.Str(TEXT("Capability"));
	Fields.Query         = Parsed.Str(TEXT("Query"));
	Fields.Note          = Parsed.Str(TEXT("Note"));
	Fields.AttemptedArgs = Parsed.Str(TEXT("AttemptedArgs"));
	Fields.ActualError   = Parsed.Str(TEXT("ActualError"));
	Fields.ExpectedField = Parsed.Str(TEXT("ExpectedField"));

	FNexusFeedback::RecordManual(Category, Fields);

	// 返回确认信息
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("bOk"),       true);
	Out->SetStringField(TEXT("Recorded"), FDateTime::UtcNow().ToIso8601());
	Out->SetStringField(TEXT("Category"), Category);

	Result.StructuredContent = Out;
	Result.OutputText = FNexusJsonUtils::SerializeCondensed(Out);
	return Result;
}

REGISTER_MCP_TOOL(FNexusMcpToolSubmitFeedback)
