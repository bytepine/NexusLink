// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/NexusReimportAssetCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "EditorReimportHandler.h"
#include "NexusMcpTool.h"

void FReimportAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("reimport_asset");
	Out.Description = TEXT("重新导入资产源文件。刷新已修改的外部资源。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("资产路径")))
		.Prop(TEXT("assetPaths"), FNexusSchema::StrArr(TEXT("多个资产路径（批量）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("reimport"), TEXT("refresh"), TEXT("reload"), TEXT("source") };
	Out.RelatedCapabilities = { TEXT("search_asset"), TEXT("export_asset") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("重新导入资产源文件（如修改了外部 FBX/纹理后刷新）");
}

FCapabilityResult FReimportAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TArray<FString> Paths;
		if (Arguments.IsValid())
		{
			FString Single;
			if (Arguments->TryGetStringField(TEXT("assetPath"), Single) && !Single.IsEmpty())
				Paths.Add(Single);
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (Arguments->TryGetArrayField(TEXT("assetPaths"), Arr) && Arr)
			{
				for (const auto& V : *Arr)
				{
					FString P;
					if (V.IsValid() && V->TryGetString(P) && !P.IsEmpty())
						Paths.AddUnique(P);
				}
			}
		}
		if (Paths.Num() == 0) { OutError = TEXT("需要 assetPath 或 assetPaths"); return; }

		for (const FString& Path : Paths)
		{
			UObject* Asset = FNexusAssetUtils::LoadAssetWithFallback<UObject>(Path);
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), Path);

			if (!Asset)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("资产未找到: %s"), *Path));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
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
				Entry->SetStringField(TEXT("error"), TEXT("重导入失败（资产可能不支持重导入）"));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FReimportAssetCapability)

#endif // WITH_EDITOR
