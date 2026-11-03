// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Asset/NexusGetAssetSourceControlCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusVersionCompat.h"
#include "Misc/PackageName.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "SourceControlOperations.h"

void FNexusGetAssetSourceControlCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetSourceControl");
	Out.Description = TEXT("Preflight package files. Read-only/checkout/lock/conflict status; optional provider refresh, never submits.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPaths"), FNexusSchema::StrArr(TEXT("1-128 saved asset package/object paths")))
		.Prop(TEXT("bRefresh"), FNexusSchema::Bool(TEXT("Ask the configured source-control provider to update status"), true, false)).Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor};
	Out.bCacheable = false;
	Out.RelatedCapabilities = {TEXT("SaveAsset"), TEXT("GetAssetRefs")};
	Out.ExtraSearchKeywords = {TEXT("checkout"), TEXT("lock"), TEXT("Perforce"), TEXT("Git"), TEXT("read only")};
}

FCapabilityResult FNexusGetAssetSourceControlCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& Entries, auto& Top, auto& Error)
	{
		const FNexusArgs Args(Arguments); const auto Paths = Args.StrArr(TEXT("AssetPaths"));
		if (Paths.Num() < 1 || Paths.Num() > 128) { Error = TEXT("AssetPaths must contain 1-128 saved package paths"); return; }
		auto& Module = ISourceControlModule::Get(); auto& Provider = Module.GetProvider();
		const bool bEnabled = Module.IsEnabled() && Provider.IsAvailable();
		Top->SetStringField(TEXT("Provider"), Provider.GetName().ToString());
		Top->SetBoolField(TEXT("bProviderAvailable"), bEnabled);
		TArray<FString> Files;
		for (const auto& Path : Paths)
		{
			auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("Path"), Path);
			FString File; const FString Package = FPackageName::ObjectPathToPackageName(Path);
#if NX_UE_HAS_PACKAGE_EXISTS_WITHOUT_GUID
			const bool bExists = FPackageName::IsValidLongPackageName(Package) && FPackageName::DoesPackageExist(Package, &File);
#else
			const bool bExists = FPackageName::IsValidLongPackageName(Package) && FPackageName::DoesPackageExist(Package, nullptr, &File);
#endif
			if (!bExists)
				Entry->SetStringField(TEXT("Error"), TEXT("Saved package file not found; save new assets before source-control preflight"));
			else { File = FPaths::ConvertRelativePathToFull(File); Entry->SetStringField(TEXT("FilePath"), File); Files.AddUnique(File); }
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		if (Args.Bool(TEXT("bRefresh")))
		{
			if (!bEnabled) { Error = TEXT("Source-control provider is disabled or unavailable"); return; }
			if (Files.Num() && Provider.Execute(ISourceControlOperation::Create<FUpdateStatus>(), Files) != ECommandResult::Succeeded)
			{ Error = TEXT("Source-control status refresh failed"); return; }
		}
		for (const auto& Value : Entries)
		{
			const auto Entry = Value->AsObject(); FString File;
			if (!Entry->TryGetStringField(TEXT("FilePath"), File)) continue;
			Entry->SetBoolField(TEXT("bReadOnly"), IFileManager::Get().IsReadOnly(*File));
			Entry->SetBoolField(TEXT("bStatusKnown"), false);
			if (!bEnabled) continue;
			const auto State = Provider.GetState(File, EStateCacheUsage::Use);
			if (!State.IsValid() || State->IsUnknown()) continue;
			FString OtherUser;
			Entry->SetBoolField(TEXT("bStatusKnown"), true);
			Entry->SetBoolField(TEXT("bSourceControlled"), State->IsSourceControlled());
			Entry->SetBoolField(TEXT("bCheckedOut"), State->IsCheckedOut());
			Entry->SetBoolField(TEXT("bCheckedOutByOther"), State->IsCheckedOutOther(&OtherUser));
			Entry->SetStringField(TEXT("OtherUser"), OtherUser);
			Entry->SetBoolField(TEXT("bCanCheckout"), State->CanCheckout());
			Entry->SetBoolField(TEXT("bCurrent"), State->IsCurrent());
			Entry->SetBoolField(TEXT("bConflicted"), State->IsConflicted());
			Entry->SetBoolField(TEXT("bAdded"), State->IsAdded());
		}
	});
}
REGISTER_MCP_CAPABILITY(FNexusGetAssetSourceControlCapability)
