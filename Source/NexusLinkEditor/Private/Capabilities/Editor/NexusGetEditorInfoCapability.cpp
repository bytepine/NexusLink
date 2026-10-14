// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/NexusGetEditorInfoCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Misc/App.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

void FNexusGetEditorInfoCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetEditorInfo");
	Out.Description = TEXT("Return UE version, project, platform, build config. No params.");
	Out.InputSchema = FNexusSchema::Object().Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("Status"), TEXT("environment"), TEXT("engine"), TEXT("Version"), TEXT("project") };
	Out.RelatedCapabilities = { TEXT("GetOutputLog") };
}

FCapabilityResult FNexusGetEditorInfoCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		OutEntry->SetStringField(TEXT("EngineVersion"), FString::Printf(TEXT("%d.%d.%d"),
			ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION, ENGINE_PATCH_VERSION));
		OutEntry->SetStringField(TEXT("ProjectName"), FApp::GetProjectName());

	#if PLATFORM_WINDOWS
		OutEntry->SetStringField(TEXT("Platform"), TEXT("Windows"));
	#elif PLATFORM_MAC
		OutEntry->SetStringField(TEXT("Platform"), TEXT("Mac"));
	#elif PLATFORM_LINUX
		OutEntry->SetStringField(TEXT("Platform"), TEXT("Linux"));
	#else
		OutEntry->SetStringField(TEXT("Platform"), TEXT("Unknown"));
	#endif

	#if UE_BUILD_SHIPPING
		OutEntry->SetStringField(TEXT("BuildConfig"), TEXT("Shipping"));
	#elif UE_BUILD_DEVELOPMENT
		OutEntry->SetStringField(TEXT("BuildConfig"), TEXT("Development"));
	#elif UE_BUILD_DEBUG
		OutEntry->SetStringField(TEXT("BuildConfig"), TEXT("Debug"));
	#else
		OutEntry->SetStringField(TEXT("BuildConfig"), TEXT("Unknown"));
	#endif

	#if WITH_EDITOR
		OutEntry->SetBoolField(TEXT("bIsEditor"), true);
		if (GEditor)
		{
			OutEntry->SetBoolField(TEXT("bIsPIERunning"), GEditor->IsPlayingSessionInEditor());
		}
	#else
		OutEntry->SetBoolField(TEXT("bIsEditor"), false);
	#endif
		OutEntry->SetBoolField(TEXT("bHasPlayWorld"), FNexusRuntimeUtils::HasPlayWorld());

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetEditorInfoCapability)

#endif // WITH_EDITOR
