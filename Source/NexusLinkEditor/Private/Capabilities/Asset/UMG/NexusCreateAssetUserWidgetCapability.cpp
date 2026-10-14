// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/UMG/NexusCreateAssetUserWidgetCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusAssetEditorUtils.h"
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

void FNexusCreateAssetUserWidgetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetUserWidget");
	Out.Description = TEXT("Create WBP. ParentClass sets UI base; fill widget tree via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),   FNexusSchema::Str(TEXT("New WidgetBlueprint package path")))
		.Prop(TEXT("ParentClass"), FNexusSchema::Str(TEXT("Parent class (default UserWidget)")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("wbp"), TEXT("umg"), TEXT("new"), TEXT("UI"), TEXT("panel") };
	Out.RelatedCapabilities = { TEXT("ManageAssetUserWidget"), TEXT("GetAssetUserWidget") };
	Out.WhenToUse = TEXT("Create empty WBP; ParentClass optional (default UserWidget)");
}

FCapabilityResult FNexusCreateAssetUserWidgetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
#if WITH_EDITOR
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FString ParentClassName = A.Str(TEXT("ParentClass"), TEXT("UserWidget"));
		const FNexusAssetEditorUtils::FAssetCreateOutcome Created = FNexusAssetEditorUtils::CreateBlueprintAsset(
			AssetPath, ParentClassName, UUserWidget::StaticClass(), UWidgetBlueprint::StaticClass());
		if (!Created.Ok())
		{
			OutError = Created.Error;
			return;
		}
		UBlueprint* NewBP = Cast<UBlueprint>(Created.Asset);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), NewBP->GetOutermost()->GetName());
		if (NewBP->ParentClass)
		{
			Entry->SetStringField(TEXT("ParentClass"), NewBP->ParentClass->GetName());
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
#else
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		OutError = TEXT("CreateAssetUserWidget only available in editor builds");
	});
#endif
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetUserWidgetCapability)
