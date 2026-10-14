// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Level/NexusGetAssetLevelCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusEditorLevelUtils.h"
#include "Engine/World.h"
#include "NexusMcpTool.h"

void FNexusGetAssetLevelCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetLevel");
	Out.SearchAssetTypes = {TEXT("World")};
	Out.Description = TEXT("Inspect level snapshot. Writes via ManageAssetLevel.");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.Prerequisites = { TEXT("EditorOnly") };
	Out.ExtraSearchKeywords = { TEXT("Level"), TEXT("map"), TEXT("umap"), TEXT("World"), TEXT("Actor") };
	Out.RelatedCapabilities = { TEXT("ManageAssetLevel"), TEXT("CreateAssetLevel"), TEXT("SearchAsset"), TEXT("ListRuntimeActors"), TEXT("GetAssetRefs") };
	Out.WhenToUse = TEXT("Read on-disk level; WorldSettings via ManageAssetLevel");
}

TSharedPtr<FJsonObject> FNexusGetAssetLevelCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("Level asset path (/Game/.../*.umap package path)")))
		.Prop(TEXT("ClassFilter"), FNexusSchema::Str(TEXT("Actor class name filter (optional)")))
		.Prop(TEXT("NameFilter"),  FNexusSchema::Str(TEXT("Actor name/tag filter (optional)")))
		.Prop(TEXT("TagFilter"),   FNexusSchema::Str(TEXT("Actor Tag exact match (optional)")))
		.Prop(TEXT("Offset"),      FNexusSchema::Int(TEXT("actors section pagination offset"), 0, 0))
		.Prop(TEXT("Limit"),       FNexusSchema::Int(TEXT("actors section page size"), 100, 1, 500))
		.Required({ TEXT("AssetPath") })
		.Build();
}

TArray<FString> FNexusGetAssetLevelCapability::GetSectionNames() const
{
	return { TEXT("Actors"), TEXT("Settings") };
}

TArray<FString> FNexusGetAssetLevelCapability::GetDefaultSectionNames() const
{
	return { TEXT("Actors"), TEXT("Settings") };
}

bool FNexusGetAssetLevelCapability::PrepareEntry(const TSharedPtr<FJsonObject>& Args,
                                            TSharedPtr<FJsonObject>&       OutEntry,
                                            void*&                         OutTargetOpaque,
                                            FString&                       OutError) const
{
	FString AssetPath;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("AssetPath"), AssetPath) || AssetPath.IsEmpty())
	{
		OutError = TEXT("AssetPath is required");
		return false;
	}

	bool bEditorWorld = false;
	UWorld* World = FNexusEditorLevelUtils::LoadLevelWorldForRead(AssetPath, bEditorWorld, OutError);
	if (!World)
	{
		return false;
	}

	OutEntry->SetStringField(TEXT("Path"), AssetPath);
	OutEntry->SetStringField(TEXT("PackagePath"), FNexusEditorLevelUtils::NormalizeLevelPackagePath(AssetPath));
	OutEntry->SetStringField(TEXT("WorldName"), World->GetName());
	OutEntry->SetBoolField(TEXT("bIsEditorWorld"), bEditorWorld);

	OutTargetOpaque = static_cast<void*>(World);
	return true;
}

void FNexusGetAssetLevelCapability::ExecuteSection(const FString&                 SectionName,
                                              const TSharedPtr<FJsonObject>& Args,
                                              void*                          TargetOpaque,
                                              TSharedPtr<FJsonObject>&       InOutDetail,
                                              FString&                       OutError) const
{
	UWorld* World = static_cast<UWorld*>(TargetOpaque);
	if (!World)
	{
		OutError = TEXT("Internal error: World is empty");
		return;
	}

	if (SectionName == TEXT("Actors"))
	{
		FNexusEditorLevelUtils::AppendLevelActorsSection(World, Args, InOutDetail);
	}
	else if (SectionName == TEXT("Settings"))
	{
		FNexusEditorLevelUtils::AppendLevelSettingsSection(World, InOutDetail);
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown section: %s"), *SectionName);
	}
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetLevelCapability)
