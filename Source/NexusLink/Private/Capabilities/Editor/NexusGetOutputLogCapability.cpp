// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/NexusGetOutputLogCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Editor/NexusLogCapture.h"
#include "Utils/NexusResponseCompactorUtils.h"
#include "NexusMcpTool.h"

void FGetOutputLogCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_output_log");
	Out.Description = TEXT("读取 UE 控制台缓冲。含 LogConsole（exec_command 镜像）；category/verbosity/text 过滤+分页。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("offset"),         FNexusSchema::Int(TEXT("分页偏移"), 0, 0))
		.Prop(TEXT("limit"),          FNexusSchema::Int(TEXT("每页最大条数"), 100, 1, 500))
		.Prop(TEXT("categoryFilter"), FNexusSchema::Str(TEXT("日志分类子串（不区分大小写）")))
		.Prop(TEXT("verbosity"),      FNexusSchema::Enum(TEXT("最低详细级别"),
			{ TEXT("error"), TEXT("warning"), TEXT("display"), TEXT("log"), TEXT("verbose"), TEXT("veryverbose"), TEXT("all") }, TEXT("log")))
		.Prop(TEXT("textFilter"),     FNexusSchema::Str(TEXT("单文本子串过滤")))
		.Prop(TEXT("textFilters"),    FNexusSchema::StrArr(TEXT("文本过滤（OR）；覆盖 textFilter")))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("logs"), TEXT("console"), TEXT("messages"), TEXT("verbosity"), TEXT("warning") };
	Out.RelatedCapabilities = { TEXT("set_log_capture_filter"), TEXT("exec_command") };
}

FCapabilityResult FGetOutputLogCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		int32 Offset = 0;
		int32 Limit  = 100;
		FString CategoryFilter;
		FString VerbosityStr = TEXT("log");
		TArray<FString> TextFilters;

		if (Arguments.IsValid())
		{
			if (Arguments->HasField(TEXT("offset")))
				Offset = FMath::Max(0, static_cast<int32>(Arguments->GetNumberField(TEXT("offset"))));
			if (Arguments->HasField(TEXT("limit")))
				Limit = FMath::Clamp(static_cast<int32>(Arguments->GetNumberField(TEXT("limit"))), 1, 500);
			Arguments->TryGetStringField(TEXT("categoryFilter"), CategoryFilter);
			FString TmpVerbosity;
			if (Arguments->TryGetStringField(TEXT("verbosity"), TmpVerbosity)) VerbosityStr = TmpVerbosity.ToLower();
			if (Arguments->HasField(TEXT("textFilters")))
			{
				const TArray<TSharedPtr<FJsonValue>>* ArrPtr = nullptr;
				if (Arguments->TryGetArrayField(TEXT("textFilters"), ArrPtr) && ArrPtr)
					for (const TSharedPtr<FJsonValue>& V : *ArrPtr) { TextFilters.Add(V->AsString()); }
			}
			else
			{
				FString SingleFilter;
				if (Arguments->TryGetStringField(TEXT("textFilter"), SingleFilter) && !SingleFilter.IsEmpty())
					TextFilters.Add(SingleFilter);
			}
		}

		ELogVerbosity::Type VerbosityFilter = ELogVerbosity::Log;
		if      (VerbosityStr == TEXT("fatal"))       VerbosityFilter = ELogVerbosity::Fatal;
		else if (VerbosityStr == TEXT("error"))        VerbosityFilter = ELogVerbosity::Error;
		else if (VerbosityStr == TEXT("warning"))      VerbosityFilter = ELogVerbosity::Warning;
		else if (VerbosityStr == TEXT("display"))      VerbosityFilter = ELogVerbosity::Display;
		else if (VerbosityStr == TEXT("log"))          VerbosityFilter = ELogVerbosity::Log;
		else if (VerbosityStr == TEXT("verbose"))      VerbosityFilter = ELogVerbosity::Verbose;
		else if (VerbosityStr == TEXT("veryverbose"))  VerbosityFilter = ELogVerbosity::VeryVerbose;
		else if (VerbosityStr == TEXT("all"))          VerbosityFilter = ELogVerbosity::All;

		int32 TotalCount = 0;
		const TArray<FNexusLogEntry> Entries = FNexusLogCapture::Get().Query(
			Offset, Limit, CategoryFilter, VerbosityFilter, TextFilters, TotalCount);

		auto VerbosityToString = [](ELogVerbosity::Type V) -> FString
		{
			switch (V)
			{
			case ELogVerbosity::Fatal:       return TEXT("Fatal");
			case ELogVerbosity::Error:       return TEXT("Error");
			case ELogVerbosity::Warning:     return TEXT("Warning");
			case ELogVerbosity::Display:     return TEXT("Display");
			case ELogVerbosity::Log:         return TEXT("Log");
			case ELogVerbosity::Verbose:     return TEXT("Verbose");
			case ELogVerbosity::VeryVerbose: return TEXT("VeryVerbose");
			default:                         return TEXT("Unknown");
			}
		};

		TArray<TSharedPtr<FJsonValue>> LogArray;
		for (const FNexusLogEntry& E : Entries)
		{
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			if (!E.Category.IsEmpty()) Item->SetStringField(TEXT("category"), E.Category);
			Item->SetStringField(TEXT("verbosity"), VerbosityToString(E.Verbosity));
			if (!E.Message.IsEmpty())  Item->SetStringField(TEXT("message"),  E.Message);
			Item->SetNumberField(TEXT("timestamp"), E.Timestamp);
			LogArray.Add(MakeShared<FJsonValueObject>(Item));
		}

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		OutEntry->SetNumberField(TEXT("totalCount"), TotalCount);
		OutEntry->SetNumberField(TEXT("offset"),     Offset);
		OutEntry->SetNumberField(TEXT("limit"),      Limit);
		OutEntry->SetArrayField(TEXT("entries"),     LogArray);

		if (!CategoryFilter.IsEmpty())
		{
			FNexusResponseCompactorUtils EntryCompactor;
			EntryCompactor.AddForcedDefault(TEXT("category"), CategoryFilter);
			EntryCompactor.SetAutoDiscover(true);
			EntryCompactor.CompactArray(LogArray);
			EntryCompactor.Emit(OutEntry, TEXT("entries"));
		}

		const TArray<FString> Whitelist = FNexusLogCapture::Get().GetCategoryWhitelist();
		if (Whitelist.Num() == 0)
		{
			OutEntry->SetStringField(TEXT("captureFilter"), TEXT("all"));
		}
		else
		{
			TArray<TSharedPtr<FJsonValue>> WlArr;
			for (const FString& Cat : Whitelist) WlArr.Add(MakeShared<FJsonValueString>(Cat));
			OutEntry->SetArrayField(TEXT("captureFilter"), WlArr);
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FGetOutputLogCapability)

#endif // WITH_EDITOR
