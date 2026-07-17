// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/ControlRig/NexusGetAssetControlRigCapability.h"

#if WITH_CONTROL_RIG

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "ControlRigBlueprint.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyDefines.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/RigVMPin.h"
#include "RigVMModel/RigVMLink.h"
#include "NexusMcpTool.h"

void FGetAssetControlRigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_control_rig");
	Out.SearchAssetTypes = {TEXT("ControlRig"), TEXT("ControlRigBlueprint")};
	Out.Description = TEXT("读取 ControlRig Blueprint 层级（骨骼/控件/Null）与 RigVM 图（节点/引脚/连线）。写用 manage_asset_control_rig。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("ControlRig Blueprint 资产路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("controlrig"), TEXT("rig"), TEXT("bone"), TEXT("control"), TEXT("hierarchy"), TEXT("rigvm"), TEXT("node"), TEXT("link"), TEXT("wire") };
	Out.RelatedCapabilities = { TEXT("manage_asset_control_rig"), TEXT("create_asset_control_rig"), TEXT("get_asset_skeleton") };
	Out.WhenToUse = TEXT("读取 ControlRig 层级元素与 RigVM 图节点/连线；写用 manage_asset_control_rig");
}

FCapabilityResult FGetAssetControlRigCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UControlRigBlueprint* CRBp = FNexusAssetUtils::LoadAssetWithFallback<UControlRigBlueprint>(AssetPath);
		if (!CRBp)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}},
				FString::Printf(TEXT("ControlRig Blueprint 未找到: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("assetPath"), AssetPath);
		Entry->SetStringField(TEXT("name"),      CRBp->GetName());
		Entry->SetStringField(TEXT("assetType"), TEXT("ControlRigBlueprint"));

		if (USkeletalMesh* PreviewMesh = CRBp->GetPreviewMesh())
			Entry->SetStringField(TEXT("previewMesh"), PreviewMesh->GetPathName());

		const URigHierarchy* Hier = CRBp->GetHierarchy();
		if (!Hier) { OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

		// 骨骼
		TArray<TSharedPtr<FJsonValue>> BonesArr;
		for (FRigBoneElement* Bone : Hier->GetBones())
		{
			if (!Bone) continue;
			TSharedPtr<FJsonObject> BObj = MakeShared<FJsonObject>();
			BObj->SetStringField(TEXT("name"), Bone->GetName());
			BonesArr.Add(MakeShared<FJsonValueObject>(BObj));
		}
		Entry->SetArrayField(TEXT("bones"), BonesArr);

		// 控件
		TArray<TSharedPtr<FJsonValue>> CtrlsArr;
		for (FRigControlElement* Ctrl : Hier->GetControls())
		{
			if (!Ctrl) continue;
			TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
			CObj->SetStringField(TEXT("name"), Ctrl->GetName());
			CObj->SetNumberField(TEXT("controlType"), static_cast<int32>(Ctrl->Settings.ControlType));
			CtrlsArr.Add(MakeShared<FJsonValueObject>(CObj));
		}
		Entry->SetArrayField(TEXT("controls"), CtrlsArr);

		// Null
		TArray<TSharedPtr<FJsonValue>> NullsArr;
		for (FRigNullElement* Null : Hier->GetNulls())
		{
			if (!Null) continue;
			TSharedPtr<FJsonObject> NObj = MakeShared<FJsonObject>();
			NObj->SetStringField(TEXT("name"), Null->GetName());
			NullsArr.Add(MakeShared<FJsonValueObject>(NObj));
		}
		Entry->SetArrayField(TEXT("nulls"), NullsArr);

		Entry->SetNumberField(TEXT("totalElements"), Hier->Num());

		// RigVM 图：节点（含引脚）与连线概览
		URigVMGraph* VmModel = CRBp->GetDefaultModel();
		if (VmModel)
		{
			TArray<TSharedPtr<FJsonValue>> VmNodesArr;
			for (URigVMNode* Node : VmModel->GetNodes())
			{
				if (!Node) continue;
				TSharedPtr<FJsonObject> NObj = MakeShared<FJsonObject>();
				NObj->SetStringField(TEXT("path"),  Node->GetNodePath());
				NObj->SetStringField(TEXT("title"), Node->GetNodeTitle().ToString());

				TArray<TSharedPtr<FJsonValue>> InPins, OutPins;
				for (URigVMPin* Pin : Node->GetPins())
				{
					if (!Pin) continue;
					const ERigVMPinDirection Dir = Pin->GetDirection();
					if (Dir == ERigVMPinDirection::Output)
						OutPins.Add(MakeShared<FJsonValueString>(Pin->GetPinPath()));
					else
						InPins.Add(MakeShared<FJsonValueString>(Pin->GetPinPath()));
				}
				NObj->SetArrayField(TEXT("inputPins"),  InPins);
				NObj->SetArrayField(TEXT("outputPins"), OutPins);
				VmNodesArr.Add(MakeShared<FJsonValueObject>(NObj));
			}
			Entry->SetArrayField(TEXT("rigVmNodes"), VmNodesArr);

			TArray<TSharedPtr<FJsonValue>> LinksArr;
			for (URigVMLink* Link : VmModel->GetLinks())
			{
				if (!Link) continue;
				URigVMPin* SrcPin = Link->GetSourcePin();
				URigVMPin* DstPin = Link->GetTargetPin();
				if (!SrcPin || !DstPin) continue;
				TSharedPtr<FJsonObject> LObj = MakeShared<FJsonObject>();
				LObj->SetStringField(TEXT("from"), SrcPin->GetPinPath());
				LObj->SetStringField(TEXT("to"),   DstPin->GetPinPath());
				LinksArr.Add(MakeShared<FJsonValueObject>(LObj));
			}
			Entry->SetArrayField(TEXT("rigVmLinks"), LinksArr);
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetControlRigCapability)

#endif // WITH_CONTROL_RIG
