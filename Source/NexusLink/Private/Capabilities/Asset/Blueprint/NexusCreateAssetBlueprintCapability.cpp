// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Blueprint/NexusCreateAssetBlueprintCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#if WITH_EDITOR
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_Event.h"
#include "EdGraph/EdGraph.h"
#endif
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "NexusMcpTool.h"

void FCreateAssetBlueprintCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_blueprint");
	Out.Description = TEXT("Create new BP and compile; parentClass=Interface for BPI. Add vars/nodes via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),   FNexusSchema::Str(TEXT("New Blueprint package path, e.g. '/Game/Blueprints/BP_NewActor'")))
		.Prop(TEXT("parentClass"), FNexusSchema::Str(TEXT("Parent class or BP path. Interface for BPI; Actor/Pawn/Character for normal BP")))
		.Required({ TEXT("assetPath"), TEXT("parentClass") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("bp"), TEXT("new"), TEXT("subclass"), TEXT("derive"), TEXT("parent"), TEXT("interface"), TEXT("bpi") };
	Out.RelatedCapabilities = { TEXT("manage_asset_blueprint"), TEXT("get_asset_blueprint") };
	Out.WhenToUse = TEXT("Create empty BP or BPI (parentClass=Interface); not for existing BP");
}

FCapabilityResult FCreateAssetBlueprintCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
#if WITH_EDITOR
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		const FString AssetPath = A.Str(TEXT("assetPath"));
		const FString ParentClassName = A.Str(TEXT("parentClass"));
		const FNexusAssetUtils::FAssetCreateOutcome Created = FNexusAssetUtils::CreateBlueprintAsset(
			AssetPath, ParentClassName, UObject::StaticClass(), nullptr, nullptr, /*bCompileAndSave=*/false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UBlueprint* NewBlueprint = Cast<UBlueprint>(Created.Asset);
		UClass* ParentClass = NewBlueprint ? NewBlueprint->ParentClass : nullptr;

		// headless / DefaultEventNodes 未注册时 CreateBlueprint 可能不生成 BeginPlay。
		// 手动补启用态 ReceiveBeginPlay；勿 MarkBlueprintAsStructurallyModified（骨架重编译可能清图）。
		int32 BeginPlayEnsured = 0;
		if (ParentClass && ParentClass->IsChildOf(AActor::StaticClass()) && NewBlueprint->UbergraphPages.Num() > 0)
		{
			UEdGraph* Uber = NewBlueprint->UbergraphPages[0];
			if (!FBlueprintEditorUtils::FindOverrideForFunction(
				NewBlueprint, AActor::StaticClass(), FName(TEXT("ReceiveBeginPlay"))))
			{
				UK2Node_Event* EventNode = NewObject<UK2Node_Event>(Uber);
				EventNode->SetFlags(RF_Transactional);
				EventNode->EventReference.SetExternalMember(FName(TEXT("ReceiveBeginPlay")), AActor::StaticClass());
				EventNode->bOverrideFunction = true;
				Uber->AddNode(EventNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);
				EventNode->CreateNewGuid();
				EventNode->PostPlacedNewNode();
				EventNode->AllocateDefaultPins();
				EventNode->NodePosX = 0;
				EventNode->NodePosY = 0;
				// 若仍是 Ghost（引擎默认生成路径），改为启用以便 connect
				EventNode->SetEnabledState(ENodeEnabledState::Enabled, /*bUserAction=*/true);
				BeginPlayEnsured = 1;
			}
			else
			{
				if (UK2Node_Event* Existing = FBlueprintEditorUtils::FindOverrideForFunction(
					NewBlueprint, AActor::StaticClass(), FName(TEXT("ReceiveBeginPlay"))))
				{
					Existing->SetEnabledState(ENodeEnabledState::Enabled, /*bUserAction=*/true);
					BeginPlayEnsured = 2;
				}
			}
			FBlueprintEditorUtils::MarkBlueprintAsModified(NewBlueprint);
		}

		FNexusAssetUtils::NotifyCompileAndSave(NewBlueprint->GetOutermost(), NewBlueprint, AssetPath);

		OutEntry->SetStringField(TEXT("path"),    AssetPath);
		OutEntry->SetStringField(TEXT("name"),    NewBlueprint->GetName());
		FNexusAssetUtils::AppendBlueprintMetaFields(NewBlueprint, OutEntry);
		OutEntry->SetNumberField(TEXT("beginPlayEnsured"), BeginPlayEnsured);
		if (NewBlueprint->UbergraphPages.Num() > 0)
		{
			OutEntry->SetNumberField(TEXT("eventGraphNodeCount"), NewBlueprint->UbergraphPages[0]->Nodes.Num());
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
#else
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		OutError = TEXT("create_asset_blueprint only available in editor builds");
	});
#endif
}

REGISTER_MCP_CAPABILITY(FCreateAssetBlueprintCapability)
