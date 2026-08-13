// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Blueprint/NexusCreateAssetBlueprintCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
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
	Out.Description = TEXT("创建新 BP 并编译；parentClass=Interface 建 BPI。用 manage 加变量/节点。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),   FNexusSchema::Str(TEXT("新蓝图包路径，如 '/Game/Blueprints/BP_NewActor'")))
		.Prop(TEXT("parentClass"), FNexusSchema::Str(TEXT("父类名或 BP 路径。Interface 建蓝图接口；Actor/Pawn/Character 建普通 BP")))
		.Required({ TEXT("assetPath"), TEXT("parentClass") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("bp"), TEXT("new"), TEXT("subclass"), TEXT("derive"), TEXT("parent"), TEXT("interface"), TEXT("bpi") };
	Out.RelatedCapabilities = { TEXT("manage_asset_blueprint"), TEXT("get_asset_blueprint") };
	Out.WhenToUse = TEXT("创建空白 BP 或 BPI（parentClass=Interface）；不用于编辑现有 BP");
}

FCapabilityResult FCreateAssetBlueprintCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
#if WITH_EDITOR
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")) || !Arguments->HasField(TEXT("parentClass")))
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("缺少必填参数: assetPath, parentClass")); return; }

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		const FString ParentClassName = Arguments->GetStringField(TEXT("parentClass"));

		// AssetRegistry 覆盖已存在但未加载的包，LoadObject 只能检测已在内存中的对象
		if (FPackageName::DoesPackageExist(AssetPath))
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("Blueprint already exists: %s"), *AssetPath)); return; }

		UClass* ParentClass = FNexusAssetUtils::FindClassWithUPrefix(ParentClassName);
		if (!ParentClass) ParentClass = FNexusAssetUtils::FindClassWithUPrefix(TEXT("A") + ParentClassName);
		if (!ParentClass && ParentClassName.Contains(TEXT("/")))
		{
			if (UBlueprint* ParentBP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(ParentClassName))
			{
				ParentClass = ParentBP->GeneratedClass;
				if (!ParentClass)
				{
					FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(
						TEXT("父类蓝图尚未编译（无 GeneratedClass）: %s"), *ParentClassName));
					return;
				}
			}
		}
		if (!ParentClass)
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("父类未找到: %s"), *ParentClassName)); return; }
		if (!ParentClass->IsChildOf(UObject::StaticClass()))
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("无效的父类（非 UObject 子类）: %s"), *ParentClass->GetName())); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		// 校验包名合法性，提前给出具体原因（非法字符 / 路径挂载点不存在等）
		FText PackageNameError;
		if (!FPackageName::IsValidLongPackageName(AssetPath, false, &PackageNameError))
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("无效的包路径 '%s': %s"), *AssetPath, *PackageNameError.ToString())); return; }
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package)
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("创建包失败: %s"), *AssetPath)); return; }

		const bool bIsInterface = ParentClass->HasAnyClassFlags(CLASS_Interface);
		UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(
			ParentClass, Package, *AssetName,
			bIsInterface ? BPTYPE_Interface : BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()
		);
		if (!NewBlueprint)
		{ FNexusCapabilityResultBuilder::AddEntryError(OutEntries, FString::Printf(TEXT("Blueprint 创建失败: %s"), *AssetPath)); return; }

		// headless / DefaultEventNodes 未注册时 CreateBlueprint 可能不生成 BeginPlay。
		// 手动补启用态 ReceiveBeginPlay；勿 MarkBlueprintAsStructurallyModified（骨架重编译可能清图）。
		int32 BeginPlayEnsured = 0;
		if (ParentClass->IsChildOf(AActor::StaticClass()) && NewBlueprint->UbergraphPages.Num() > 0)
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

		FNexusAssetUtils::NotifyCompileAndSave(Package, NewBlueprint, AssetPath);

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
		OutError = TEXT("create_asset_blueprint 仅在编辑器构建可用");
	});
#endif
}

REGISTER_MCP_CAPABILITY(FCreateAssetBlueprintCapability)
