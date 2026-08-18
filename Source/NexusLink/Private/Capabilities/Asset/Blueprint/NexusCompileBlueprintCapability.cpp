// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Blueprint/NexusCompileBlueprintCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
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
	Out.Description = TEXT("Explicitly compile Blueprint/ABP/WBP. Optional saveToDisk; use after manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),   FNexusSchema::Str(TEXT("Blueprint asset path")))
		.Prop(TEXT("saveToDisk"),  FNexusSchema::Bool(TEXT("Save package to disk after compile"), false))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("compile"), TEXT("rebuild"), TEXT("kismet"), TEXT("abp"), TEXT("wbp") };
	Out.RelatedCapabilities = { TEXT("save_asset"), TEXT("manage_asset_blueprint"), TEXT("get_asset_blueprint") };
	Out.WhenToUse = TEXT("Explicit compile when manage omits compile");
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
		const FNexusArgs A(Arguments);
#if !WITH_EDITOR
		OutError = TEXT("compile_blueprint only available in editor mode");
		return;
#else
		const FString Path = A.Str(TEXT("assetPath"));

		bool bSaveToDisk = false;
		Arguments->TryGetBoolField(TEXT("saveToDisk"), bSaveToDisk);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Path);

		UBlueprint* BP = FNexusAssetUtils::LoadAssetTracked<UBlueprint>(Path);
		if (!BP)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *Path));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
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
#endif
	});
}

REGISTER_MCP_CAPABILITY(FCompileBlueprintCapability)
