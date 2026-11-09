// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusDeleteAssetCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "ObjectTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "Engine/World.h"
#include "NexusMcpTool.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "HAL/FileManager.h"

static void AppendDeleteDiagnostics(const FString& PackagePath, const FString& Extension,
	const TArray<TWeakObjectPtr<UObject>>& Objects, IAssetRegistry& Registry, const TSharedPtr<FJsonObject>& Entry)
{
	TArray<FName> Referencers;
	Registry.GetReferencers(FName(*PackagePath), Referencers);
	TArray<TSharedPtr<FJsonValue>> Disk, Memory, Editors;
	for (const FName& Referencer : Referencers)
		if (Referencer != FName(*PackagePath) && Disk.Num() < 32) Disk.Add(MakeShared<FJsonValueString>(Referencer.ToString()));
	bool bReferenced = false, bUndo = false;
	for (const auto& WeakObject : Objects)
	{
		UObject* Object = WeakObject.Get();
		if (!Object) continue;
		bool bObjectReferenced = false, bObjectUndo = false;
		FReferencerInformationList References;
		ObjectTools::GatherObjectReferencersForDeletion(Object, bObjectReferenced, bObjectUndo, &References, true);
		bReferenced |= bObjectReferenced; bUndo |= bObjectUndo;
		for (const auto& Reference : References.ExternalReferences)
		{
			if (!Reference.Referencer || Memory.Num() >= 32) continue;
			auto Detail = MakeShared<FJsonObject>();
			Detail->SetStringField(TEXT("ObjectPath"), Reference.Referencer->GetPathName());
			Detail->SetNumberField(TEXT("ReferenceCount"), Reference.TotalReferences);
			TArray<TSharedPtr<FJsonValue>> Properties;
			for (const FProperty* Property : Reference.ReferencingProperties)
				if (Property && Properties.Num() < 16) Properties.Add(MakeShared<FJsonValueString>(Property->GetName()));
			Detail->SetArrayField(TEXT("Properties"), Properties);
			Memory.Add(MakeShared<FJsonValueObject>(Detail));
		}
		UAssetEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (Subsystem && Subsystem->FindEditorForAsset(Object, false)) Editors.Add(MakeShared<FJsonValueString>(Object->GetPathName()));
	}
	Entry->SetArrayField(TEXT("DiskReferencers"), Disk);
	Entry->SetArrayField(TEXT("MemoryReferencers"), Memory);
	Entry->SetArrayField(TEXT("OpenAssetEditors"), Editors);
	Entry->SetBoolField(TEXT("bReferencedInMemory"), bReferenced);
	Entry->SetBoolField(TEXT("bReferencedByUndo"), bUndo);
	FString FilePath;
	if (FPackageName::TryConvertLongPackageNameToFilename(PackagePath, FilePath, Extension))
		Entry->SetBoolField(TEXT("bFileReadOnly"), IFileManager::Get().IsReadOnly(*FilePath));
	Entry->SetStringField(TEXT("Note"), TEXT("Diagnostics are bounded snapshots after the failed deletion. Resolve references or explicitly save/close editors; no force delete was attempted."));
}

void FNexusDeleteAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("DeleteAsset");
	Out.Description = TEXT("Delete single asset package. Best-effort redirector cleanup; irreversible.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset path, e.g. '/Game/BP/BP_MyActor'")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("trash"), TEXT("Remove"), TEXT("package"), TEXT("uasset"), TEXT("cleanup") };
	Out.RelatedCapabilities = { TEXT("SaveAsset"), TEXT("RenameAsset") };
}

FCapabilityResult FNexusDeleteAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);


		const FString DeletePath = A.Str(TEXT("AssetPath"));

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		FString PackagePath = DeletePath;
		int32 DotIdx;
		if (PackagePath.FindLastChar(TEXT('.'), DotIdx))
		{
			PackagePath = PackagePath.Left(DotIdx);
		}

		TArray<FAssetData> Assets;
		Registry.GetAssetsByPackageName(FName(*PackagePath), Assets);
		if (Assets.Num() == 0)
		{
			UObject* Obj = LoadObject<UObject>(nullptr, *DeletePath);
			if (!Obj)
			{
				Obj = LoadObject<UObject>(nullptr, *(PackagePath + TEXT(".") + FPaths::GetBaseFilename(PackagePath)));
			}
			if (Obj)
			{
				Assets.Add(FAssetData(Obj));
			}
		}

		if (Assets.Num() == 0)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetBoolField(TEXT("bSuccess"), false);
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Asset not found: %s"), *PackagePath));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		TArray<UObject*> ObjectsToDelete;
		for (const FAssetData& AD : Assets)
		{
			UObject* Obj = AD.GetAsset();
			if (Obj)
			{
				ObjectsToDelete.Add(Obj);
			}
		}

		FString PackageExt = FPackageName::GetAssetPackageExtension();
		for (UObject* Obj : ObjectsToDelete)
		{
			if (!Obj)
			{
				continue;
			}
			PackageExt = FNexusAssetUtils::GetPackageFileExtension(Obj->GetOutermost(), Obj);
			if (PackageExt == FPackageName::GetMapPackageExtension())
			{
				break;
			}
		}

		TArray<TWeakObjectPtr<UObject>> WeakObjects;
		for (UObject* Object : ObjectsToDelete) WeakObjects.Add(Object);
		const int32 Count = ObjectsToDelete.Num() > 0 ? ObjectTools::DeleteObjects(ObjectsToDelete, false) : 0;
		const bool bDeleted = Count > 0 && Count == ObjectsToDelete.Num();

		if (bDeleted)
		{
			TArray<FAssetData> Redirectors;
			TArray<FAssetData> RemainAssets;
			Registry.GetAssetsByPackageName(FName(*PackagePath), RemainAssets);
			for (const FAssetData& AD : RemainAssets)
			{
				if (NEXUS_ASSET_CLASS_NAME(AD).ToString().Contains(TEXT("ObjectRedirector")))
				{
					Redirectors.Add(AD);
				}
			}
			if (Redirectors.Num() > 0)
			{
				TArray<UObject*> RedirObjs;
				for (const FAssetData& AD : Redirectors)
				{
					UObject* Obj = AD.GetAsset();
					if (Obj)
					{
						RedirObjs.Add(Obj);
					}
				}
				if (RedirObjs.Num() > 0)
				{
					ObjectTools::DeleteObjects(RedirObjs, false);
				}
			}

			FString FilePath;
			if (FPackageName::TryConvertLongPackageNameToFilename(PackagePath, FilePath, PackageExt) &&
				!FPaths::FileExists(FilePath))
			{
				TArray<FString> DeletedFilePaths = { FilePath };
				Registry.ScanModifiedAssetFiles(DeletedFilePaths);
			}
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		if (bDeleted)
		{
		}
		else
		{
			Entry->SetBoolField(TEXT("bSuccess"), false);
			Entry->SetStringField(TEXT("Error"),
				FString::Printf(TEXT("%s (deletion incomplete; inspect reference/editor/file diagnostics)"), *PackagePath));
			Entry->SetNumberField(TEXT("DeletedCount"), Count);
			AppendDeleteDiagnostics(PackagePath, PackageExt, WeakObjects, Registry, Entry);
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusDeleteAssetCapability)

#endif // WITH_EDITOR
