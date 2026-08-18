// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/ControlRig/NexusManageAssetControlRigCapability.h"

#if WITH_CONTROL_RIG

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
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
	Out.Description = TEXT("Edit ControlRig hierarchy and RigVM graph. See operations[].action.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("rename_element"), TEXT("set_control_color"), TEXT("add_null"), TEXT("remove_element"),
			  TEXT("add_rig_link"), TEXT("break_rig_link"), TEXT("add_rig_node"),
			  TEXT("add_control"), TEXT("add_bone"), TEXT("set_pin_default") }))
		.Prop(TEXT("elementName"),  FNexusSchema::Str(TEXT("Target element name")))
		.Prop(TEXT("newName"),      FNexusSchema::Str(TEXT("New name (rename_element)")))
		.Prop(TEXT("r"),            FNexusSchema::Num(TEXT("Color R (set_control_color)")))
		.Prop(TEXT("g"),            FNexusSchema::Num(TEXT("Color G")))
		.Prop(TEXT("b"),            FNexusSchema::Num(TEXT("Color B")))
		.Prop(TEXT("a"),            FNexusSchema::Num(TEXT("Color A")))
		.Prop(TEXT("parentName"),   FNexusSchema::Str(TEXT("Parent element name (add_null; empty=root)")))
		.Prop(TEXT("elementType"),  FNexusSchema::Enum(TEXT("Element type (remove_element)"),
			{ TEXT("bone"), TEXT("control"), TEXT("null") }))
		.Prop(TEXT("sourcePinPath"), FNexusSchema::Str(TEXT("Source pin path (add_rig_link/break_rig_link) e.g. 'NodeName.PinName'")))
		.Prop(TEXT("targetPinPath"), FNexusSchema::Str(TEXT("Target pin path")))
		.Prop(TEXT("structType"),    FNexusSchema::Str(TEXT("UScriptStruct name (add_rig_node) e.g. 'RigUnit_GetTransform'")))
		.Prop(TEXT("nodeName"),      FNexusSchema::Str(TEXT("New node name (add_rig_node, optional)")))
		.Prop(TEXT("pinPath"),       FNexusSchema::Str(TEXT("Pin path (set_pin_default)")))
		.Prop(TEXT("pinDefaultValue"), FNexusSchema::Str(TEXT("Pin default value (set_pin_default)")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),  FNexusSchema::Str(TEXT("ControlRig Blueprint asset path")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("controlrig"), TEXT("rig"), TEXT("rename"), TEXT("null"), TEXT("control"), TEXT("rigvm"), TEXT("link"), TEXT("wire"), TEXT("connect"), TEXT("node") };
	Out.RelatedCapabilities = { TEXT("get_asset_control_rig"), TEXT("create_asset_control_rig") };
	Out.WhenToUse = TEXT("Edit ControlRig hierarchy and RigVM graph; persist with save_asset");
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
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("ControlRig Blueprint not found: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> OpsArr = FNexusJsonUtils::ExtractOperations(Arguments);
		if (OpsArr.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("Missing operations array"));
			return;
		}

		URigHierarchy* Hier = CRBp->GetHierarchy();
		URigHierarchyController* Controller = Hier ? Hier->GetController(true) : nullptr;
		if (!Controller)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("Unable to get RigHierarchyController"));
			return;
		}

		// RigVM 控制器（用于图连线操作，惰性获取）
		URigVMGraph* VmModel = CRBp->GetDefaultModel();
		URigVMController* VmCtrl = VmModel ? CRBp->GetOrCreateController(VmModel) : nullptr;

		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : OpsArr)
		{
			const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpObjPtr;

			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);
			TSharedPtr<FJsonObject> ResEntry = MakeShared<FJsonObject>();
			ResEntry->SetStringField(TEXT("path"), AssetPath);
			ResEntry->SetStringField(TEXT("action"), Action);

			if (Action.Equals(TEXT("rename_element"), ESearchCase::IgnoreCase))
			{
				FString ElemName, NewName;
				Op->TryGetStringField(TEXT("elementName"), ElemName);
				Op->TryGetStringField(TEXT("newName"),     NewName);
				if (ElemName.IsEmpty() || NewName.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("rename_element requires elementName + newName"));
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
					ResEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Element not found: %s"), *ElemName));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				const bool bOk = Controller->RenameElement(Key, FName(*NewName));
				bDirty |= bOk;
				if (!bOk) ResEntry->SetStringField(TEXT("error"), TEXT("rename_element failed"));
				else ResEntry->SetStringField(TEXT("newName"), NewName);
			}
			else if (Action.Equals(TEXT("set_control_color"), ESearchCase::IgnoreCase))
			{
				FString ElemName;
				Op->TryGetStringField(TEXT("elementName"), ElemName);
				if (ElemName.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("set_control_color requires elementName"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				const FRigElementKey Key(FName(*ElemName), ERigElementType::Control);
				FRigControlElement* Ctrl = Hier->Find<FRigControlElement>(Key);
				if (!Ctrl)
				{
					ResEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Control not found: %s"), *ElemName));
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
			}
			else if (Action.Equals(TEXT("add_null"), ESearchCase::IgnoreCase))
			{
				FString ElemName, ParentName;
				Op->TryGetStringField(TEXT("elementName"), ElemName);
				Op->TryGetStringField(TEXT("parentName"),  ParentName);
				if (ElemName.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("add_null requires elementName"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				FRigElementKey ParentKey;
				if (!ParentName.IsEmpty())
					ParentKey = FRigElementKey(FName(*ParentName), ERigElementType::Bone);
				const FRigElementKey NewKey = Controller->AddNull(FName(*ElemName), ParentKey);
				bDirty = NewKey.IsValid();
				if (!NewKey.IsValid()) ResEntry->SetStringField(TEXT("error"), TEXT("add_null failed"));
				else ResEntry->SetStringField(TEXT("elementName"), ElemName);
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
					ResEntry->SetStringField(TEXT("error"), TEXT("remove_element requires elementName + elementType"));
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
					ResEntry->SetStringField(TEXT("error"), TEXT("add_rig_link requires sourcePinPath + targetPinPath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				if (!VmCtrl)
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("Unable to get RigVMController"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				const bool bOk = VmCtrl->AddLink(SrcPin, DstPin, false);
				bDirty |= bOk;
				if (!bOk)
					ResEntry->SetStringField(TEXT("error"), TEXT("AddLink failed; check pin paths or type compatibility"));
			}
			else if (Action.Equals(TEXT("break_rig_link"), ESearchCase::IgnoreCase))
			{
				FString SrcPin, DstPin;
				Op->TryGetStringField(TEXT("sourcePinPath"), SrcPin);
				Op->TryGetStringField(TEXT("targetPinPath"), DstPin);
				if (SrcPin.IsEmpty() || DstPin.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("break_rig_link requires sourcePinPath + targetPinPath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				if (!VmCtrl)
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("Unable to get RigVMController"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				const bool bOk = VmCtrl->BreakLink(SrcPin, DstPin, false);
				bDirty |= bOk;
				if (!bOk)
					ResEntry->SetStringField(TEXT("error"), TEXT("BreakLink failed; check pin paths are connected"));
			}
			else if (Action.Equals(TEXT("add_rig_node"), ESearchCase::IgnoreCase))
			{
				// 在 RigVM 图中添加 Unit 节点；structType 为 UScriptStruct 名，如 'RigUnit_GetTransform'
				FString StructType, NodeName;
				Op->TryGetStringField(TEXT("structType"), StructType);
				Op->TryGetStringField(TEXT("nodeName"),   NodeName);
				if (StructType.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("add_rig_node requires structType"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				if (!VmCtrl)
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("Unable to get RigVMController"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				// 查找 UScriptStruct
				UScriptStruct* Struct = FindFirstObject<UScriptStruct>(*StructType,
					EFindFirstObjectOptions::NativeFirst | EFindFirstObjectOptions::EnsureIfAmbiguous);
				if (!Struct)
				{
					ResEntry->SetStringField(TEXT("error"),
						FString::Printf(TEXT("UScriptStruct '%s' not found"), *StructType));
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
					ResEntry->SetStringField(TEXT("nodePath"), NewNode->GetNodePath());
				}
				else
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("AddUnitNode failed"));
				}
			}
			else if (Action.Equals(TEXT("add_control"), ESearchCase::IgnoreCase)
				|| Action.Equals(TEXT("add_bone"), ESearchCase::IgnoreCase))
			{
				FString ElemName, ParentName;
				Op->TryGetStringField(TEXT("elementName"), ElemName);
				Op->TryGetStringField(TEXT("parentName"), ParentName);
				if (ElemName.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("elementName required"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				FRigElementKey ParentKey;
				if (!ParentName.IsEmpty())
					ParentKey = FRigElementKey(FName(*ParentName), ERigElementType::Bone);
				FRigElementKey NewKey;
				if (Action.Equals(TEXT("add_control"), ESearchCase::IgnoreCase))
				{
					FRigControlSettings Settings;
					NewKey = Controller->AddControl(FName(*ElemName), ParentKey, Settings, FRigControlValue(), false);
				}
				else
				{
					NewKey = Controller->AddBone(FName(*ElemName), ParentKey, FTransform::Identity, true, ERigTransformType::InitialLocal);
				}
				bDirty = NewKey.IsValid();
				if (!NewKey.IsValid()) ResEntry->SetStringField(TEXT("error"), TEXT("Failed to add element"));
				else ResEntry->SetStringField(TEXT("elementName"), ElemName);
			}
			else if (Action.Equals(TEXT("set_pin_default"), ESearchCase::IgnoreCase))
			{
				FString PinPath, PinVal;
				Op->TryGetStringField(TEXT("pinPath"), PinPath);
				Op->TryGetStringField(TEXT("pinDefaultValue"), PinVal);
				if (PinPath.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("set_pin_default requires pinPath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				if (!VmCtrl)
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("Unable to get RigVMController"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				const bool bOk = VmCtrl->SetPinDefaultValue(PinPath, PinVal, true, false, false);
				bDirty |= bOk;
				if (!bOk) ResEntry->SetStringField(TEXT("error"), TEXT("SetPinDefaultValue failed"));
			}
			else
			{
				ResEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: %s"), *Action));
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry));
		}

		if (bDirty)
		{
			CRBp->MarkPackageDirty();
			OutTop->SetStringField(TEXT("note"), TEXT("Modified; persist with save_asset"));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetControlRigCapability)

#endif // WITH_CONTROL_RIG
