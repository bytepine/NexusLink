// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/UMG/NexusCreateAssetUserWidgetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#if WITH_EDITOR
#include "Kismet2/KismetEditorUtilities.h"
#endif
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#if WITH_EDITOR
#include "WidgetBlueprint.h"
#endif
#include "Blueprint/UserWidget.h"
#include "NexusMcpTool.h"

void FCreateAssetUserWidgetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_user_widget");
	Out.Description = TEXT("Create WBP. parentClass sets UI base; fill widget tree via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),   FNexusSchema::Str(TEXT("New WidgetBlueprint package path")))
		.Prop(TEXT("parentClass"), FNexusSchema::Str(TEXT("Parent class (default UserWidget)")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("wbp"), TEXT("umg"), TEXT("new"), TEXT("ui"), TEXT("panel") };
	Out.RelatedCapabilities = { TEXT("manage_asset_user_widget"), TEXT("get_asset_user_widget") };
	Out.WhenToUse = TEXT("Create empty WBP; parentClass optional (default UserWidget)");
}

FCapabilityResult FCreateAssetUserWidgetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
#if WITH_EDITOR
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));
		const FString ParentClassName = A.Str(TEXT("parentClass"), TEXT("UserWidget"));
		const FNexusAssetUtils::FAssetCreateOutcome Created = FNexusAssetUtils::CreateBlueprintAsset(
			AssetPath, ParentClassName, UUserWidget::StaticClass(), UWidgetBlueprint::StaticClass());
		if (!Created.Ok())
		{
			OutError = Created.Error;
			return;
		}
		UBlueprint* NewBP = Cast<UBlueprint>(Created.Asset);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), NewBP->GetOutermost()->GetName());
		if (NewBP->ParentClass)
		{
			Entry->SetStringField(TEXT("parentClass"), NewBP->ParentClass->GetName());
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
#else
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		OutError = TEXT("create_asset_user_widget only available in editor builds");
	});
#endif
}

REGISTER_MCP_CAPABILITY(FCreateAssetUserWidgetCapability)
