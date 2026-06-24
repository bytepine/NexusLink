// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/Context/NexusGetEditorContextCapability.h"

#if WITH_EDITOR

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusEditorContextUtils.h"
#include "NexusMcpTool.h"

void FGetEditorContextCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_editor_context");
	Out.Description = TEXT("读编辑器选中与浏览路径。3 section；editor World≠PIE。");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.ExtraSearchKeywords = { TEXT("selection"), TEXT("content browser"), TEXT("picker"), TEXT("focused") };
	Out.RelatedCapabilities = { TEXT("get_editor_info"), TEXT("capture_viewport"), TEXT("search_asset") };
	Out.WhenToUse = TEXT("读编辑器选中；PIE 用 list_runtime_actors");
}

TSharedPtr<FJsonObject> FGetEditorContextCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("limit"), FNexusSchema::Int(TEXT("列表最大条数（selection 段）"), 100, 1, 500))
		.Build();
}

TArray<FString> FGetEditorContextCapability::GetSectionNames() const
{
	return { TEXT("selection_actors"), TEXT("selection_assets"), TEXT("content_browser_path") };
}

TArray<FString> FGetEditorContextCapability::GetDefaultSectionNames() const
{
	return { TEXT("selection_actors"), TEXT("selection_assets"), TEXT("content_browser_path") };
}

void FGetEditorContextCapability::ExecuteSection(const FString&                 SectionName,
                                                 const TSharedPtr<FJsonObject>& Args,
                                                 void*                          /*TargetOpaque*/,
                                                 TSharedPtr<FJsonObject>&       InOutDetail,
                                                 FString&                       OutError) const
{
#if !WITH_EDITOR
	OutError = TEXT("get_editor_context 仅编辑器构建可用");
	return;
#endif

	int32 Limit = 100;
	if (Args.IsValid() && Args->HasField(TEXT("limit")))
	{
		Limit = FMath::Clamp(static_cast<int32>(Args->GetNumberField(TEXT("limit"))), 1, 500);
	}

	if (SectionName == TEXT("selection_actors"))
	{
		TArray<TSharedPtr<FJsonValue>> Actors;
		int32 Count = 0;
		FNexusEditorContextUtils::CollectSelectionActors(Actors, Count, Limit);
		InOutDetail->SetArrayField(TEXT("actors"), Actors);
		InOutDetail->SetNumberField(TEXT("count"), Actors.Num());
		InOutDetail->SetBoolField(TEXT("isEditorWorld"), true);
	}
	else if (SectionName == TEXT("selection_assets"))
	{
		TArray<TSharedPtr<FJsonValue>> Assets;
		int32 Count = 0;
		FNexusEditorContextUtils::CollectSelectionAssets(Assets, Count, Limit);
		InOutDetail->SetArrayField(TEXT("assets"), Assets);
		InOutDetail->SetNumberField(TEXT("count"), Assets.Num());
	}
	else if (SectionName == TEXT("content_browser_path"))
	{
		FString Path;
		if (!FNexusEditorContextUtils::CollectContentBrowserPath(Path, OutError))
		{
			return;
		}
		InOutDetail->SetStringField(TEXT("path"), Path);
	}
	else
	{
		OutError = FString::Printf(TEXT("未处理的 section '%s'"), *SectionName);
	}
}

REGISTER_MCP_CAPABILITY(FGetEditorContextCapability)

#endif // WITH_EDITOR
