// Copyright byteyang. All Rights Reserved.
#pragma once
#include "NexusCapability.h"
#include "NexusMcpTool.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusEditorToolsets.h"
#include "Dom/JsonValue.h"

struct FNexusEditorToolSchema final
{
    static TSharedRef<FJsonObject> Defaults(const TSharedPtr<FJsonObject>& Schema)
    {
        auto Copy = MakeShared<FJsonObject>(); Copy->Values = Schema->Values;
        Copy->SetStringField(TEXT("type"), TEXT("object")); Copy->RemoveField(TEXT("required"));
        return Copy;
    }
    static TSharedRef<FJsonObject> Payload()
    {
        auto Window = FNexusSchema::Object().Required(TEXT("Ref"), FNexusSchema::Str(TEXT("Opaque window reference")))
            .Required(TEXT("Title"), FNexusSchema::Str(TEXT("Window title")))
            .Prop(TEXT("Index"), FNexusSchema::Int(TEXT("Current backend window index"))).Build();
        auto Node = FNexusSchema::Object().Required(TEXT("Ref"), FNexusSchema::Str(TEXT("Opaque widget reference")))
            .Prop(TEXT("Type"), FNexusSchema::Str(TEXT("Slate widget type")))
            .Prop(TEXT("Text"), FNexusSchema::Str(TEXT("Visible label or editable text")))
            .Prop(TEXT("bEnabled"), FNexusSchema::Bool(TEXT("Widget enabled state")))
            .Prop(TEXT("Depth"), FNexusSchema::Int(TEXT("Depth from snapshot root"))).Build();
        auto Test = FNexusSchema::Object().Required(TEXT("Name"), FNexusSchema::Str(TEXT("Full test name")))
            .Prop(TEXT("Outcome"), FNexusSchema::Str(TEXT("Test business outcome")))
            .Prop(TEXT("DurationSeconds"), FNexusSchema::Num(TEXT("Test duration")))
            .Prop(TEXT("Errors"), FNexusSchema::StrArr(TEXT("Test error messages")))
            .Prop(TEXT("Warnings"), FNexusSchema::StrArr(TEXT("Test warning messages"))).Build();
        // Listing emits names; completed runs emit per-test records, with the same array key.
        Test->RemoveField(TEXT("type"));
        Test->SetArrayField(TEXT("type"), {MakeShared<FJsonValueString>(TEXT("string")), MakeShared<FJsonValueString>(TEXT("object"))});
        return FNexusSchema::Object()
            .Prop(TEXT("Windows"), FNexusSchema::ArrayOf(TEXT("Visible windows"), Window.ToSharedRef()))
            .Prop(TEXT("Windows_defaults"), Defaults(Window))
            .Prop(TEXT("Nodes"), FNexusSchema::ArrayOf(TEXT("Compatibility widget snapshot"), Node.ToSharedRef()))
            .Prop(TEXT("Nodes_defaults"), Defaults(Node))
            .Prop(TEXT("Snapshot"), FNexusSchema::Str(TEXT("Official accessibility snapshot")))
            .Prop(TEXT("bTruncated"), FNexusSchema::Bool(TEXT("Snapshot was bounded")))
            .Prop(TEXT("bApplied"), FNexusSchema::Bool(TEXT("Single interaction accepted")))
            .Prop(TEXT("AppliedCount"), FNexusSchema::Int(TEXT("Completed form fields before failure")))
            .Prop(TEXT("FailedFieldIndex"), FNexusSchema::Int(TEXT("Zero-based failed form field")))
            .Prop(TEXT("bConditionMet"), FNexusSchema::Bool(TEXT("WaitFor condition matched")))
            .Prop(TEXT("bTimedOut"), FNexusSchema::Bool(TEXT("WaitFor reached its deadline")))
            .Prop(TEXT("OfficialResult"), FNexusSchema::AnyObject(TEXT("Official domain result; arrays use Items, scalar values use bApplied or Snapshot. Inspect business outcome separately from operation State.")))
            .Prop(TEXT("CompatibilityResult"), FNexusSchema::AnyObject(TEXT("Engine compatibility data; unsupported actions fail explicitly.")))
            .Prop(TEXT("FilePath"), FNexusSchema::Str(TEXT("Saved screenshot path")))
            .Prop(TEXT("Format"), FNexusSchema::Str(TEXT("Screenshot format")))
            .Prop(TEXT("Width"), FNexusSchema::Int(TEXT("Screenshot width")))
            .Prop(TEXT("Height"), FNexusSchema::Int(TEXT("Screenshot height")))
            .Prop(TEXT("Tests"), FNexusSchema::ArrayOf(TEXT("Test names or result records"), Test.ToSharedRef()))
            .Prop(TEXT("Tests_defaults"), Defaults(Test))
            .Prop(TEXT("Status"), FNexusSchema::Str(TEXT("Official discovery state")))
            .Prop(TEXT("Total"), FNexusSchema::Int(TEXT("Total matching tests")))
            .Prop(TEXT("Returned"), FNexusSchema::Int(TEXT("Number returned")))
            .Prop(TEXT("Passed"), FNexusSchema::Int(TEXT("Passed count")))
            .Prop(TEXT("Failed"), FNexusSchema::Int(TEXT("Failed count")))
            .Prop(TEXT("Skipped"), FNexusSchema::Int(TEXT("Skipped count")))
            .Prop(TEXT("DurationSeconds"), FNexusSchema::Num(TEXT("Total execution duration")))
            .Prop(TEXT("Outcome"), FNexusSchema::Enum(TEXT("Compile business outcome"), {TEXT("Success"), TEXT("Failure"), TEXT("NoChanges"), TEXT("Unknown")}))
            .Prop(TEXT("Diagnostics"), FNexusSchema::Str(TEXT("Diagnostics collected for this compile")))
            .Prop(TEXT("bPatchApplied"), FNexusSchema::Bool(TEXT("Observed patch completion")))
            .Build().ToSharedRef();
    }
	static void Configure(FNexusCapabilityDefinition& Out, bool bWrite)
	{
		Out.Tags = bWrite ? TArray<FString>{ FNexusMcpTags::Write, FNexusMcpTags::Dangerous, FNexusMcpTags::Editor }
			: TArray<FString>{ FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
		Out.Prerequisites = { TEXT("EditorOnly") };
        const FString Name = Out.Name;
        Out.ExecutionInfo = [Name]() { return FNexusNativeEditorTools::Describe(Name); };
		Out.bAutoTransaction = false; Out.bAllowBatch = false; Out.bCacheable = false;
		Out.OutputSchema = FNexusSchema::Object()
			.Required(TEXT("OperationId"), FNexusSchema::Str(TEXT("Editor-session operation identifier")))
			.Required(TEXT("Backend"), FNexusSchema::Str(TEXT("Selected execution backend")))
			.Required(TEXT("State"), FNexusSchema::Enum(TEXT("Execution state, not business outcome"), {TEXT("Running"), TEXT("Completed"), TEXT("Failed"), TEXT("Cancelled"), TEXT("Interrupted")}))
			.Required(TEXT("bCanCancel"), FNexusSchema::Bool(TEXT("Whether explicit stop is available")))
			.Prop(TEXT("Message"), FNexusSchema::Str(TEXT("Progress or failure detail")))
			.Prop(TEXT("ElapsedSeconds"), FNexusSchema::Num(TEXT("Elapsed wall-clock seconds")))
			.Prop(TEXT("Result"), Payload())
			.Prop(TEXT("bWaitCancelled"), FNexusSchema::Bool(TEXT("Waiting stopped; work may continue")))
			.Prop(TEXT("SnapshotAt"), FNexusSchema::Str(TEXT("Snapshot UTC timestamp")))
			.Prop(TEXT("TtlSeconds"), FNexusSchema::Int(TEXT("Cache lifetime")))
			.Prop(TEXT("Error"), FNexusSchema::Str(TEXT("Operation failure"))).Build();
	}
	static void ConfigureNative(FNexusCapabilityDefinition& Out, bool bWrite, const FString& Domain)
	{
		Configure(Out, bWrite);
		Out.Prerequisites.Add(TEXT("PluginEnabled:") + Domain);
		Out.ExecutionInfo = [Domain]()
		{
			auto Info = MakeShared<FJsonObject>(); Info->SetStringField(TEXT("Backend"), TEXT("OfficialToolset"));
			Info->SetStringField(TEXT("Reason"), FNexusNativeEditorTools::UseNative(Domain) ? TEXT("Host enabled the official domain; Registry enable and filter rules apply") : TEXT("Requires UE 5.8+ and enabled plugin: ") + Domain);
			return Info;
		};
	}
    static void ConfigureCompatible(FNexusCapabilityDefinition& Out, bool bWrite, const FString& Domain)
    {
        Configure(Out, bWrite);
        Out.ExecutionInfo = [Domain]() {
            auto Info = MakeShared<FJsonObject>();
            Info->SetStringField(TEXT("Backend"), FNexusNativeEditorTools::UseNative(Domain) ? TEXT("OfficialToolset") : TEXT("EngineCompatibility"));
            Info->SetStringField(TEXT("Reason"), TEXT("Fixed session backend; compatibility actions depend on engine APIs. Native failures never retry a compatibility write."));
            return Info;
        };
    }
	static FCapabilityResult Result(const TSharedPtr<FNexusEditorOperation>& Operation)
	{
		if (!Operation.IsValid()) return FCapabilityResult::MakeFatal(TEXT("Unknown operation, foreign owner, or operation capacity reached"));
		FCapabilityResult Out; auto Entry = Operation->Snapshot();
		if (Operation->State == TEXT("Failed")) Entry->SetStringField(TEXT("Error"), Operation->Message);
		Out.Entries.Add(MakeShared<FJsonValueObject>(Entry));
		if (Operation->State == TEXT("Completed")) Out.ImageFiles = Operation->ImageFiles;
		return Out;
	}
};
