// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Runtime/Debug/NexusGetRuntimePerformanceCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "EngineGlobals.h"
#include "Misc/App.h"
#include "RHI.h"

void FNexusGetRuntimePerformanceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetRuntimePerformance");
	Out.Description = TEXT("Read last available frame/Game/Render/RHI/GPU timing in milliseconds. Process-wide, asynchronous thread samples; not a synchronized frame profile. No capture or console changes.");
	Out.InputSchema = FNexusSchema::Object().Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime};
	Out.ExtraSearchKeywords = {TEXT("performance"), TEXT("frame"), TEXT("fps"), TEXT("gpu"), TEXT("timing"), TEXT("profile")};
	Out.RelatedCapabilities = {TEXT("GetProfilerCsv"), TEXT("GetTrace"), TEXT("ListTraces")};
}
FCapabilityResult FNexusGetRuntimePerformanceCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	auto Entry = MakeShared<FJsonObject>();
	Entry->SetNumberField(TEXT("TtlSeconds"), 0);
	Entry->SetStringField(TEXT("Unit"), TEXT("ms"));
	Entry->SetStringField(TEXT("Scope"), TEXT("Process"));
	Entry->SetStringField(TEXT("SampleKind"), TEXT("LatestAvailableThreadSamples"));
	Entry->SetNumberField(TEXT("SampledAtSeconds"), FPlatformTime::Seconds());
	Entry->SetStringField(TEXT("FrameCounter"), LexToString(GFrameCounter));
	const double FrameMs = (FApp::GetCurrentTime() - FApp::GetLastTime()) * 1000;
	if (FMath::IsFinite(FrameMs) && FrameMs > 0) { Entry->SetNumberField(TEXT("FrameMs"), FrameMs); Entry->SetNumberField(TEXT("Fps"), 1000 / FrameMs); }
	const uint32 GameCycles = GGameThreadTime, RenderCycles = GRenderThreadTime, RhiCycles = GRHIThreadTime;
	if (GameCycles) Entry->SetNumberField(TEXT("GameThreadMs"), FPlatformTime::ToMilliseconds(GameCycles));
	if (RenderCycles) Entry->SetNumberField(TEXT("RenderThreadMs"), FPlatformTime::ToMilliseconds(RenderCycles));
	if (RhiCycles) Entry->SetNumberField(TEXT("RhiThreadMs"), FPlatformTime::ToMilliseconds(RhiCycles));
	const uint32 GpuCycles = FApp::CanEverRender() && GDynamicRHI ? RHIGetGPUFrameCycles(0) : 0;
	Entry->SetBoolField(TEXT("bGpuTimingAvailable"), GpuCycles > 0);
	if (GpuCycles) Entry->SetNumberField(TEXT("GpuMs"), FPlatformTime::ToMilliseconds(GpuCycles));
	else Entry->SetStringField(TEXT("GpuTimingUnavailableReason"), TEXT("No renderer, GPU timing disabled, unsupported RHI, or no completed GPU sample"));
	FCapabilityResult Result; Result.Entries.Add(MakeShared<FJsonValueObject>(Entry)); return Result;
}
REGISTER_MCP_CAPABILITY(FNexusGetRuntimePerformanceCapability)
