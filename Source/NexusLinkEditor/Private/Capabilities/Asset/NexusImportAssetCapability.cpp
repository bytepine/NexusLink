// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Asset/NexusImportAssetCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetImportTask.h"
#include "Factories/TextureFactory.h"
#include "Factories/SoundFactory.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Animation/Skeleton.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"

void FNexusImportAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ImportAsset");
	Out.Description = TEXT("Create from one external file. Typed texture/WAV/FBX import, explicit collision policy; save separately.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("SourceFile"), FNexusSchema::Str(TEXT("Absolute local file path")))
		.Prop(TEXT("DestinationPath"), FNexusSchema::Str(TEXT("Content folder, e.g. /Game/Imported")))
		.Prop(TEXT("AssetName"), FNexusSchema::Str(TEXT("Valid object name; defaults to source basename")))
		.Prop(TEXT("AssetType"), FNexusSchema::Enum(TEXT("Explicit import type"), {TEXT("Texture"), TEXT("SoundWave"), TEXT("StaticMesh"), TEXT("SkeletalMesh"), TEXT("AnimSequence")}))
		.Prop(TEXT("ConflictPolicy"), FNexusSchema::Enum(TEXT("Existing destination policy"), {TEXT("Fail"), TEXT("Unique"), TEXT("Replace")}, TEXT("Fail")))
		.Prop(TEXT("SkeletonPath"), FNexusSchema::Str(TEXT("Existing skeleton; required for SkeletalMesh/AnimSequence")))
		.Prop(TEXT("bSRGB"), FNexusSchema::Bool(TEXT("Texture sRGB"), true, true))
		.Prop(TEXT("bGenerateCollision"), FNexusSchema::Bool(TEXT("StaticMesh auto collision"), true, true))
		.Required({TEXT("SourceFile"), TEXT("DestinationPath"), TEXT("AssetType")}).Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Editor};
	Out.Prerequisites = {TEXT("EditorOnly")};
	Out.bAutoTransaction = false;
	Out.bAllowBatch = false;
	Out.RelatedCapabilities = {TEXT("ReimportAsset"), TEXT("ExportAsset"), TEXT("SaveAsset")};
	Out.ExtraSearchKeywords = {TEXT("FBX"), TEXT("PNG"), TEXT("WAV"), TEXT("external file")};
}

FCapabilityResult FNexusImportAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& Entries, auto& Top, auto& Error)
	{
		const FNexusArgs Args(Arguments);
		const FString File = Args.Str(TEXT("SourceFile")), Folder = Args.Str(TEXT("DestinationPath"));
		const FString Type = Args.Str(TEXT("AssetType")), Policy = Args.Str(TEXT("ConflictPolicy"), TEXT("Fail"));
		FString Name = Args.Str(TEXT("AssetName"), FPaths::GetBaseFilename(File));
		if (FPaths::IsRelative(File) || !IFileManager::Get().FileExists(*File)) { Error = TEXT("SourceFile must be an existing absolute local file"); return; }
		if (!Folder.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Folder / Name)
			|| Name.IsEmpty() || ObjectTools::SanitizeObjectName(Name) != Name || Name.Contains(TEXT("/")))
		{ Error = TEXT("DestinationPath must be under /Game/ and AssetName must be a valid object name"); return; }
		if (Policy != TEXT("Fail") && Policy != TEXT("Unique") && Policy != TEXT("Replace")) { Error = TEXT("Unknown ConflictPolicy"); return; }
		const FString Extension = FPaths::GetExtension(File).ToLower();
		UFactory* Factory = nullptr;
		if (Type == TEXT("Texture"))
		{
			const TArray<FString> Extensions = {TEXT("png"), TEXT("jpg"), TEXT("jpeg"), TEXT("bmp"), TEXT("tga"), TEXT("exr"), TEXT("hdr")};
			if (!Extensions.Contains(Extension)) { Error = TEXT("Unsupported texture extension"); return; }
			UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
			TextureFactory->SuppressImportOverwriteDialog();
			Factory = TextureFactory;
		}
		else if (Type == TEXT("SoundWave"))
		{
			if (Extension != TEXT("wav")) { Error = TEXT("SoundWave import requires WAV"); return; }
			Factory = NewObject<USoundFactory>();
		}
		else if (Type == TEXT("StaticMesh") || Type == TEXT("SkeletalMesh") || Type == TEXT("AnimSequence"))
		{
			if (Extension != TEXT("fbx")) { Error = TEXT("Mesh/animation import requires FBX"); return; }
			USkeleton* Skeleton = nullptr;
			if (Type != TEXT("StaticMesh"))
			{
				Skeleton = FNexusAssetUtils::LoadAssetWithFallback<USkeleton>(Args.Str(TEXT("SkeletonPath")));
				if (!Skeleton) { Error = TEXT("A valid existing SkeletonPath is required"); return; }
			}
			UFbxFactory* FbxFactory = NewObject<UFbxFactory>();
			UFbxImportUI* Options = FbxFactory->ImportUI;
			Options->bAutomatedImportShouldDetectType = false;
			Options->MeshTypeToImport = Type == TEXT("StaticMesh") ? FBXIT_StaticMesh : Type == TEXT("SkeletalMesh") ? FBXIT_SkeletalMesh : FBXIT_Animation;
			Options->bImportAsSkeletal = Type != TEXT("StaticMesh");
			Options->bImportMesh = Type != TEXT("AnimSequence");
			Options->bImportAnimations = Type == TEXT("AnimSequence");
			Options->bImportMaterials = false;
			Options->bImportTextures = false;
			Options->bCreatePhysicsAsset = false;
			Options->Skeleton = Skeleton;
			Options->StaticMeshImportData->bCombineMeshes = true;
			Options->StaticMeshImportData->bAutoGenerateCollision = Args.Bool(TEXT("bGenerateCollision"), true);
			Factory = FbxFactory;
		}
		else { Error = TEXT("Unknown AssetType"); return; }
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		FString Package = Folder / Name;
		if (Policy == TEXT("Unique")) AssetTools.CreateUniqueAssetName(Package, TEXT(""), Package, Name);
		else if (Policy == TEXT("Fail") && (FPackageName::DoesPackageExist(Package) || FindPackage(nullptr, *Package)))
		{ Error = TEXT("Destination already exists; choose Unique or explicit Replace"); return; }
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = File; Task->DestinationPath = Folder; Task->DestinationName = Name;
		Task->bAutomated = true; Task->bReplaceExisting = Policy == TEXT("Replace"); Task->bReplaceExistingSettings = Task->bReplaceExisting;
		Task->bSave = false; Task->Factory = Factory;
		if (UFbxFactory* FbxFactory = Cast<UFbxFactory>(Factory)) Task->Options = FbxFactory->ImportUI;
		AssetTools.ImportAssetTasks({Task});
		// 工厂的实际产物是结果依据，不能用输入文件名假定只有一个产物。
		for (const FString& Path : Task->ImportedObjectPaths)
		{
			UObject* Asset = FNexusAssetUtils::LoadAssetWithFallback<UObject>(Path);
			if (!Asset) { Error = TEXT("Imported asset could not be read back: ") + Path; continue; }
			if (Type == TEXT("Texture"))
			{
				if (FBoolProperty* SRGB = FindFProperty<FBoolProperty>(Asset->GetClass(), TEXT("SRGB")))
				{ SRGB->SetPropertyValue_InContainer(Asset, Args.Bool(TEXT("bSRGB"), true)); Asset->PostEditChange(); }
			}
			auto Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("Path"), Asset->GetPathName()); Entry->SetStringField(TEXT("AssetClass"), Asset->GetClass()->GetName());
			Entry->SetBoolField(TEXT("bVerified"), true); Entry->SetBoolField(TEXT("bSaved"), false);
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		if (Entries.Num() == 0 && Error.IsEmpty()) Error = TEXT("Import failed or produced no assets; inspect GetOutputLog");
	});
}
REGISTER_MCP_CAPABILITY(FNexusImportAssetCapability)
