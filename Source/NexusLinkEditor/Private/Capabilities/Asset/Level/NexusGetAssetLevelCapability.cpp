// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Level/NexusGetAssetLevelCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusEditorLevelUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusBlueprintEditUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "NexusMcpTool.h"

void FNexusGetAssetLevelCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetLevel");
	Out.SearchAssetTypes = {TEXT("World")};
	Out.Description = TEXT("Inspect level actors, settings and object property values versus class defaults; no writes.");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.Prerequisites = { TEXT("EditorOnly") };
	Out.ExtraSearchKeywords = { TEXT("Level"), TEXT("map"), TEXT("umap"), TEXT("World"), TEXT("Actor"), TEXT("object"), TEXT("property") };
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
		.Prop(TEXT("ActorName"),   FNexusSchema::Str(TEXT("Exact actor name from Actors for ActorProperties")))
		.Prop(TEXT("PropertyPaths"), FNexusSchema::StrArr(TEXT("ActorProperties: explicit reflected property paths (max 32); returns Value, DefaultValue and bDiffersFromDefault")))
		.Prop(TEXT("Offset"),      FNexusSchema::Int(TEXT("actors section pagination offset"), 0, 0))
		.Prop(TEXT("Limit"),       FNexusSchema::Int(TEXT("actors section page size"), 100, 1, 500))
		.Required({ TEXT("AssetPath") })
		.Build();
}

TArray<FString> FNexusGetAssetLevelCapability::GetSectionNames() const
{
	return { TEXT("Actors"), TEXT("Settings"), TEXT("ActorProperties") };
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
	else if (SectionName == TEXT("ActorProperties"))
	{
		const FString ActorName = FNexusArgs(Args).Str(TEXT("ActorName"));
		TArray<FString> Paths;
		FNexusPropertyUtils::ReadStringArray(Args, TEXT("PropertyPaths"), Paths);
		if (ActorName.IsEmpty() && Paths.Num() == 0)
		{ InOutDetail->SetStringField(TEXT("Note"), TEXT("Provide ActorName and PropertyPaths for actor/default property values")); return; }
		if (ActorName.IsEmpty() || Paths.Num() == 0 || Paths.Num() > 32)
		{ OutError = TEXT("ActorProperties requires ActorName and 1..32 PropertyPaths"); return; }
		AActor* Actor = nullptr;
		for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
		{
			if (ActorIt->GetName() != ActorName) continue;
			if (Actor) { OutError = TEXT("ActorName is ambiguous across loaded levels"); return; }
			Actor = *ActorIt;
		}
		if (!Actor)
		{ OutError = TEXT("Exact actor name not found; read Actors first"); return; }
		UObject* Defaults = Actor->GetClass()->GetDefaultObject(false);
		InOutDetail->SetStringField(TEXT("ActorName"), ActorName);
		if (Defaults) InOutDetail->SetStringField(TEXT("DefaultObjectPath"), Defaults->GetPathName());
		TArray<TSharedPtr<FJsonValue>> Properties;
		for (const FString& Path : Paths)
		{
			TSharedPtr<FJsonObject> Property = MakeShared<FJsonObject>();
			Property->SetStringField(TEXT("PropertyPath"), Path);
			TArray<FString> Segments;
			Path.ParseIntoArray(Segments, TEXT("."), false);
			FProperty* Field = nullptr;
			void* Value = nullptr;
			FString Error;
			if (!FNexusPropertyUtils::ResolvePropertyWrite(Actor, Segments, 0, Field, Value, Error))
			{ Property->SetStringField(TEXT("Error"), Error); OutError = TEXT("One or more actor properties could not be read"); }
			else
			{
				FString Text;
				FNexusPropertyUtils::ExportText(Field, Text, Value);
				Property->SetStringField(TEXT("Type"), Field->GetCPPType());
				Property->SetStringField(TEXT("Value"), Text);
				FProperty* DefaultField = nullptr;
				void* DefaultValue = nullptr;
				if (FNexusBlueprintEditUtils::ResolveActorDefaultProperty(Actor, Segments, DefaultField, DefaultValue, Error)
					&& DefaultField == Field)
				{
					Text.Reset();
					FNexusPropertyUtils::ExportText(DefaultField, Text, DefaultValue);
					Property->SetStringField(TEXT("DefaultValue"), Text);
					Property->SetBoolField(TEXT("bDiffersFromDefault"), !Field->Identical(Value, DefaultValue, PPF_None));
				}
				else
				{
					Property->SetStringField(TEXT("DefaultError"), Error.IsEmpty() ? TEXT("Matching class default is unavailable") : Error);
					OutError = TEXT("One or more class default properties could not be read");
				}
			}
			Properties.Add(MakeShared<FJsonValueObject>(Property));
		}
		InOutDetail->SetArrayField(TEXT("Properties"), Properties);
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown section: %s"), *SectionName);
	}
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetLevelCapability)
