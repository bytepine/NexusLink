// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Blueprint/NexusCompileBlueprintCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPackageLedger.h"
#include "Engine/Blueprint.h"
#include "NexusMcpTool.h"

#if WITH_EDITOR
#include "Kismet2/KismetEditorUtilities.h"
#include "Editor.h"
#endif

void FCompileBlueprintCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("compile_blueprint");
	Out.Description = TEXT("显式编译 Blueprint/ABP/WBP。可选 saveToDisk 落盘；manage 后补编译用。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),   FNexusSchema::Str(TEXT("蓝图资产路径")))
		.Prop(TEXT("assetPaths"),  FNexusSchema::StrArr(TEXT("多个蓝图路径（批量）")))
		.Prop(TEXT("saveToDisk"),  FNexusSchema::Bool(TEXT("编译后保存包到磁盘"), false))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("compile"), TEXT("rebuild"), TEXT("kismet"), TEXT("abp"), TEXT("wbp") };
	Out.RelatedCapabilities = { TEXT("save_asset"), TEXT("manage_asset_blueprint"), TEXT("get_asset_blueprint") };
	Out.WhenToUse = TEXT("manage 改图后显式触发编译；持久化另调 save_asset");
}

static void CollectBlueprintPaths(const TSharedPtr<FJsonObject>& Args, TArray<FString>& OutPaths)
{
	OutPaths.Reset();
	if (!Args.IsValid()) return;

	FString Single;
	if (Args->TryGetStringField(TEXT("assetPath"), Single) && !Single.IsEmpty())
	{
		OutPaths.Add(Single);
	}

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Args->TryGetArrayField(TEXT("assetPaths"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FString P;
			if (V.IsValid() && V->TryGetString(P) && !P.IsEmpty())
			{
				OutPaths.AddUnique(P);
			}
		}
	}
}

#if WITH_EDITOR
static FString BlueprintStatusToString(const UBlueprint* BP)
{
	if (!BP) return TEXT("Unknown");
	return FString::Printf(TEXT("%d"), static_cast<int32>(BP->Status));
}
#endif

FCapabilityResult FCompileBlueprintCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
#if !WITH_EDITOR
		OutError = TEXT("compile_blueprint 仅在编辑器模式可用");
		return;
#else
		TArray<FString> Paths;
		CollectBlueprintPaths(Arguments, Paths);
		if (Paths.Num() == 0)
		{
			OutError = TEXT("需要 assetPath 或 assetPaths");
			return;
		}

		bool bSaveToDisk = false;
		if (Arguments.IsValid())
		{
			Arguments->TryGetBoolField(TEXT("saveToDisk"), bSaveToDisk);
		}

		for (const FString& Path : Paths)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), Path);

			UBlueprint* BP = FNexusAssetUtils::LoadAssetTracked<UBlueprint>(Path);
			if (!BP)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint 未找到: %s"), *Path));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			if (bSaveToDisk)
			{
				UPackage* Pkg = BP->GetOutermost();
				const FString PkgPath = FNexusAssetUtils::PackagePathOf(BP);
				const bool bSaved = FNexusAssetUtils::CompileAndSaveBlueprint(Pkg, BP, PkgPath);
				Entry->SetBoolField(TEXT("saved"), bSaved);
			}
			else
			{
				FKismetEditorUtilities::CompileBlueprint(BP);
			}

			const bool bHasCompilerErrors = (BP->Status == BS_Error);
			Entry->SetStringField(TEXT("status"), BlueprintStatusToString(BP));
			Entry->SetBoolField(TEXT("hasCompilerErrors"), bHasCompilerErrors);

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));

			// 未保存的编译结果包会 dirty，Flush 内部会自动跳过；仅 saveToDisk 落盘后的包才可能被本轮卸载
			FNexusPackageLedger::MaybeFlush();
		}
#endif
	});
}

REGISTER_MCP_CAPABILITY(FCompileBlueprintCapability)
