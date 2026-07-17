// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/ControlRig/NexusManageAssetControlRigCapability.h"

#if WITH_CONTROL_RIG

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "ControlRigBlueprint.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyController.h"
#include "Rigs/RigHierarchyDefines.h"
#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMGraph.h"
#include "NexusMcpTool.h"

void FManageAssetControlRigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_control_rig");
	Out.SearchAssetTypes = {TEXT("ControlRig"), TEXT("ControlRigBlueprint")};
	Out.Description = TEXT("编辑 ControlRig：层级（rename_element/set_control_color/add_null/remove_element）与 RigVM 图连线（add_rig_link/break_rig_link/add_rig_node）。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(TEXT("操作"),
			{ TEXT("rename_element"), TEXT("set_control_color"), TEXT("add_null"), TEXT("remove_element"),
			  TEXT("add_rig_link"), TEXT("break_rig_link"), TEXT("add_rig_node") }))
		.Prop(TEXT("elementName"),  FNexusSchema::Str(TEXT("目标元素名")))
		.Prop(TEXT("newName"),      FNexusSchema::Str(TEXT("新名称（rename_element）")))
		.Prop(TEXT("r"),            FNexusSchema::Num(TEXT("颜色 R（set_control_color）")))
		.Prop(TEXT("g"),            FNexusSchema::Num(TEXT("颜色 G")))
		.Prop(TEXT("b"),            FNexusSchema::Num(TEXT("颜色 B")))
		.Prop(TEXT("a"),            FNexusSchema::Num(TEXT("颜色 A")))
		.Prop(TEXT("parentName"),   FNexusSchema::Str(TEXT("父元素名（add_null，空=根）")))
		.Prop(TEXT("elementType"),  FNexusSchema::Enum(TEXT("元素类型（remove_element）"),
			{ TEXT("bone"), TEXT("control"), TEXT("null") }))
		.Prop(TEXT("sourcePinPath"), FNexusSchema::Str(TEXT("源引脚路径（add_rig_link/break_rig_link）如 'NodeName.PinName'")))
		.Prop(TEXT("targetPinPath"), FNexusSchema::Str(TEXT("目标引脚路径")))
		.Prop(TEXT("structType"),    FNexusSchema::Str(TEXT("UScriptStruct 名（add_rig_node）如 'RigUnit_GetTransform'")))
		.Prop(TEXT("nodeName"),      FNexusSchema::Str(TEXT("新节点名（add_rig_node，可选）")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),  FNexusSchema::Str(TEXT("ControlRig Blueprint 资产路径")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("操作列表"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("controlrig"), TEXT("rig"), TEXT("rename"), TEXT("null"), TEXT("control"), TEXT("rigvm"), TEXT("link"), TEXT("wire"), TEXT("connect"), TEXT("node") };
	Out.RelatedCapabilities = { TEXT("get_asset_control_rig"), TEXT("create_asset_control_rig") };
	Out.WhenToUse = TEXT("修改 ControlRig：层级元素增删/改色；RigVM 图节点增删与引脚连线（add_rig_link/break_rig_link）；需 save_asset 落盘");
}

FCapabilityResult FManageAssetControlRigCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
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

		const TArray<TSharedPtr<FJsonValue>>* OpsArr = nullptr;
		if (!Arguments.IsValid() || !Arguments->TryGetArrayField(TEXT("operations"), OpsArr) || !OpsArr)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}}, TEXT("缺少 operations 数组"));
			return;
		}

		URigHierarchy* Hier = CRBp->GetHierarchy();
		URigHierarchyController* Controller = Hier ? Hier->GetController(true) : nullptr;
		if (!Controller)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}}, TEXT("无法获取 RigHierarchyController"));
			return;
		}

		// RigVM 控制器（用于图连线操作，惰性获取）
		URigVMGraph* VmModel = CRBp->GetDefaultModel();
		URigVMController* VmCtrl = VmModel ? CRBp->GetOrCreateController(VmModel) : nullptr;

		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : *OpsArr)
		{
			const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpObjPtr;

			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);
			TSharedPtr<FJsonObject> ResEntry = MakeShared<FJsonObject>();
			ResEntry->SetStringField(TEXT("assetPath"), AssetPath);
			ResEntry->SetStringField(TEXT("action"), Action);

			if (Action.Equals(TEXT("rename_element"), ESearchCase::IgnoreCase))
			{
				FString ElemName, NewName;
				Op->TryGetStringField(TEXT("elementName"), ElemName);
				Op->TryGetStringField(TEXT("newName"),     NewName);
				if (ElemName.IsEmpty() || NewName.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("rename_element 需要 elementName + newName"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				// 尝试各类型
				FRigElementKey Key;
				for (ERigElementType T : {ERigElementType::Bone, ERigElementType::Control, ERigElementType::Null})
				{
					FRigElementKey Candidate(FName(*ElemName), T);
					if (Hier->Contains(Candidate)) { Key = Candidate; break; }
				}
				if (!Key.IsValid())
				{
					ResEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("元素未找到: %s"), *ElemName));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				const bool bOk = Controller->RenameElement(Key, FName(*NewName));
				bDirty |= bOk;
				ResEntry->SetBoolField(TEXT("success"), bOk);
				ResEntry->SetStringField(TEXT("newName"), NewName);
			}
			else if (Action.Equals(TEXT("set_control_color"), ESearchCase::IgnoreCase))
			{
				FString ElemName;
				Op->TryGetStringField(TEXT("elementName"), ElemName);
				if (ElemName.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("set_control_color 需要 elementName"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				const FRigElementKey Key(FName(*ElemName), ERigElementType::Control);
				FRigControlElement* Ctrl = Hier->Find<FRigControlElement>(Key);
				if (!Ctrl)
				{
					ResEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Control 未找到: %s"), *ElemName));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				double R = 1.0, G = 1.0, B = 1.0, A = 1.0;
				Op->TryGetNumberField(TEXT("r"), R);
				Op->TryGetNumberField(TEXT("g"), G);
				Op->TryGetNumberField(TEXT("b"), B);
				Op->TryGetNumberField(TEXT("a"), A);
				Ctrl->Settings.ShapeColor = FLinearColor(
					static_cast<float>(R), static_cast<float>(G),
					static_cast<float>(B), static_cast<float>(A));
				bDirty = true;
				ResEntry->SetBoolField(TEXT("success"), true);
			}
			else if (Action.Equals(TEXT("add_null"), ESearchCase::IgnoreCase))
			{
				FString ElemName, ParentName;
				Op->TryGetStringField(TEXT("elementName"), ElemName);
				Op->TryGetStringField(TEXT("parentName"),  ParentName);
				if (ElemName.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("add_null 需要 elementName"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				FRigElementKey ParentKey;
				if (!ParentName.IsEmpty())
					ParentKey = FRigElementKey(FName(*ParentName), ERigElementType::Bone);
				const FRigElementKey NewKey = Controller->AddNull(FName(*ElemName), ParentKey);
				bDirty = NewKey.IsValid();
				ResEntry->SetBoolField(TEXT("success"), NewKey.IsValid());
				ResEntry->SetStringField(TEXT("elementName"), ElemName);
			}
			else if (Action.Equals(TEXT("remove_element"), ESearchCase::IgnoreCase))
			{
				FString ElemName, ElemTypeStr;
				Op->TryGetStringField(TEXT("elementName"),  ElemName);
				Op->TryGetStringField(TEXT("elementType"),  ElemTypeStr);
				ERigElementType ElemType = ERigElementType::None;
				if (ElemTypeStr.Equals(TEXT("bone"), ESearchCase::IgnoreCase))      ElemType = ERigElementType::Bone;
				else if (ElemTypeStr.Equals(TEXT("control"), ESearchCase::IgnoreCase)) ElemType = ERigElementType::Control;
				else if (ElemTypeStr.Equals(TEXT("null"), ESearchCase::IgnoreCase))    ElemType = ERigElementType::Null;
				if (ElemName.IsEmpty() || ElemType == ERigElementType::None)
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("remove_element 需要 elementName + elementType"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				const FRigElementKey Key(FName(*ElemName), ElemType);
				const bool bOk = Controller->RemoveElement(Key);
				bDirty |= bOk;
				ResEntry->SetBoolField(TEXT("removed"), bOk);
			}
			else if (Action.Equals(TEXT("add_rig_link"), ESearchCase::IgnoreCase))
			{
				// 连接两个节点引脚，引脚路径格式：'NodePath.PinName'（从 get_asset_control_rig rigVmNodes 获取）
				FString SrcPin, DstPin;
				Op->TryGetStringField(TEXT("sourcePinPath"), SrcPin);
				Op->TryGetStringField(TEXT("targetPinPath"), DstPin);
				if (SrcPin.IsEmpty() || DstPin.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("add_rig_link 需要 sourcePinPath + targetPinPath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				if (!VmCtrl)
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("无法获取 RigVMController"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				const bool bOk = VmCtrl->AddLink(SrcPin, DstPin, false);
				bDirty |= bOk;
				ResEntry->SetBoolField(TEXT("success"), bOk);
				if (!bOk)
					ResEntry->SetStringField(TEXT("error"), TEXT("AddLink 失败，检查引脚路径或类型兼容性"));
			}
			else if (Action.Equals(TEXT("break_rig_link"), ESearchCase::IgnoreCase))
			{
				FString SrcPin, DstPin;
				Op->TryGetStringField(TEXT("sourcePinPath"), SrcPin);
				Op->TryGetStringField(TEXT("targetPinPath"), DstPin);
				if (SrcPin.IsEmpty() || DstPin.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("break_rig_link 需要 sourcePinPath + targetPinPath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				if (!VmCtrl)
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("无法获取 RigVMController"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				const bool bOk = VmCtrl->BreakLink(SrcPin, DstPin, false);
				bDirty |= bOk;
				ResEntry->SetBoolField(TEXT("success"), bOk);
				if (!bOk)
					ResEntry->SetStringField(TEXT("error"), TEXT("BreakLink 失败，检查引脚路径是否已连接"));
			}
			else if (Action.Equals(TEXT("add_rig_node"), ESearchCase::IgnoreCase))
			{
				// 在 RigVM 图中添加 Unit 节点；structType 为 UScriptStruct 名，如 'RigUnit_GetTransform'
				FString StructType, NodeName;
				Op->TryGetStringField(TEXT("structType"), StructType);
				Op->TryGetStringField(TEXT("nodeName"),   NodeName);
				if (StructType.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("add_rig_node 需要 structType"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				if (!VmCtrl)
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("无法获取 RigVMController"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				// 查找 UScriptStruct
				UScriptStruct* Struct = FindFirstObject<UScriptStruct>(*StructType,
					EFindFirstObjectOptions::NativeFirst | EFindFirstObjectOptions::EnsureIfAmbiguous);
				if (!Struct)
				{
					ResEntry->SetStringField(TEXT("error"),
						FString::Printf(TEXT("UScriptStruct '%s' 未找到"), *StructType));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				URigVMNode* NewNode = VmCtrl->AddUnitNode(
					Struct, TEXT("Execute"),
					FVector2D::ZeroVector,
					NodeName,
					false);
				if (NewNode)
				{
					bDirty = true;
					ResEntry->SetBoolField(TEXT("success"),   true);
					ResEntry->SetStringField(TEXT("nodePath"), NewNode->GetNodePath());
				}
				else
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("AddUnitNode 失败"));
				}
			}
			else
			{
				ResEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry));
		}

		if (bDirty)
		{
			CRBp->MarkPackageDirty();
			OutTop->SetStringField(TEXT("note"), TEXT("已修改，用 save_asset 落盘"));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetControlRigCapability)

#endif // WITH_CONTROL_RIG
