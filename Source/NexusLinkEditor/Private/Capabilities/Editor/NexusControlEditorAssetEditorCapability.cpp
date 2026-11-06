// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/NexusControlEditorAssetEditorCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "Subsystems/AssetEditorSubsystem.h"

void FNexusControlEditorAssetEditorCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ControlEditorAssetEditor");
	Out.Description = TEXT("Open or focus an asset editor, or inspect whether it is open. Single asset; no save or close.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset path discovered via SearchAsset; maps use ControlEditorLevel")))
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Editor action"), {TEXT("Open"), TEXT("Focus"), TEXT("Status")}, TEXT("Open")))
		.Prop(TEXT("bValidateOnly"), FNexusSchema::Bool(TEXT("Validate target and report current editor state without opening/focusing"), true, false))
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Editor};
	Out.bAutoTransaction = false;
	Out.bAllowBatch = false;
	Out.ExtraSearchKeywords = {TEXT("open"), TEXT("focus"), TEXT("asset"), TEXT("editor"), TEXT("tab")};
	Out.RelatedCapabilities = {TEXT("SearchAsset"), TEXT("GetEditorSlate"), TEXT("ControlEditorLevel")};
	Out.Prerequisites = {TEXT("EditorOnly")};
}

FCapabilityResult FNexusControlEditorAssetEditorCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& Entries, auto& Top, auto& Error)
	{
		const FNexusArgs Args(Arguments);
		if (!GEditor || IsRunningCommandlet()) { Error = TEXT("An interactive editor host is required"); return; }
		UObject* Asset = FNexusAssetUtils::LoadAssetWithFallback<UObject>(Args.Str(TEXT("AssetPath")));
		if (!Asset) { Error = TEXT("Asset not found"); return; }
		if (Asset->IsA(UWorld::StaticClass())) { Error = TEXT("Maps must be opened through ControlEditorLevel with unsaved-level protection"); return; }
		UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (!Editors) { Error = TEXT("Asset editor subsystem is unavailable"); return; }
		const TArray<FString> Actions = {TEXT("Open"), TEXT("Focus"), TEXT("Status")};
		const FString Action = Actions[Args.EnumInt(TEXT("Action"), Actions, 0)];
		const bool bWasOpen = Editors->FindEditorForAsset(Asset, false) != nullptr;
		const bool bValidateOnly = Args.Bool(TEXT("bValidateOnly"));
		if (!bValidateOnly && Action != TEXT("Status"))
		{
			if (!FApp::CanEverRender() || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().CanDisplayWindows())
			{ Error = TEXT("Open/Focus require a GUI editor with rendering; use Status or bValidateOnly in a headless host"); return; }
			if (Action == TEXT("Focus") && !bWasOpen) { Error = TEXT("Asset editor is not open; use Action=Open first"); return; }
			if (bWasOpen) Editors->FindEditorForAsset(Asset, true);
			else if (!Editors->OpenEditorForAsset(Asset, EToolkitMode::Standalone, TSharedPtr<IToolkitHost>(), false))
			{ Error = TEXT("Native asset editor failed to open"); return; }
		}
		auto Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("AssetPath"), Asset->GetPathName());
		Entry->SetStringField(TEXT("Action"), Action);
		Entry->SetBoolField(TEXT("bWasOpen"), bWasOpen);
		Entry->SetBoolField(TEXT("bIsOpen"), Editors->FindEditorForAsset(Asset, false) != nullptr);
		Entry->SetBoolField(TEXT("bValidateOnly"), bValidateOnly);
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusControlEditorAssetEditorCapability)
