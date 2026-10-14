// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusSaveAssetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusAssetEditorUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "UObject/Package.h"

#include "NexusMcpTool.h"
#if WITH_EDITOR
#include "UObject/UObjectGlobals.h"
#endif

void FNexusSaveAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("SaveAsset");
	Out.Description = TEXT("Persist asset package to disk. MarkPackageDirty then save.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Single asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("persist"), TEXT("commit"), TEXT("flush"), TEXT("package"), TEXT("disk") };
	Out.RelatedCapabilities = { TEXT("RenameAsset"), TEXT("DeleteAsset") };
}

FCapabilityResult FNexusSaveAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

	#if !WITH_EDITOR
		OutError = TEXT("SaveAsset only available in editor mode");
		return;
	#else
		const FString OrigPath = A.Str(TEXT("AssetPath"));

		int32 SavedCount = 0;
		int32 FailedCount = 0;
		int32 DeferredCount = 0;

		FString PackagePath = OrigPath;
		int32 DotIdx;
		if (PackagePath.FindLastChar(TEXT('.'), DotIdx))
		{
			PackagePath = PackagePath.Left(DotIdx);
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), OrigPath);

		UPackage* Pkg = FindPackage(nullptr, *PackagePath);
		if (!Pkg) Pkg = LoadPackage(nullptr, *PackagePath, LOAD_None);
		if (!Pkg)
		{
			++FailedCount;
			Entry->SetBoolField(TEXT("bSuccess"), false);
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Package not found: %s"), *PackagePath));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		else
		{
			bool bDeferred = false;
			FString Note;
			const bool bOk = FNexusAssetEditorUtils::SaveDirtyPackage(Pkg, PackagePath, OrigPath, bDeferred, Note);

			if (bDeferred)
			{
				++DeferredCount;
				Entry->SetBoolField(TEXT("bDeferred"), true);
				if (!Note.IsEmpty())
				{
					Entry->SetStringField(TEXT("Note"), Note);
				}
			}
			else if (bOk)
			{
				++SavedCount;
			}
			else
			{
				++FailedCount;
				Entry->SetBoolField(TEXT("bSuccess"), false);
				const FString ErrorText = Note.IsEmpty()
					? FString::Printf(TEXT("SavePackage failed: %s"), *PackagePath)
					: FString::Printf(TEXT("%s（%s）"), *PackagePath, *Note);
				Entry->SetStringField(TEXT("Error"), ErrorText);
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}

		OutTop->SetNumberField(TEXT("bSaved"), SavedCount);
		OutTop->SetNumberField(TEXT("Skipped"), 0);
		OutTop->SetNumberField(TEXT("Failed"), FailedCount);
		if (DeferredCount > 0)
		{
			OutTop->SetNumberField(TEXT("bDeferred"), DeferredCount);
		}
	#endif
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusSaveAssetCapability)
