// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Level/NexusGetAssetLevelCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusEditorLevelUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusBlueprintEditUtils.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "AI/Navigation/NavigationDataChunk.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "NexusMcpTool.h"

static TArray<TSharedPtr<FJsonValue>> ReadOwnedPropertySchema(const UStruct* Struct, void* Container, UObject* Root,
	const FString& Prefix, int32 Depth, int32& Remaining)
{
	TArray<TSharedPtr<FJsonValue>> Fields;
	for (TFieldIterator<FProperty> Field(Struct); Field && Remaining > 0; ++Field)
	{
		--Remaining;
		auto Item = MakeShared<FJsonObject>();
		const FString Path = Prefix.IsEmpty() ? Field->GetName() : Prefix + TEXT(".") + Field->GetName();
		Item->SetStringField(TEXT("PropertyPath"), Path);
		Item->SetStringField(TEXT("Name"), Field->GetName());
		Item->SetStringField(TEXT("Type"), Field->GetCPPType());
		Item->SetBoolField(TEXT("bEditable"), Field->HasAnyPropertyFlags(CPF_Edit) && !Field->HasAnyPropertyFlags(CPF_EditConst));
		void* Value = Field->ContainerPtrToValuePtr<void>(Container);
		const UStruct* ChildStruct = nullptr; void* ChildValue = nullptr;
		if (FStructProperty* StructField = CastField<FStructProperty>(*Field)) { ChildStruct = StructField->Struct; ChildValue = Value; }
		else if (FObjectPropertyBase* Reference = CastField<FObjectPropertyBase>(*Field))
		{
			UObject* Object = Reference->GetObjectPropertyValue(Value);
			const bool bOwned = Object && Object != Root && Object->IsIn(Root);
			Item->SetBoolField(TEXT("bNull"), !Object);
			Item->SetBoolField(TEXT("bOwnedSubobject"), bOwned);
			if (bOwned) { ChildStruct = Object->GetClass(); ChildValue = Object; }
		}
		if (ChildStruct)
		{
			if (Depth > 0 && Remaining > 0) Item->SetArrayField(TEXT("Children"), ReadOwnedPropertySchema(ChildStruct, ChildValue, Root, Path, Depth - 1, Remaining));
			else Item->SetBoolField(TEXT("bChildrenTruncated"), true);
		}
		Fields.Add(MakeShared<FJsonValueObject>(Item));
	}
	return Fields;
}

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
		.Prop(TEXT("SchemaDepth"), FNexusSchema::Int(TEXT("ActorPropertySchema: child depth, owned subobjects/structs only"), 1, 0, 3))
		.Prop(TEXT("MaxSchemaProperties"), FNexusSchema::Int(TEXT("ActorPropertySchema: total field budget across all requested roots"), 64, 1, 256))
		.Prop(TEXT("Offset"),      FNexusSchema::Int(TEXT("actors section pagination offset"), 0, 0))
		.Prop(TEXT("Limit"),       FNexusSchema::Int(TEXT("actors section page size"), 100, 1, 500))
		.Required({ TEXT("AssetPath") })
		.Build();
}

TArray<FString> FNexusGetAssetLevelCapability::GetSectionNames() const
{
	return { TEXT("Actors"), TEXT("Settings"), TEXT("ActorProperties"), TEXT("ActorPropertySchema"), TEXT("Navigation") };
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
	else if (SectionName == TEXT("Navigation"))
	{
		ULevel* Level = FNexusEditorLevelUtils::ResolveLevel(World);
		if (!Level) { OutError = TEXT("Requested level is unavailable"); return; }
		InOutDetail->SetStringField(TEXT("LevelPath"), Level->GetOutermost()->GetName());
		InOutDetail->SetBoolField(TEXT("bVisible"), Level->bIsVisible);
		InOutDetail->SetNumberField(TEXT("NavDataChunkCount"), Level->NavDataChunks.Num());
		TArray<TSharedPtr<FJsonValue>> Chunks;
		for (UNavigationDataChunk* Chunk : Level->NavDataChunks)
		{
			if (!Chunk || Chunks.Num() >= 100) continue;
			auto Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("Path"), Chunk->GetPathName());
			Item->SetStringField(TEXT("Class"), Chunk->GetClass()->GetPathName());
			Chunks.Add(MakeShared<FJsonValueObject>(Item));
		}
		InOutDetail->SetArrayField(TEXT("NavDataChunks"), Chunks);
	}
	else if (SectionName == TEXT("Settings"))
	{
		FNexusEditorLevelUtils::AppendLevelSettingsSection(World, InOutDetail);
	}
	else if (SectionName == TEXT("ActorPropertySchema"))
	{
		const FNexusArgs Query(Args);
		AActor* Actor = FNexusEditorLevelUtils::FindLevelActorByNameOrLabel(World, Query.Str(TEXT("ActorName")), false);
		if (!Actor) { OutError = TEXT("ActorPropertySchema requires an exact ActorName from Actors"); return; }
		TArray<FString> Paths = Query.StrArr(TEXT("PropertyPaths"));
		if (Paths.Num() > 32) { OutError = TEXT("ActorPropertySchema accepts at most 32 root paths"); return; }
		if (Paths.Num() == 0) Paths.Add(TEXT(""));
		const int32 Depth = FMath::Clamp(Query.Int(TEXT("SchemaDepth"), 1), 0, 3);
		int32 Remaining = FMath::Clamp(Query.Int(TEXT("MaxSchemaProperties"), 64), 1, 256);
		TArray<TSharedPtr<FJsonValue>> Schemas;
		for (const FString& Path : Paths)
		{
			auto Schema = MakeShared<FJsonObject>(); Schema->SetStringField(TEXT("PropertyPath"), Path);
			const UStruct* Struct = Actor->GetClass(); void* Container = Actor;
			if (!Path.IsEmpty())
			{
				TArray<FString> Parts; Path.ParseIntoArray(Parts, TEXT("."), false);
				FProperty* Field = nullptr; void* Value = nullptr; UObject* Owner = nullptr; FString Error;
				if (!FNexusPropertyUtils::ResolvePropertyWrite(Actor, Parts, 0, Field, Value, Error, &Owner)
					|| !Owner || (Owner != Actor && !Owner->IsIn(Actor)))
				{ Schema->SetStringField(TEXT("Error"), Error.IsEmpty() ? TEXT("Schema path must remain within owned subobjects") : Error); Struct = nullptr; }
				else if (FStructProperty* StructField = CastField<FStructProperty>(Field)) { Struct = StructField->Struct; Container = Value; }
				else if (FObjectPropertyBase* Reference = CastField<FObjectPropertyBase>(Field))
				{
					UObject* Object = Reference->GetObjectPropertyValue(Value);
					if (Object && Object->IsIn(Actor)) { Struct = Object->GetClass(); Container = Object; }
					else { Struct = nullptr; Schema->SetStringField(TEXT("Error"), TEXT("Schema root is a null or external object reference")); }
				}
				else { Struct = nullptr; Schema->SetStringField(TEXT("Error"), TEXT("Schema root must be a struct or owned object")); }
			}
			if (Struct) Schema->SetArrayField(TEXT("Properties"), ReadOwnedPropertySchema(Struct, Container, Actor, Path, Depth, Remaining));
			else OutError = TEXT("One or more schema roots could not be read");
			Schemas.Add(MakeShared<FJsonValueObject>(Schema));
		}
		InOutDetail->SetArrayField(TEXT("PropertySchemas"), Schemas);
		InOutDetail->SetBoolField(TEXT("bSchemaBudgetExhausted"), Remaining == 0);
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
		AActor* Actor = FNexusEditorLevelUtils::FindLevelActorByNameOrLabel(World, ActorName, false);
		if (!Actor)
		{ OutError = TEXT("Exact actor name not found; read Actors first"); return; }
		UObject* Defaults = Actor->GetClass()->GetDefaultObject(false);
		InOutDetail->SetStringField(TEXT("ActorName"), ActorName);
		InOutDetail->SetStringField(TEXT("ActorPath"), Actor->GetPathName());
		InOutDetail->SetStringField(TEXT("LevelPath"), Actor->GetLevel()->GetOutermost()->GetName());
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
					Property->SetBoolField(TEXT("bDefaultUnavailable"), true);
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
