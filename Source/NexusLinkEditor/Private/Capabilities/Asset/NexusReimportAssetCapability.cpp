// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusReimportAssetCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "EditorReimportHandler.h"
#include "NexusMcpTool.h"

void FNexusReimportAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ReimportAsset");
	Out.Description = TEXT("Reimport asset source file. Refresh modified external resources.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("reimport"), TEXT("refresh"), TEXT("reload"), TEXT("Source") };
	Out.RelatedCapabilities = { TEXT("SearchAsset"), TEXT("ExportAsset") };
	Out.Prerequisites = { TEXT("EditorOnly") };
	Out.WhenToUse = TEXT("Reimport source (e.g. after editing external FBX/texture)");
}

FCapabilityResult FNexusReimportAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Path = A.Str(TEXT("AssetPath"));

		UObject* Asset = FNexusAssetUtils::LoadAssetWithFallback<UObject>(Path);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), Path);

		if (!Asset)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Asset not found: %s"), *Path));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("AssetClass"), Asset->GetClass()->GetName());

		// 通过 FReimportManager 执行重导入（跨版本：UE4 返回 bool，UE5 返回 EReimportResult）
		bool bSuccess = false;
		if (FReimportManager* ReimportMgr = FReimportManager::Instance())
		{
			bSuccess = !!ReimportMgr->Reimport(Asset);
		}

		if (!bSuccess)
		{
			Entry->SetStringField(TEXT("Error"), TEXT("Reimport failed (asset may not support reimport)"));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusReimportAssetCapability)

#endif // WITH_EDITOR
