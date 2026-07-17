// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusSaveAssetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "UObject/Package.h"

#include "NexusMcpTool.h"
#if WITH_EDITOR
#include "UObject/UObjectGlobals.h"
#endif

void FSaveAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("save_asset");
	Out.Description = TEXT("持久化资产包到磁盘。先 MarkPackageDirty 再落盘。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("单个资产路径")))
		.Prop(TEXT("assetPaths"), FNexusSchema::StrArr(TEXT("多个资产路径（批量）")))
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("persist"), TEXT("commit"), TEXT("flush"), TEXT("package"), TEXT("disk") };
	Out.RelatedCapabilities = { TEXT("rename_asset"), TEXT("delete_asset") };
}

FCapabilityResult FSaveAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

	#if !WITH_EDITOR
		OutError = TEXT("save_asset 仅在编辑器模式可用");
		return;
	#else
		TArray<FString> Paths;
		if (Arguments.IsValid())
		{
			FString Single;
			if (Arguments->TryGetStringField(TEXT("assetPath"), Single) && !Single.IsEmpty())
			{
				Paths.Add(Single);
			}
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (Arguments->TryGetArrayField(TEXT("assetPaths"), Arr) && Arr)
			{
				for (const TSharedPtr<FJsonValue>& V : *Arr)
				{
					FString P;
					if (V.IsValid() && V->TryGetString(P) && !P.IsEmpty())
					{
						Paths.AddUnique(P);
					}
				}
			}
		}
		if (Paths.Num() == 0)
		{
			OutError = TEXT("需要 assetPath 或 assetPaths");
			return;
		}

		int32 SavedCount = 0;
		int32 FailedCount = 0;
		int32 DeferredCount = 0;

		for (const FString& OrigPath : Paths)
		{
			FString PackagePath = OrigPath;
			int32 DotIdx;
			if (PackagePath.FindLastChar(TEXT('.'), DotIdx))
			{
				PackagePath = PackagePath.Left(DotIdx);
			}

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), OrigPath);

			UPackage* Pkg = FindPackage(nullptr, *PackagePath);
			if (!Pkg) Pkg = LoadPackage(nullptr, *PackagePath, LOAD_None);
			if (!Pkg)
			{
				++FailedCount;
				Entry->SetBoolField(TEXT("success"), false);
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("%s（包未找到）"), *PackagePath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			bool bDeferred = false;
			FString Note;
			const bool bOk = FNexusAssetUtils::SaveDirtyPackage(Pkg, PackagePath, OrigPath, bDeferred, Note);

			if (bDeferred)
			{
				++DeferredCount;
				Entry->SetBoolField(TEXT("deferred"), true);
				if (!Note.IsEmpty())
				{
					Entry->SetStringField(TEXT("note"), Note);
				}
			}
			else if (bOk)
			{
				++SavedCount;
			}
			else
			{
				++FailedCount;
				Entry->SetBoolField(TEXT("success"), false);
				const FString ErrorText = Note.IsEmpty()
					? FString::Printf(TEXT("%s（SavePackage 失败）"), *PackagePath)
					: FString::Printf(TEXT("%s（%s）"), *PackagePath, *Note);
				Entry->SetStringField(TEXT("error"), ErrorText);
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}

		OutTop->SetNumberField(TEXT("saved"), SavedCount);
		OutTop->SetNumberField(TEXT("skipped"), 0);
		OutTop->SetNumberField(TEXT("failed"), FailedCount);
		if (DeferredCount > 0)
		{
			OutTop->SetNumberField(TEXT("deferred"), DeferredCount);
		}
	#endif
	
	});
}

REGISTER_MCP_CAPABILITY(FSaveAssetCapability)
