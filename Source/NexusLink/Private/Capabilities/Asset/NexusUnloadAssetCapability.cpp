// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusUnloadAssetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusPackageLedger.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

void FUnloadAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("unload_asset");
	Out.Description = TEXT("手动卸载已加载资产包。兜底用；日常无需调用，内存高水位机制会自动卸载。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("单个资产路径")))
		.Prop(TEXT("assetPaths"), FNexusSchema::StrArr(TEXT("多个资产路径（批量）")))
		.Prop(TEXT("bSkipDirty"), FNexusSchema::Bool(TEXT("true 时跳过未保存修改的包（默认 true，建议保持）"), true, true))
		.Prop(TEXT("bForceGC"),   FNexusSchema::Bool(TEXT("卸载后是否触发一次 KEEPFLAGS GC（默认 true）"), true, true))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("unload"), TEXT("memory"), TEXT("gc"), TEXT("package"), TEXT("release") };
	Out.RelatedCapabilities = { TEXT("save_asset"), TEXT("get_asset_blueprint") };
	Out.WhenToUse = TEXT("批量读取后内存仍偏高、需立即释放时手动调用");
}

FCapabilityResult FUnloadAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
#if !WITH_EDITOR
		OutError = TEXT("unload_asset 仅在编辑器模式可用");
		return;
#else
		TArray<FString> Paths;
		bool bSkipDirty = true;
		bool bForceGC = true;
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
			Arguments->TryGetBoolField(TEXT("bSkipDirty"), bSkipDirty);
			Arguments->TryGetBoolField(TEXT("bForceGC"), bForceGC);
		}
		if (Paths.Num() == 0)
		{
			OutError = TEXT("需要 assetPath 或 assetPaths");
			return;
		}

		// 解析每个路径对应的已驻留包（未加载过的路径无需处理，直接视为 alreadyUnloaded）
		TMap<FString, UPackage*> PathToPackage;
		TArray<UPackage*> Candidates;
		for (const FString& Path : Paths)
		{
			FString PackageName = Path;
			int32 DotIdx;
			if (PackageName.FindChar(TEXT('.'), DotIdx))
			{
				PackageName = PackageName.Left(DotIdx);
			}
			UPackage* Pkg = FindPackage(nullptr, *PackageName);
			PathToPackage.Add(Path, Pkg);
			if (Pkg)
			{
				Candidates.AddUnique(Pkg);
			}
		}

		TArray<UPackage*> Skipped;
		const FNexusPackageLedger::FFlushStats Stats =
			FNexusPackageLedger::UnloadPackagesSafely(Candidates, bSkipDirty, bForceGC, &Skipped);

		int32 UnloadedCount = 0, SkippedCount = 0, AlreadyUnloadedCount = 0;
		for (const FString& Path : Paths)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), Path);

			UPackage* Pkg = PathToPackage.FindRef(Path);
			if (!Pkg)
			{
				Entry->SetStringField(TEXT("status"), TEXT("alreadyUnloaded"));
				++AlreadyUnloadedCount;
			}
			else if (Skipped.Contains(Pkg))
			{
				Entry->SetStringField(TEXT("status"), TEXT("skipped"));
				Entry->SetStringField(TEXT("reason"), TEXT("dirty 或编辑器已打开或引擎内建包，未卸载"));
				++SkippedCount;
			}
			else
			{
				Entry->SetStringField(TEXT("status"), TEXT("unloaded"));
				++UnloadedCount;
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}

		OutTop->SetNumberField(TEXT("unloaded"), UnloadedCount);
		OutTop->SetNumberField(TEXT("skipped"), SkippedCount);
		OutTop->SetNumberField(TEXT("alreadyUnloaded"), AlreadyUnloadedCount);
		(void)Stats;
#endif
	});
}

REGISTER_MCP_CAPABILITY(FUnloadAssetCapability)
