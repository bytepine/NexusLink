// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusUnloadAssetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPackageLedger.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

void FUnloadAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("unload_asset");
	Out.Description = TEXT("Manually unload loaded packages. Fallback; auto-unload handles memory normally.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("Single asset path")))
		.Prop(TEXT("bSkipDirty"), FNexusSchema::Bool(TEXT("If true skip dirty packages (default true, recommended)"), true, true))
		.Prop(TEXT("bForceGC"),   FNexusSchema::Bool(TEXT("Trigger KEEPFLAGS GC after unload (default true)"), true, true))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("unload"), TEXT("memory"), TEXT("gc"), TEXT("package"), TEXT("release") };
	Out.RelatedCapabilities = { TEXT("save_asset"), TEXT("get_asset_blueprint") };
	Out.WhenToUse = TEXT("Call manually when memory stays high after batch reads");
}

FCapabilityResult FUnloadAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
#if !WITH_EDITOR
		OutError = TEXT("unload_asset only available in editor mode");
		return;
#else
		bool bSkipDirty = true;
		bool bForceGC = true;
		const FString Path = A.Str(TEXT("assetPath"));
		Arguments->TryGetBoolField(TEXT("bSkipDirty"), bSkipDirty);
		Arguments->TryGetBoolField(TEXT("bForceGC"), bForceGC);

		// 解析路径对应的已驻留包（未加载过的路径无需处理，直接视为 alreadyUnloaded）
		FString PackageName = Path;
		int32 DotIdx;
		if (PackageName.FindChar(TEXT('.'), DotIdx))
		{
			PackageName = PackageName.Left(DotIdx);
		}
		UPackage* Pkg = FindPackage(nullptr, *PackageName);
		TArray<UPackage*> Candidates;
		if (Pkg)
		{
			Candidates.Add(Pkg);
		}

		TArray<UPackage*> Skipped;
		const FNexusPackageLedger::FFlushStats Stats =
			FNexusPackageLedger::UnloadPackagesSafely(Candidates, bSkipDirty, bForceGC, &Skipped);

		int32 UnloadedCount = 0, SkippedCount = 0, AlreadyUnloadedCount = 0;
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Path);

		if (!Pkg)
		{
			Entry->SetStringField(TEXT("status"), TEXT("alreadyUnloaded"));
			++AlreadyUnloadedCount;
		}
		else if (Skipped.Contains(Pkg))
		{
			Entry->SetStringField(TEXT("status"), TEXT("skipped"));
			Entry->SetStringField(TEXT("reason"), TEXT("Dirty, editor-open, or engine package; not unloaded"));
			++SkippedCount;
		}
		else
		{
			Entry->SetStringField(TEXT("status"), TEXT("unloaded"));
			++UnloadedCount;
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));

		OutTop->SetNumberField(TEXT("unloaded"), UnloadedCount);
		OutTop->SetNumberField(TEXT("skipped"), SkippedCount);
		OutTop->SetNumberField(TEXT("alreadyUnloaded"), AlreadyUnloadedCount);
		(void)Stats;
#endif
	});
}

REGISTER_MCP_CAPABILITY(FUnloadAssetCapability)
