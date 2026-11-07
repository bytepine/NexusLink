// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/Context/NexusGetEditorContextCapability.h"

#if WITH_EDITOR

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusEditorContextUtils.h"
#include "NexusMcpTool.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"

void FNexusGetEditorContextCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetEditorContext");
	Out.Description = TEXT("Read selection, browse path, current map and open asset editors. Editor world differs from PIE.");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.Prerequisites = { TEXT("EditorOnly") };
	Out.ExtraSearchKeywords = {
		TEXT("selection"), TEXT("content browser"), TEXT("picker"), TEXT("focused"),
		TEXT("current map"), TEXT("open level"), TEXT("loaded map")
	};
	Out.RelatedCapabilities = { TEXT("GetEditorInfo"), TEXT("CaptureViewport"), TEXT("SearchAsset"), TEXT("ControlEditorAssetEditor"), TEXT("GetEditorSlate") };
	Out.WhenToUse = TEXT("Read editor selection or current map; use ListRuntimeActors in PIE");
}

TSharedPtr<FJsonObject> FNexusGetEditorContextCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("Limit"), FNexusSchema::Int(TEXT("Max list items (selection section)"), 100, 1, 500))
		.Build();
}

TArray<FString> FNexusGetEditorContextCapability::GetSectionNames() const
{
	return { TEXT("SelectionActors"), TEXT("SelectionAssets"), TEXT("ContentBrowserPath"), TEXT("CurrentMap"), TEXT("AssetEditors") };
}

TArray<FString> FNexusGetEditorContextCapability::GetDefaultSectionNames() const
{
	return { TEXT("SelectionActors"), TEXT("SelectionAssets"), TEXT("ContentBrowserPath"), TEXT("CurrentMap") };
}

void FNexusGetEditorContextCapability::ExecuteSection(const FString&                 SectionName,
                                                 const TSharedPtr<FJsonObject>& Args,
                                                 void*                          /*TargetOpaque*/,
                                                 TSharedPtr<FJsonObject>&       InOutDetail,
                                                 FString&                       OutError) const
{
#if !WITH_EDITOR
	OutError = TEXT("GetEditorContext only available in editor builds");
	return;
#endif

	int32 Limit = 100;
	if (Args.IsValid() && Args->HasField(TEXT("Limit")))
	{
		Limit = FMath::Clamp(static_cast<int32>(Args->GetNumberField(TEXT("Limit"))), 1, 500);
	}

	if (SectionName == TEXT("SelectionActors"))
	{
		TArray<TSharedPtr<FJsonValue>> Actors;
		int32 Count = 0;
		FNexusEditorContextUtils::CollectSelectionActors(Actors, Count, Limit);
		InOutDetail->SetArrayField(TEXT("Actors"), Actors);
		InOutDetail->SetNumberField(TEXT("Count"), Actors.Num());
		InOutDetail->SetBoolField(TEXT("bIsEditorWorld"), true);
	}
	else if (SectionName == TEXT("SelectionAssets"))
	{
		TArray<TSharedPtr<FJsonValue>> Assets;
		int32 Count = 0;
		FNexusEditorContextUtils::CollectSelectionAssets(Assets, Count, Limit);
		InOutDetail->SetArrayField(TEXT("Assets"), Assets);
		InOutDetail->SetNumberField(TEXT("Count"), Assets.Num());
	}
	else if (SectionName == TEXT("AssetEditors"))
	{
		UAssetEditorSubsystem* Editors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (!Editors) { OutError = TEXT("Asset editor subsystem is unavailable"); return; }
		TArray<TSharedPtr<FJsonValue>> Assets;
		const auto OpenAssets = Editors->GetAllEditedAssets();
		for (UObject* Asset : OpenAssets)
		{
			if (!Asset || Assets.Num() >= Limit) continue;
			auto Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("AssetPath"), Asset->GetPathName());
			Entry->SetStringField(TEXT("AssetClass"), Asset->GetClass()->GetPathName());
			Entry->SetBoolField(TEXT("bDirty"), Asset->GetOutermost()->IsDirty());
			TArray<TSharedPtr<FJsonValue>> Names;
			for (IAssetEditorInstance* Editor : Editors->FindEditorsForAsset(Asset)) if (Editor) Names.Add(MakeShared<FJsonValueString>(Editor->GetEditorName().ToString()));
			Entry->SetArrayField(TEXT("EditorNames"), Names);
			Assets.Add(MakeShared<FJsonValueObject>(Entry));
		}
		InOutDetail->SetArrayField(TEXT("Assets"), Assets);
		InOutDetail->SetNumberField(TEXT("TotalCount"), OpenAssets.Num());
		InOutDetail->SetBoolField(TEXT("bTruncated"), OpenAssets.Num() > Assets.Num());
	}
	else if (SectionName == TEXT("ContentBrowserPath"))
	{
		FString Path;
		if (!FNexusEditorContextUtils::CollectContentBrowserPath(Path, OutError))
		{
			return;
		}
		InOutDetail->SetStringField(TEXT("Path"), Path);
	}
	else if (SectionName == TEXT("CurrentMap"))
	{
		FString AssetPath;
		FString MapName;
		if (!FNexusEditorContextUtils::CollectCurrentEditorMap(AssetPath, MapName, OutError))
		{
			return;
		}
		InOutDetail->SetStringField(TEXT("AssetPath"), AssetPath);
		InOutDetail->SetStringField(TEXT("MapName"), MapName);
		InOutDetail->SetBoolField(TEXT("bIsEditorWorld"), true);
	}
	else
	{
		OutError = FString::Printf(TEXT("Unhandled section '%s'"), *SectionName);
	}
}

REGISTER_MCP_CAPABILITY(FNexusGetEditorContextCapability)

#endif // WITH_EDITOR
