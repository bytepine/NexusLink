// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/NexusControlEditorLevelCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "EditorLevelUtils.h"
#include "LevelUtils.h"
#include "Misc/PackageName.h"

void FNexusControlEditorLevelCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ControlEditorLevel");
	Out.Description = TEXT("Open a map or load/show/hide/make current an existing streaming sublevel. Open rejects unsaved changes unless bDiscardUnsaved.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Editor level operation; Open switches the entire map"), {TEXT("Open"), TEXT("LoadStreaming"), TEXT("Show"), TEXT("Hide"), TEXT("MakeCurrent")}, TEXT("Open")))
		.Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Map asset path, e.g. /Game/Maps/Main")))
		.Prop(TEXT("bDiscardUnsaved"), FNexusSchema::Bool(TEXT("Discard unsaved changes to the current map"), true, false))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("open"), TEXT("level"), TEXT("map"), TEXT("switch"), TEXT("load"), TEXT("streaming"), TEXT("sublevel"), TEXT("visible"), TEXT("current") };
	Out.RelatedCapabilities = { TEXT("GetEditorContext"), TEXT("ManageAssetLevel") };
	Out.WhenToUse = TEXT("Switch the editor's open map without starting PIE");
}

FCapabilityResult FNexusControlEditorLevelCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	const FNexusArgs A(Arguments);
	FString AssetPath = A.Str(TEXT("AssetPath"));
	const bool bDiscardUnsaved = A.Bool(TEXT("bDiscardUnsaved"), false);
	const FString Action = A.Str(TEXT("Action"), TEXT("Open"));
	int32 DotIdx = INDEX_NONE;
	if (AssetPath.FindLastChar(TEXT('.'), DotIdx))
	{
		AssetPath = AssetPath.Left(DotIdx);
	}
	if (!FPackageName::IsValidLongPackageName(AssetPath))
	{
		return FCapabilityResult::MakeArgInvalid(FString::Printf(TEXT("Invalid map path: %s"), *AssetPath));
	}
	if (Action == TEXT("Open") && !FPackageName::DoesPackageExist(AssetPath))
	{
		return FCapabilityResult::MakeFatal(FString::Printf(TEXT("Map package not found: %s"), *AssetPath));
	}
	FString Filename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(AssetPath, Filename, FPackageName::GetMapPackageExtension()))
	{
		return FCapabilityResult::MakeFatal(FString::Printf(TEXT("Path conversion failed: %s"), *AssetPath));
	}
	if (!GEditor)
	{
		return FCapabilityResult::MakeFatal(TEXT("Editor is not available"));
	}
	if (GEditor->PlayWorld || GEditor->IsPlayingSessionInEditor())
	{
		return FCapabilityResult::MakeFatal(TEXT("PIE is running. Stop PIE before opening a map."));
	}

	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	if (Action != TEXT("Open"))
	{
		if (!EditorWorld) return FCapabilityResult::MakeArgInvalid(TEXT("Editor world is unavailable"));
		if (Action != TEXT("LoadStreaming") && Action != TEXT("Show") && Action != TEXT("Hide") && Action != TEXT("MakeCurrent"))
			return FCapabilityResult::MakeArgInvalid(TEXT("Unknown level Action"));
		ULevel* TargetLevel = nullptr;
		for (ULevel* Level : EditorWorld->GetLevels())
			if (Level && Level->GetOutermost()->GetName() == AssetPath) TargetLevel = Level;
		ULevelStreaming* Streaming = nullptr;
		for (ULevelStreaming* Candidate : EditorWorld->GetStreamingLevels())
			if (Candidate && Candidate->GetWorldAssetPackageName() == AssetPath) Streaming = Candidate;
		if (!TargetLevel && Action == TEXT("LoadStreaming") && Streaming)
		{
			Streaming->SetShouldBeLoaded(true);
			EditorWorld->FlushLevelStreaming();
			TargetLevel = Streaming->GetLoadedLevel();
		}
		if (!TargetLevel) return FCapabilityResult::MakeArgInvalid(TEXT("AssetPath must identify an existing sublevel of the open map; use LoadStreaming for an unloaded reference"));
		if (FLevelUtils::IsLevelLocked(TargetLevel)) return FCapabilityResult::MakeArgInvalid(TEXT("Target level is locked"));
		if (Action == TEXT("Show") || Action == TEXT("Hide"))
			EditorLevelUtils::SetLevelVisibility(TargetLevel, Action == TEXT("Show"), false);
		if (Action == TEXT("MakeCurrent"))
		{
			EditorLevelUtils::SetLevelVisibility(TargetLevel, true, false);
			EditorLevelUtils::MakeLevelCurrent(TargetLevel);
		}
		return FNexusCapabilityResultBuilder::Build([&](auto& Entries, auto&, auto& Error)
		{
			auto Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("AssetPath"), AssetPath);
			Entry->SetStringField(TEXT("Action"), Action);
			Entry->SetBoolField(TEXT("bLoaded"), true);
			Entry->SetBoolField(TEXT("bVisible"), TargetLevel->bIsVisible);
			Entry->SetBoolField(TEXT("bCurrent"), TargetLevel == EditorWorld->GetCurrentLevel());
			Entry->SetBoolField(TEXT("bDirty"), TargetLevel->GetOutermost()->IsDirty());
			if ((Action == TEXT("MakeCurrent") && TargetLevel != EditorWorld->GetCurrentLevel())
				|| (Action == TEXT("Show") && !TargetLevel->bIsVisible) || (Action == TEXT("Hide") && TargetLevel->bIsVisible))
				Error = TEXT("Editor did not apply the requested level state");
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		});
	}
	UPackage* EditorPackage = EditorWorld ? EditorWorld->GetOutermost() : nullptr;
	// LoadMap 会卸载关卡及外部对象，不能只检查主 World 包。
	TArray<UPackage*> DirtyWorldPackages;
	FEditorFileUtils::GetDirtyWorldPackages(DirtyWorldPackages);
	if (DirtyWorldPackages.Num() > 0 && !bDiscardUnsaved)
	{
		return FCapabilityResult::MakeFatal(TEXT("Current levels or external Actors have unsaved changes. Save them, or pass bDiscardUnsaved=true."));
	}
	TWeakObjectPtr<UPackage> ClearedPackage;
	if (EditorPackage && EditorPackage->IsDirty())
	{
		if (!bDiscardUnsaved)
		{
			return FCapabilityResult::MakeFatal(TEXT("Current map has unsaved changes. Save it, or pass bDiscardUnsaved=true."));
		}
		// LoadMap 对从未落盘的脏关卡会弹确认框。先清脏标记，避免对话框。
		ClearedPackage = EditorPackage;
		EditorPackage->SetDirtyFlag(false);
	}

	const bool bLoaded = FEditorFileUtils::LoadMap(Filename, false, false);
	if (!bLoaded)
	{
		if (ClearedPackage.IsValid()) ClearedPackage->SetDirtyFlag(true);
		return FCapabilityResult::MakeFatal(FString::Printf(TEXT("Failed to open map: %s"), *AssetPath));
	}

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		(void)OutTop;
		(void)OutError;
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		UWorld* Opened = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		const FString OpenedPath = (Opened && Opened->GetOutermost()) ? Opened->GetOutermost()->GetName() : AssetPath;
		Entry->SetStringField(TEXT("AssetPath"), AssetPath);
		Entry->SetStringField(TEXT("OpenedPath"), OpenedPath);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusControlEditorLevelCapability)

#endif // WITH_EDITOR
