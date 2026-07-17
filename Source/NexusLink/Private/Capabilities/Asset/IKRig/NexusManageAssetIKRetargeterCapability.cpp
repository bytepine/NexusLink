// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/IKRig/NexusManageAssetIKRetargeterCapability.h"

#if WITH_IK_RIG

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetSettings.h"
#include "Rig/IKRigDefinition.h"
#include "NexusMcpTool.h"

void FManageAssetIKRetargeterCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_ik_retargeter");
	Out.SearchAssetTypes = {TEXT("IKRetargeter")};
	Out.Description = TEXT("编辑 IKRetargeter：set_source_rig / set_target_rig / set_chain_source。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(TEXT("操作"),
			{ TEXT("set_source_rig"), TEXT("set_target_rig"), TEXT("set_chain_source") }))
		.Prop(TEXT("rigPath"),       FNexusSchema::Str(TEXT("IKRig 资产路径（set_source/target_rig）")))
		.Prop(TEXT("targetChain"),   FNexusSchema::Str(TEXT("目标 Chain 名（set_chain_source）")))
		.Prop(TEXT("sourceChain"),   FNexusSchema::Str(TEXT("源 Chain 名（set_chain_source）")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),  FNexusSchema::Str(TEXT("IKRetargeter 资产路径")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("操作列表"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("ikretargeter"), TEXT("retarget"), TEXT("chain"), TEXT("source"), TEXT("target") };
	Out.RelatedCapabilities = { TEXT("get_asset_ik_retargeter"), TEXT("get_asset_ik_rig") };
	Out.WhenToUse = TEXT("修改 IKRetargeter 绑定；修改后需 save_asset 落盘");
}

FCapabilityResult FManageAssetIKRetargeterCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UIKRetargeter* Retargeter = FNexusAssetUtils::LoadAssetWithFallback<UIKRetargeter>(AssetPath);
		if (!Retargeter)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("IKRetargeter 未找到: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* OpsArr = nullptr;
		if (!Arguments.IsValid() || !Arguments->TryGetArrayField(TEXT("operations"), OpsArr) || !OpsArr)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("缺少 operations 数组"));
			return;
		}

		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : *OpsArr)
		{
			const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpObjPtr;
			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);
			TSharedPtr<FJsonObject> ResEntry = MakeShared<FJsonObject>();
			ResEntry->SetStringField(TEXT("path"), AssetPath);
			ResEntry->SetStringField(TEXT("action"), Action);

			if (Action.Equals(TEXT("set_source_rig"), ESearchCase::IgnoreCase)
				|| Action.Equals(TEXT("set_target_rig"), ESearchCase::IgnoreCase))
			{
				FString RigPath;
				Op->TryGetStringField(TEXT("rigPath"), RigPath);
				if (RigPath.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("需要 rigPath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				UIKRigDefinition* IKRig = FNexusAssetUtils::LoadAssetWithFallback<UIKRigDefinition>(RigPath);
				if (!IKRig)
				{
					ResEntry->SetStringField(TEXT("error"),
						FString::Printf(TEXT("IKRig 未找到: %s"), *RigPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				// 通过 GetIKRigWriteable 修改 (bypasses controller for simplicity)
				// Use reflection to set TSoftObjectPtr fields
				const bool bSource = Action.Equals(TEXT("set_source_rig"), ESearchCase::IgnoreCase);
				const FName FieldName = bSource
					? FName(TEXT("SourceIKRigAsset"))
					: FName(TEXT("TargetIKRigAsset"));
				if (FSoftObjectProperty* Prop = FindFProperty<FSoftObjectProperty>(Retargeter->GetClass(), FieldName))
				{
					Prop->SetPropertyValue_InContainer(Retargeter, FSoftObjectPtr(IKRig));
					bDirty = true;
					ResEntry->SetStringField(TEXT("rigPath"), RigPath);
				}
				else
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("反射找不到字段"));
				}
			}
			else if (Action.Equals(TEXT("set_chain_source"), ESearchCase::IgnoreCase))
			{
				FString TargetChain, SourceChain;
				Op->TryGetStringField(TEXT("targetChain"), TargetChain);
				Op->TryGetStringField(TEXT("sourceChain"), SourceChain);
				if (TargetChain.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("set_chain_source 需要 targetChain"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				const TObjectPtr<URetargetChainSettings> CS = Retargeter->GetChainMapByName(FName(*TargetChain));
				if (!CS)
				{
					ResEntry->SetStringField(TEXT("error"),
						FString::Printf(TEXT("Chain 未找到: %s"), *TargetChain));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				CS->SourceChain = FName(*SourceChain);
				bDirty = true;
				ResEntry->SetStringField(TEXT("targetChain"), TargetChain);
				ResEntry->SetStringField(TEXT("sourceChain"), SourceChain);
			}
			else
			{
				ResEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry));
		}

		if (bDirty)
		{
			Retargeter->MarkPackageDirty();
			OutTop->SetStringField(TEXT("note"), TEXT("已修改，用 save_asset 落盘"));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetIKRetargeterCapability)

#endif // WITH_IK_RIG
