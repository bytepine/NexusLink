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
	Out.Description = TEXT("Open, focus, inspect or safely close one asset editor. Close rejects dirty assets or shared editor instances.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset path discovered via SearchAsset; maps use ControlEditorLevel")))
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Editor action"), {TEXT("Open"), TEXT("Focus"), TEXT("Status"), TEXT("Close")}, TEXT("Open")))
		.Prop(TEXT("bValidateOnly"), FNexusSchema::Bool(TEXT("Validate target and report current editor state without changing the interface"), true, false))
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Editor};
	Out.bAutoTransaction = false;
	Out.bAllowBatch = false;
	Out.ExtraSearchKeywords = {TEXT("open"), TEXT("focus"), TEXT("close"), TEXT("asset"), TEXT("editor"), TEXT("tab")};
	Out.RelatedCapabilities = {TEXT("SearchAsset"), TEXT("GetEditorSlate"), TEXT("GetEditorContext"), TEXT("SaveAsset"), TEXT("ControlEditorLevel")};
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
		const TArray<FString> Actions = {TEXT("Open"), TEXT("Focus"), TEXT("Status"), TEXT("Close")};
		const FString Action = Actions[Args.EnumInt(TEXT("Action"), Actions, 0)];
		const bool bWasOpen = Editors->FindEditorForAsset(Asset, false) != nullptr;
		const bool bValidateOnly = Args.Bool(TEXT("bValidateOnly"));
		bool bSharedEditor = false;
		const auto TargetEditors = Editors->FindEditorsForAsset(Asset);
		for (UObject* Other : Editors->GetAllEditedAssets())
			if (Other && Other != Asset) for (auto* OtherEditor : Editors->FindEditorsForAsset(Other)) if (TargetEditors.Contains(OtherEditor)) bSharedEditor = true;
		const bool bDirty = Asset->GetOutermost()->IsDirty();
		if (!bValidateOnly && Action != TEXT("Status"))
		{
			if (!FApp::CanEverRender() || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().CanDisplayWindows())
			{ Error = TEXT("Open/Focus require a GUI editor with rendering; use Status or bValidateOnly in a headless host"); return; }
			if (Action == TEXT("Focus") && !bWasOpen) { Error = TEXT("Asset editor is not open; use Action=Open first"); return; }
			if (Action == TEXT("Close"))
			{
				if (bDirty) { Error = TEXT("Asset has unsaved changes; save explicitly with SaveAsset before closing"); return; }
				if (bSharedEditor) { Error = TEXT("Editor instance contains other assets; closing it would affect unrelated targets"); return; }
				if (bWasOpen && Editors->CloseAllEditorsForAsset(Asset) == 0) { Error = TEXT("Native editor refused to close"); return; }
			}
			else if (bWasOpen) Editors->FindEditorForAsset(Asset, true);
			else if (!Editors->OpenEditorForAsset(Asset, EToolkitMode::Standalone, TSharedPtr<IToolkitHost>(), false))
			{ Error = TEXT("Native asset editor failed to open"); return; }
		}
		auto Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("AssetPath"), Asset->GetPathName());
		Entry->SetStringField(TEXT("Action"), Action);
		Entry->SetBoolField(TEXT("bWasOpen"), bWasOpen);
		Entry->SetBoolField(TEXT("bIsOpen"), Editors->FindEditorForAsset(Asset, false) != nullptr);
		Entry->SetBoolField(TEXT("bValidateOnly"), bValidateOnly);
		Entry->SetBoolField(TEXT("bDirty"), bDirty);
		Entry->SetBoolField(TEXT("bCanClose"), !bDirty && !bSharedEditor);
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusControlEditorAssetEditorCapability)
