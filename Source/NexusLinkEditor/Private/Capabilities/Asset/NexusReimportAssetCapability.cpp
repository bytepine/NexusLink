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

void FReimportAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("reimport_asset");
	Out.Description = TEXT("Reimport asset source file. Refresh modified external resources.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Asset path")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("reimport"), TEXT("refresh"), TEXT("reload"), TEXT("source") };
	Out.RelatedCapabilities = { TEXT("search_asset"), TEXT("export_asset") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("Reimport source (e.g. after editing external FBX/texture)");
}

FCapabilityResult FReimportAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Path = A.Str(TEXT("assetPath"));

		UObject* Asset = FNexusAssetUtils::LoadAssetWithFallback<UObject>(Path);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Path);

		if (!Asset)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset not found: %s"), *Path));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());

		// 通过 FReimportManager 执行重导入（跨版本：UE4 返回 bool，UE5 返回 EReimportResult）
		bool bSuccess = false;
		if (FReimportManager* ReimportMgr = FReimportManager::Instance())
		{
			bSuccess = !!ReimportMgr->Reimport(Asset);
		}

		if (!bSuccess)
		{
			Entry->SetStringField(TEXT("error"), TEXT("Reimport failed (asset may not support reimport)"));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FReimportAssetCapability)

#endif // WITH_EDITOR
