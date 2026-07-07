// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/ControlRig/NexusCreateAssetControlRigCapability.h"

#if WITH_CONTROL_RIG

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "ControlRigBlueprint.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "ControlRig.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "NexusMcpTool.h"

void FCreateAssetControlRigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_control_rig");
	Out.Description = TEXT("创建空白 ControlRig Blueprint；用 manage 添加骨骼/控件。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产路径（包路径）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("controlrig"), TEXT("rig"), TEXT("new"), TEXT("create") };
	Out.RelatedCapabilities = { TEXT("get_asset_control_rig"), TEXT("manage_asset_control_rig") };
	Out.WhenToUse = TEXT("创建空白 ControlRig Blueprint；UE5.0+ 专用");
}

FCapabilityResult FCreateAssetControlRigCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		AssetPath = Arguments->GetStringField(TEXT("assetPath"));

		if (LoadObject<UControlRigBlueprint>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("ControlRig Blueprint 已存在: %s"), *AssetPath));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UControlRigBlueprint* CRBp = Cast<UControlRigBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UControlRig::StaticClass(), Package, *AssetName,
			BPTYPE_Normal, UControlRigBlueprint::StaticClass(), UControlRigBlueprintGeneratedClass::StaticClass()
		));

		if (!CRBp)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("ControlRig Blueprint 创建失败"));
			return;
		}

		FNexusAssetUtils::NotifyAndSaveCreated(Package, CRBp, AssetPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),     CRBp->GetName());
		Entry->SetStringField(TEXT("path"),     CRBp->GetPathName());
		Entry->SetStringField(TEXT("assetType"), TEXT("ControlRigBlueprint"));
		Entry->SetBoolField(TEXT("success"),    true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetControlRigCapability)

#endif // WITH_EDITOR
#endif // WITH_CONTROL_RIG
