// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/NexusGetProfilerCsvCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusVersionCompat.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/Csv/CsvParser.h"
#if NX_UE_HAS_CSV_PROFILER_UTILS
#include "CSVProfilerUtils.h"

/** 原生读取器的分词器会吞掉空列；用 CSV 解析器读取有界快照并保留事件/统计值列位置。 */
static bool NexusReadProfilerCsv(const TArray<FString>& Lines, CsvUtils::FCsvProfilerCapture& Capture, FString& Error)
{
	int32 End = Lines.Num(); bool bHeaderAtEnd = false;
	if (End < 2) { Error = TEXT("CSV requires a header and frame rows"); return false; }
	if (Lines.Last().TrimStartAndEnd().StartsWith(TEXT("[")))
	{
		FCsvParser Metadata(Lines.Last());
		if (Metadata.GetRows().Num())
		{
			const auto& Cells = Metadata.GetRows()[0];
			for (int32 Index = 0; Index + 1 < Cells.Num(); ++Index)
				if (FString(Cells[Index]).Equals(TEXT("[hasheaderrowatend]"), ESearchCase::IgnoreCase) && FString(Cells[Index + 1]) == TEXT("1")) bHeaderAtEnd = true;
		}
		--End;
	}
	if (bHeaderAtEnd) --End;
	if (End < 2 || End > 500001) { Error = TEXT("CSV requires 1-500000 complete frame rows"); return false; }
	FCsvParser HeaderParser(Lines[bHeaderAtEnd ? End : 0]);
	if (HeaderParser.GetRows().Num() != 1) { Error = TEXT("CSV requires one header row"); return false; }
	const auto& HeaderCells = HeaderParser.GetRows()[0];
	TArray<FString> Headers;
	for (const TCHAR* Cell : HeaderCells) Headers.Add(Cell);
	if (Headers.Num() < 1 || Headers.Num() > 8192 || static_cast<int64>(Headers.Num()) * (End - 1) > 4000000)
	{ Error = TEXT("CSV exceeds 8192 columns or 4 million samples"); return false; }
	int32 EventsColumn = INDEX_NONE;
	for (int32 Column = 0; Column < Headers.Num(); ++Column)
	{
		if (Headers[Column].Equals(TEXT("events"), ESearchCase::IgnoreCase))
		{
			if (EventsColumn != INDEX_NONE) { Error = TEXT("CSV has duplicate EVENTS columns"); return false; }
			EventsColumn = Column;
			continue;
		}
		if (Headers[Column].IsEmpty() || Capture.Samples.Contains(Headers[Column]))
		{ Error = TEXT("CSV stat names must be nonempty and unique"); return false; }
		auto& Sample = Capture.Samples.Add(Headers[Column]);
		Sample.Name = Headers[Column]; Sample.Values.SetNumZeroed(End - 1);
	}
	for (int32 Row = 1; Row < End; ++Row)
	{
		FCsvParser RowParser(Lines[Row]);
		if (RowParser.GetRows().Num() != 1) { Error = FString::Printf(TEXT("CSV row %d is invalid"), Row); return false; }
		const auto& Cells = RowParser.GetRows()[0];
		if (Cells.Num() > Headers.Num() || (!bHeaderAtEnd && Cells.Num() != Headers.Num()))
		{ Error = FString::Printf(TEXT("CSV row %d has %d cells; expected %d"), Row, Cells.Num(), Headers.Num()); return false; }
		// 流式捕获新增统计项后，尾部表头比早期帧长；未出现的尾列保持零值。
		for (int32 Column = 0; Column < Cells.Num(); ++Column)
		{
			if (Column == EventsColumn)
			{
				TArray<FString> Events; FString(Cells[Column]).ParseIntoArray(Events, TEXT(";"), true);
				for (const FString& Name : Events) { auto& Event = Capture.Events.AddDefaulted_GetRef(); Event.Name = Name; Event.Frame = Row - 1; }
			}
			else Capture.Samples.FindChecked(Headers[Column]).Values[Row - 1] = FCString::Atof(Cells[Column]);
		}
	}
	return true;
}
#endif

void FNexusGetProfilerCsvCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetProfilerCsv");
	Out.Description = TEXT("List profiling CSV files or query an existing text CSV capture under project Saved. UE5.7+. Recomputes interval min/max/mean/total from samples. No recording or GPU capture. 16 MiB file limit.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Read operation; default Files"), {TEXT("Files"), TEXT("Summary"), TEXT("Stats"), TEXT("Events")}, TEXT("Files")))
		.Prop(TEXT("CapturePath"), FNexusSchema::Str(TEXT("CSV path from Files; must be under project Saved")))
		.Prop(TEXT("StatFilter"), FNexusSchema::Str(TEXT("Stat name substring; empty matches all")))
		.Prop(TEXT("FrameStart"), FNexusSchema::Int(TEXT("Inclusive frame index"), 0, 0))
		.Prop(TEXT("FrameEnd"), FNexusSchema::Int(TEXT("Exclusive frame index; -1 means end"), -1, -1))
		.Prop(TEXT("SortBy"), FNexusSchema::Enum(TEXT("Stats ranking"), {TEXT("Mean"), TEXT("Max"), TEXT("Total"), TEXT("Name")}, TEXT("Mean")))
		.Prop(TEXT("Offset"), FNexusSchema::Int(TEXT("Result offset"), 0, 0))
		.Prop(TEXT("Limit"), FNexusSchema::Int(TEXT("Max returned stats/events/files"), 50, 1, 500)).Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor};
	Out.ExtraSearchKeywords = {TEXT("csv"), TEXT("performance"), TEXT("profile"), TEXT("stats"), TEXT("aggregate")};
	Out.RelatedCapabilities = {TEXT("GetRuntimePerformance"), TEXT("GetTrace")};
}
FCapabilityResult FNexusGetProfilerCsvCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
#if NX_UE_HAS_CSV_PROFILER_UTILS
	return FNexusCapabilityResultBuilder::Build([&](auto& Entries, auto& Top, auto& Error)
	{
		const FNexusArgs Args(Arguments);
		const FString Action = Args.Str(TEXT("Action"), TEXT("Files"));
		const int32 Offset = Args.Int(TEXT("Offset"), 0), Limit = Args.Int(TEXT("Limit"), 50);
		if (Offset < 0 || Offset > 1000000 || Limit < 1 || Limit > 500) { Error = TEXT("Offset 0-1000000 and Limit 1-500 required"); return; }
		const FString SavedRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
		auto Entry = MakeShared<FJsonObject>();
		if (Action == TEXT("Files"))
		{
			TArray<FString> Files;
			IFileManager::Get().FindFilesRecursive(Files, *FPaths::ProfilingDir(), TEXT("*.csv"), true, false);
			Files.Sort();
			TArray<TSharedPtr<FJsonValue>> Items;
			for (int32 Index = Offset; Index < FMath::Min(Files.Num(), Offset + Limit); ++Index)
			{
				auto Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("CapturePath"), FPaths::ConvertRelativePathToFull(Files[Index]));
				Item->SetNumberField(TEXT("Bytes"), IFileManager::Get().FileSize(*Files[Index]));
				Items.Add(MakeShared<FJsonValueObject>(Item));
			}
			Entry->SetArrayField(TEXT("Files"), Items); Entry->SetNumberField(TEXT("Total"), Files.Num());
		}
		else
		{
			FString FilePath = Args.Str(TEXT("CapturePath"));
			if (FilePath.IsEmpty()) { Error = TEXT("CapturePath is required; discover captures with Action=Files"); return; }
			FilePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), FilePath);
			FPaths::NormalizeFilename(FilePath); FPaths::CollapseRelativeDirectories(FilePath);
			const int64 Size = IFileManager::Get().FileSize(*FilePath);
			if (!FPaths::IsUnderDirectory(FilePath, SavedRoot) || FPaths::GetExtension(FilePath).ToLower() != TEXT("csv") || Size <= 0 || Size > 16 * 1024 * 1024)
			{ Error = TEXT("Require an existing text .csv under project Saved, 1 byte to 16 MiB"); return; }
			auto Reader = TUniquePtr<FArchive>(IFileManager::Get().CreateFileReader(*FilePath));
			char Magic[6] = {};
			if (!Reader) { Error = TEXT("Cannot open capture"); return; }
			if (Size >= 6) Reader->Serialize(Magic, 6);
			Reader.Reset();
			if (FMemory::Memcmp(Magic, "CSVBIN", 6) == 0) { Error = TEXT("Binary CSV capture is not supported by this text-only query"); return; }
			CsvUtils::FCsvProfilerCapture Capture;
			TArray<FString> Lines;
			if (!FFileHelper::LoadFileToStringArray(Lines, *FilePath) || !NexusReadProfilerCsv(Lines, Capture, Error)) { if (Error.IsEmpty()) Error = TEXT("Cannot read CSV snapshot"); return; }
			int32 FrameCount = 0;
			for (const auto& Sample : Capture.Samples) FrameCount = FMath::Max(FrameCount, Sample.Value.Values.Num());
			const int32 Start = Args.Int(TEXT("FrameStart"), 0), RequestedEnd = Args.Int(TEXT("FrameEnd"), -1);
			const int32 End = RequestedEnd == -1 ? FrameCount : RequestedEnd;
			if (Start < 0 || End <= Start || End > FrameCount) { Error = TEXT("Frame range must be nonempty and within the capture; FrameEnd is exclusive"); return; }
			Entry->SetStringField(TEXT("CapturePath"), FilePath); Entry->SetNumberField(TEXT("FrameCount"), FrameCount);
			Entry->SetNumberField(TEXT("FrameStart"), Start); Entry->SetNumberField(TEXT("FrameEnd"), End);
			if (Action == TEXT("Summary"))
			{
				Entry->SetNumberField(TEXT("StatCount"), Capture.Samples.Num()); Entry->SetNumberField(TEXT("EventCount"), Capture.Events.Num());
				TArray<FString> Names; Capture.Samples.GetKeys(Names); Names.Sort();
				TArray<TSharedPtr<FJsonValue>> Page;
				for (int32 Index = Offset; Index < FMath::Min(Names.Num(), Offset + Limit); ++Index) Page.Add(MakeShared<FJsonValueString>(Names[Index]));
				Entry->SetArrayField(TEXT("StatNames"), Page);
				Entry->SetStringField(TEXT("Units"), TEXT("Capture-defined per stat; not assumed to all be milliseconds"));
			}
			else if (Action == TEXT("Stats"))
			{
				struct FStatRow { FString Name; double Min = 0, Max = 0, Total = 0, Mean = 0; int32 Count = 0; };
				TArray<FStatRow> Rows;
				for (const auto& Sample : Capture.Samples)
				{
					if (!Args.Str(TEXT("StatFilter")).IsEmpty() && !Sample.Key.Contains(Args.Str(TEXT("StatFilter")))) continue;
					FStatRow Row; Row.Name = Sample.Key;
					for (int32 Index = Start; Index < FMath::Min(End, Sample.Value.Values.Num()); ++Index)
					{
						const double Value = Sample.Value.Values[Index];
						if (!FMath::IsFinite(Value)) continue;
						if (!Row.Count) Row.Min = Row.Max = Value;
						Row.Min = FMath::Min(Row.Min, Value); Row.Max = FMath::Max(Row.Max, Value); Row.Total += Value; ++Row.Count;
					}
					if (!Row.Count) continue;
					Row.Mean = Row.Total / Row.Count; Rows.Add(Row);
				}
				const FString Sort = Args.Str(TEXT("SortBy"), TEXT("Mean"));
				Rows.Sort([&](const FStatRow& Left, const FStatRow& Right)
				{
					const double LeftValue = Sort == TEXT("Max") ? Left.Max : (Sort == TEXT("Total") ? Left.Total : Left.Mean);
					const double RightValue = Sort == TEXT("Max") ? Right.Max : (Sort == TEXT("Total") ? Right.Total : Right.Mean);
					return Sort == TEXT("Name") || LeftValue == RightValue ? Left.Name < Right.Name : LeftValue > RightValue;
				});
				TArray<TSharedPtr<FJsonValue>> Page;
				for (int32 Index = Offset; Index < FMath::Min(Rows.Num(), Offset + Limit); ++Index)
				{
					const auto& Row = Rows[Index]; auto Item = MakeShared<FJsonObject>();
					Item->SetStringField(TEXT("Name"), Row.Name); Item->SetNumberField(TEXT("SampleCount"), Row.Count);
					Item->SetNumberField(TEXT("Min"), Row.Min); Item->SetNumberField(TEXT("Max"), Row.Max);
					Item->SetNumberField(TEXT("Mean"), Row.Mean); Item->SetNumberField(TEXT("Total"), Row.Total); Page.Add(MakeShared<FJsonValueObject>(Item));
				}
				Entry->SetArrayField(TEXT("Stats"), Page); Entry->SetNumberField(TEXT("Total"), Rows.Num());
				Entry->SetStringField(TEXT("Units"), TEXT("Capture-defined; samples aggregated over [FrameStart,FrameEnd), nonfinite samples excluded"));
			}
			else if (Action == TEXT("Events"))
			{
				TArray<TSharedPtr<FJsonValue>> Items; int32 Count = 0;
				for (const auto& Event : Capture.Events)
				{
					if (Event.Frame < Start || Event.Frame >= End) continue;
					if (Count >= Offset && Items.Num() < Limit) { auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("Name"), Event.Name); Item->SetNumberField(TEXT("Frame"), Event.Frame); Items.Add(MakeShared<FJsonValueObject>(Item)); }
					++Count;
				}
				Entry->SetArrayField(TEXT("Events"), Items); Entry->SetNumberField(TEXT("Total"), Count);
			}
			else { Error = TEXT("Unknown Action"); return; }
		}
		Entry->SetNumberField(TEXT("Offset"), Offset); Entry->SetNumberField(TEXT("Limit"), Limit);
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	});
#else
	return FCapabilityResult::MakeFatal(TEXT("CSV profiler query requires UE5.7+ CSVUtils"));
#endif
}
REGISTER_MCP_CAPABILITY(FNexusGetProfilerCsvCapability)
