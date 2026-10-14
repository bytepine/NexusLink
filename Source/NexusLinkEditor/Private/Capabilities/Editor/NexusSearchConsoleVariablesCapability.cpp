// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/NexusSearchConsoleVariablesCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusStringMatchUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "HAL/IConsoleManager.h"
#include "NexusMcpTool.h"

void FNexusSearchConsoleVariablesCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("SearchConsoleVariables");
	Out.Description = TEXT("Search console variable names. Substring match; read-only.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Query"),  FNexusSchema::Str(TEXT("Variable name substring (required)")))
		.Prop(TEXT("Offset"), FNexusSchema::Int(TEXT("Pagination offset"), 0, 0))
		.Prop(TEXT("Limit"),  FNexusSchema::Int(TEXT("Max items per page"), 50, 1, 200))
		.Required({ TEXT("Query") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("cvar"), TEXT("console"), TEXT("variable"), TEXT("r."), TEXT("t.") };
	Out.RelatedCapabilities = { TEXT("ExecCommand") };
}

FCapabilityResult FNexusSearchConsoleVariablesCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		int32 Offset = 0;
		int32 Limit  = 50;

		const FString Query = A.Str(TEXT("Query"));

		if (Arguments->HasField(TEXT("Offset")))
		{
			Offset = FMath::Max(0, static_cast<int32>(A.Num(TEXT("Offset"))));
		}
		if (Arguments->HasField(TEXT("Limit")))
		{
			Limit = FMath::Clamp(static_cast<int32>(A.Num(TEXT("Limit"))), 1, 200);
		}

		TArray<TSharedPtr<FJsonValue>> AllMatches;
		IConsoleManager& ConsoleManager = IConsoleManager::Get();

		ConsoleManager.ForEachConsoleObjectThatStartsWith(
			FConsoleObjectVisitor::CreateLambda([&](const TCHAR* Name, IConsoleObject* Obj)
			{
				if (!Obj || !Name)
				{
					return;
				}
				const FString NameStr(Name);
				if (!FNexusStringMatchUtils::Matches(NameStr, Query))
				{
					return;
				}

				TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), NameStr);

				if (IConsoleVariable* CVar = Obj->AsVariable())
				{
					Item->SetStringField(TEXT("Kind"), TEXT("variable"));
					Item->SetStringField(TEXT("Value"), CVar->GetString());
				}
				else
				{
					Item->SetStringField(TEXT("Kind"), TEXT("Command"));
				}

				AllMatches.Add(MakeShared<FJsonValueObject>(Item));
			}),
			TEXT(""));

		AllMatches.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
		{
			const FString NameA = A->AsObject()->GetStringField(TEXT("name"));
			const FString NameB = B->AsObject()->GetStringField(TEXT("name"));
			return NameA < NameB;
		});

		const int32 Total = AllMatches.Num();
		int32 Start = 0;
		int32 End   = 0;
		FNexusJsonUtils::ComputeSlice(Total, Offset, Limit, Start, End);

		TArray<TSharedPtr<FJsonValue>> Page;
		for (int32 i = Start; i < End; ++i)
		{
			Page.Add(AllMatches[i]);
		}

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetStringField(TEXT("Query"), Query);
		OutEntry->SetNumberField(TEXT("TotalCount"), Total);
		OutEntry->SetNumberField(TEXT("Offset"), Offset);
		OutEntry->SetNumberField(TEXT("Limit"), Limit);
		OutEntry->SetArrayField(TEXT("Variables"), Page);
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusSearchConsoleVariablesCapability)

#endif // WITH_EDITOR
