// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Trace/NexusListTracesCapability.h"

#if NX_UE_HAS_TRACE_READ

#include "Capabilities/Trace/NexusTraceSession.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Utils/NexusCapabilityResultBuilder.h"

void FNexusListTracesCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ListTraces");
	Out.Description = TEXT("List recent .utrace files and the local Trace Store.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Limit"), FNexusSchema::Int(TEXT("Max traces to return"), 20, 1, 100))
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("Insights"), TEXT("utrace"), TEXT("profiling") };
	Out.RelatedCapabilities = { TEXT("GetTrace") };
	Out.WhenToUse = TEXT("Find a .utrace path");
}

FCapabilityResult FNexusListTracesCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		int32 Limit = 20;
		if (Arguments.IsValid() && Arguments->HasField(TEXT("Limit")))
		{
			Limit = FMath::Clamp(static_cast<int32>(Arguments->GetNumberField(TEXT("Limit"))), 1, 100);
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Traces;
		FNexusTraceSessionCache::CollectRecentTraces(Limit, Traces);
		Entry->SetArrayField(TEXT("Traces"), Traces);
		Entry->SetNumberField(TEXT("Count"), Traces.Num());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusListTracesCapability)

#endif
