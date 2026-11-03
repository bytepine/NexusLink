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
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "FbxImporter.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "ObjectTools.h"
#include "UObject/Package.h"

// 同时预检单个有效 take 与多个有效 take 的命名；无效 take 不得使已有资产失去保护。
static bool ReadFbxImportSuffixes(const FString& File, bool bSkeletalMesh, TArray<FString>& Suffixes, FString& Error)
{
	FbxManager* Manager = FbxManager::Create();
	if (!Manager) { Error = TEXT("Could not create FBX reader"); return false; }
	ON_SCOPE_EXIT { Manager->Destroy(); };
	Manager->SetIOSettings(FbxIOSettings::Create(Manager, IOSROOT));
	FbxImporter* Importer = FbxImporter::Create(Manager, "NexusImportPreflight");
	FbxScene* Scene = FbxScene::Create(Manager, "NexusImportPreflight");
	if (!Importer->Initialize(TCHAR_TO_UTF8(*File), -1, Manager->GetIOSettings()) || !Importer->Import(Scene))
	{ Error = TEXT("Could not inspect FBX import destinations"); return false; }
	Suffixes.Add(FString());
	if (bSkeletalMesh)
	{
		int32 MeshNodes = 0;
		TArray<FbxNode*> Pending = {Scene->GetRootNode()};
		while (Pending.Num())
		{
			FbxNode* Node = Pending.Pop();
			if (Node->GetMesh()) ++MeshNodes;
			for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex) Pending.Add(Node->GetChild(ChildIndex));
		}
		// 多网格组会忽略目标基础名；首批导入拒绝这类文件，避免绕过任何冲突策略。
		if (MeshNodes != 1) { Error = TEXT("SkeletalMesh import requires exactly one mesh node; split multi-mesh/LOD FBX files before importing"); return false; }
		return true;
	}
	if (Scene->GetSrcObjectCount<FbxAnimStack>() > 1)
	{
		TSet<FName> TakenSuffixes;
		for (int32 StackIndex = 0; StackIndex < Scene->GetSrcObjectCount<FbxAnimStack>(); ++StackIndex)
		{
			const FString Suffix = TEXT("_") + ObjectTools::SanitizeObjectName(UTF8_TO_TCHAR(Scene->GetSrcObject<FbxAnimStack>(StackIndex)->GetName()));
			if (TakenSuffixes.Contains(FName(*Suffix))) { Error = TEXT("FBX take names collide after sanitization"); return false; }
			TakenSuffixes.Add(FName(*Suffix));
			Suffixes.Add(Suffix);
		}
	}
	return true;
}

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
			FbxFactory->SetDetectImportTypeOnImport(false);
			UFbxImportUI* Options = FbxFactory->ImportUI;
			Options->bAutomatedImportShouldDetectType = false;
			Options->bOverrideFullName = true;
			Options->MeshTypeToImport = Type == TEXT("StaticMesh") ? FBXIT_StaticMesh : Type == TEXT("SkeletalMesh") ? FBXIT_SkeletalMesh : FBXIT_Animation;
			Options->OriginalImportType = Options->MeshTypeToImport;
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
		TArray<FString> Suffixes = {FString()};
		if (Type == TEXT("AnimSequence") || Type == TEXT("SkeletalMesh"))
		{
			Suffixes.Empty();
			if (!ReadFbxImportSuffixes(File, Type == TEXT("SkeletalMesh"), Suffixes, Error)) return;
		}
		const auto HasConflict = [&](const FString& BaseName)
		{
			for (const FString& Suffix : Suffixes)
			{
				const FString Destination = Folder / (BaseName + Suffix);
				if (FPackageName::DoesPackageExist(Destination) || FindPackage(nullptr, *Destination)) return true;
			}
			return false;
		};
		if (Policy == TEXT("Unique"))
		{
			const FString BaseName = Name;
			int32 Attempt = 0;
			while (HasConflict(Name))
			{
				if (++Attempt > 10000) { Error = TEXT("Could not find an unused destination for all FBX takes"); return; }
				Name = BaseName + FString::Printf(TEXT("_%d"), Attempt);
			}
		}
		else if (Policy == TEXT("Fail") && HasConflict(Name))
		{ Error = TEXT("An import destination already exists; choose Unique or explicit Replace"); return; }
		TSet<FName> AnimationPackages;
		TSet<FString> ImportedPaths;
		if (Type == TEXT("AnimSequence")) for (const FString& Suffix : Suffixes) AnimationPackages.Add(FName(*(Folder / (Name + Suffix))));
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		// 引擎 FBX 工厂仅返回最后一个动画；同时收集新产物和已存在动画的实际修改通知。
		const FDelegateHandle Added = Registry.OnAssetAdded().AddLambda([&](const FAssetData& Data)
		{
			if (AnimationPackages.Contains(Data.PackageName))
				if (UObject* Asset = Data.GetAsset()) if (Asset->IsA(UAnimSequence::StaticClass())) ImportedPaths.Add(Asset->GetPathName());
		});
		const FDelegateHandle Dirtied = UPackage::PackageMarkedDirtyEvent.AddLambda([&](UPackage* DirtyPackage, bool)
		{
			if (DirtyPackage && AnimationPackages.Contains(DirtyPackage->GetFName()))
				if (UAnimSequence* Animation = FindObject<UAnimSequence>(DirtyPackage, *FPackageName::GetShortName(DirtyPackage->GetName()))) ImportedPaths.Add(Animation->GetPathName());
		});
		ON_SCOPE_EXIT { Registry.OnAssetAdded().Remove(Added); UPackage::PackageMarkedDirtyEvent.Remove(Dirtied); };
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = File; Task->DestinationPath = Folder; Task->DestinationName = Name;
		Task->bAutomated = true; Task->bReplaceExisting = Policy == TEXT("Replace"); Task->bReplaceExistingSettings = Task->bReplaceExisting;
		Task->bSave = false; Task->Factory = Factory;
		if (UFbxFactory* FbxFactory = Cast<UFbxFactory>(Factory)) Task->Options = FbxFactory->ImportUI;
		AssetTools.ImportAssetTasks({Task});
		// 工厂的实际产物是结果依据，不能用输入文件名假定只有一个产物。
		for (const FString& Path : Task->ImportedObjectPaths) ImportedPaths.Add(Path);
		TArray<FString> Paths = ImportedPaths.Array(); Paths.Sort();
		for (const FString& Path : Paths)
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
