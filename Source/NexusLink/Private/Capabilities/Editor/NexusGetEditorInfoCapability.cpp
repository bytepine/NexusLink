// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/NexusGetEditorInfoCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Misc/App.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

void FGetEditorInfoCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_editor_info");
	Out.Description = TEXT("返回 UE 版本、项目名、平台与构建配置。无参数。");
	Out.InputSchema = FNexusSchema::Object().Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("status"), TEXT("environment"), TEXT("engine"), TEXT("version"), TEXT("project") };
	Out.RelatedCapabilities = { TEXT("get_output_log") };
}

FCapabilityResult FGetEditorInfoCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		OutEntry->SetStringField(TEXT("engineVersion"), FString::Printf(TEXT("%d.%d.%d"),
			ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION, ENGINE_PATCH_VERSION));
		OutEntry->SetStringField(TEXT("projectName"), FApp::GetProjectName());

	#if PLATFORM_WINDOWS
		OutEntry->SetStringField(TEXT("platform"), TEXT("Windows"));
	#elif PLATFORM_MAC
		OutEntry->SetStringField(TEXT("platform"), TEXT("Mac"));
	#elif PLATFORM_LINUX
		OutEntry->SetStringField(TEXT("platform"), TEXT("Linux"));
	#else
		OutEntry->SetStringField(TEXT("platform"), TEXT("Unknown"));
	#endif

	#if UE_BUILD_SHIPPING
		OutEntry->SetStringField(TEXT("buildConfig"), TEXT("Shipping"));
	#elif UE_BUILD_DEVELOPMENT
		OutEntry->SetStringField(TEXT("buildConfig"), TEXT("Development"));
	#elif UE_BUILD_DEBUG
		OutEntry->SetStringField(TEXT("buildConfig"), TEXT("Debug"));
	#else
		OutEntry->SetStringField(TEXT("buildConfig"), TEXT("Unknown"));
	#endif

	#if WITH_EDITOR
		OutEntry->SetBoolField(TEXT("isEditor"), true);
		if (GEditor)
		{
			OutEntry->SetBoolField(TEXT("isPIERunning"), GEditor->IsPlayingSessionInEditor());
		}
	#else
		OutEntry->SetBoolField(TEXT("isEditor"), false);
	#endif

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FGetEditorInfoCapability)

#endif // WITH_EDITOR
