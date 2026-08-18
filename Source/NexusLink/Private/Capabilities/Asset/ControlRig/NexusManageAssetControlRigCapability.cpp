// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/ControlRig/NexusManageAssetControlRigCapability.h"

#if WITH_CONTROL_RIG

#include "Utils/NexusArgs.h"
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

struct FControlRigActionState
{
	UControlRigBlueprint* CRBp = nullptr;
	URigHierarchy* Hier = nullptr;
	URigHierarchyController* Controller = nullptr;
	URigVMController* VmCtrl = nullptr;
	bool bDirty = false;
	TSharedPtr<FJsonObject> OutTop;
};

static FControlRigActionState* CRState(FNexusActionContext& Ctx)
{
	return static_cast<FControlRigActionState*>(Ctx.Target);
}

static void MarkCRDirty(FNexusActionContext& Ctx)
{
	if (FControlRigActionState* S = CRState(Ctx))
	{
		S->bDirty = true;
	}
}

static FRigElementKey FindElementKey(URigHierarchy* Hier, const FString& ElemName)
{
	FRigElementKey Key;
	if (!Hier) return Key;
	for (ERigElementType T : {ERigElementType::Bone, ERigElementType::Control, ERigElementType::Null})
	{
		FRigElementKey Candidate(FName(*ElemName), T);
		if (Hier->Contains(Candidate)) { return Candidate; }
	}
	return Key;
}

static FRigElementKey ParentBoneKey(const FString& ParentName)
{
	FRigElementKey ParentKey;
	if (!ParentName.IsEmpty())
		ParentKey = FRigElementKey(FName(*ParentName), ERigElementType::Bone);
	return ParentKey;
}

static URigVMController* RequireVmCtrl(FNexusActionContext& Ctx)
{
	FControlRigActionState* S = CRState(Ctx);
	if (!S || !S->VmCtrl)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Unable to get RigVMController"));
		return nullptr;
	}
	return S->VmCtrl;
}

static void HandleCR_RenameElement(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FControlRigActionState* S = CRState(Ctx);
	const FNexusArgs A(Op);
	const FString ElemName = A.Str(TEXT("elementName"));
	const FString NewName = A.Str(TEXT("newName"));
	if (ElemName.IsEmpty() || NewName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("rename_element requires elementName + newName"));
		return;
	}
	const FRigElementKey Key = FindElementKey(S->Hier, ElemName);
	if (!Key.IsValid())
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Element not found: %s"), *ElemName));
		return;
	}
	const bool bOk = S->Controller->RenameElement(Key, FName(*NewName));
	if (!bOk) Ctx.Entry->SetStringField(TEXT("error"), TEXT("rename_element failed"));
	else
	{
		Ctx.Entry->SetStringField(TEXT("newName"), NewName);
		MarkCRDirty(Ctx);
	}
}

static void HandleCR_SetControlColor(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FControlRigActionState* S = CRState(Ctx);
	const FNexusArgs A(Op);
	const FString ElemName = A.Str(TEXT("elementName"));
	if (ElemName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_control_color requires elementName"));
		return;
	}
	const FRigElementKey Key(FName(*ElemName), ERigElementType::Control);
	FRigControlElement* Ctrl = S->Hier->Find<FRigControlElement>(Key);
	if (!Ctrl)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Control not found: %s"), *ElemName));
		return;
	}
	Ctrl->Settings.ShapeColor = FLinearColor(
		static_cast<float>(A.Num(TEXT("r"), 1.0)),
		static_cast<float>(A.Num(TEXT("g"), 1.0)),
		static_cast<float>(A.Num(TEXT("b"), 1.0)),
		static_cast<float>(A.Num(TEXT("a"), 1.0)));
	MarkCRDirty(Ctx);
}

static void HandleCR_AddNull(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FControlRigActionState* S = CRState(Ctx);
	const FNexusArgs A(Op);
	const FString ElemName = A.Str(TEXT("elementName"));
	if (ElemName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_null requires elementName"));
		return;
	}
	const FRigElementKey NewKey = S->Controller->AddNull(FName(*ElemName), ParentBoneKey(A.Str(TEXT("parentName"))));
	if (!NewKey.IsValid()) Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_null failed"));
	else
	{
		Ctx.Entry->SetStringField(TEXT("elementName"), ElemName);
		MarkCRDirty(Ctx);
	}
}

static void HandleCR_RemoveElement(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FControlRigActionState* S = CRState(Ctx);
	const FNexusArgs A(Op);
	const FString ElemName = A.Str(TEXT("elementName"));
	const FString ElemTypeStr = A.Str(TEXT("elementType"));
	ERigElementType ElemType = ERigElementType::None;
	if (ElemTypeStr.Equals(TEXT("bone"), ESearchCase::IgnoreCase))      ElemType = ERigElementType::Bone;
	else if (ElemTypeStr.Equals(TEXT("control"), ESearchCase::IgnoreCase)) ElemType = ERigElementType::Control;
	else if (ElemTypeStr.Equals(TEXT("null"), ESearchCase::IgnoreCase))    ElemType = ERigElementType::Null;
	if (ElemName.IsEmpty() || ElemType == ERigElementType::None)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_element requires elementName + elementType"));
		return;
	}
	const FRigElementKey Key(FName(*ElemName), ElemType);
	const bool bOk = S->Controller->RemoveElement(Key);
	if (bOk) MarkCRDirty(Ctx);
	Ctx.Entry->SetBoolField(TEXT("removed"), bOk);
}

static void HandleCR_AddRigLink(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	const FNexusArgs A(Op);
	const FString SrcPin = A.Str(TEXT("sourcePinPath"));
	const FString DstPin = A.Str(TEXT("targetPinPath"));
	if (SrcPin.IsEmpty() || DstPin.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_rig_link requires sourcePinPath + targetPinPath"));
		return;
	}
	URigVMController* VmCtrl = RequireVmCtrl(Ctx);
	if (!VmCtrl) return;
	const bool bOk = VmCtrl->AddLink(SrcPin, DstPin, false);
	if (!bOk) Ctx.Entry->SetStringField(TEXT("error"), TEXT("AddLink failed; check pin paths or type compatibility"));
	else MarkCRDirty(Ctx);
}

static void HandleCR_BreakRigLink(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	const FNexusArgs A(Op);
	const FString SrcPin = A.Str(TEXT("sourcePinPath"));
	const FString DstPin = A.Str(TEXT("targetPinPath"));
	if (SrcPin.IsEmpty() || DstPin.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("break_rig_link requires sourcePinPath + targetPinPath"));
		return;
	}
	URigVMController* VmCtrl = RequireVmCtrl(Ctx);
	if (!VmCtrl) return;
	const bool bOk = VmCtrl->BreakLink(SrcPin, DstPin, false);
	if (!bOk) Ctx.Entry->SetStringField(TEXT("error"), TEXT("BreakLink failed; check pin paths are connected"));
	else MarkCRDirty(Ctx);
}

static void HandleCR_AddRigNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	const FNexusArgs A(Op);
	const FString StructType = A.Str(TEXT("structType"));
	const FString NodeName = A.Str(TEXT("nodeName"));
	if (StructType.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_rig_node requires structType"));
		return;
	}
	URigVMController* VmCtrl = RequireVmCtrl(Ctx);
	if (!VmCtrl) return;
	UScriptStruct* Struct = FindFirstObject<UScriptStruct>(*StructType,
		EFindFirstObjectOptions::NativeFirst | EFindFirstObjectOptions::EnsureIfAmbiguous);
	if (!Struct)
	{
		Ctx.Entry->SetStringField(TEXT("error"),
			FString::Printf(TEXT("UScriptStruct '%s' not found"), *StructType));
		return;
	}
	URigVMNode* NewNode = VmCtrl->AddUnitNode(
		Struct, TEXT("Execute"),
		FVector2D::ZeroVector,
		NodeName,
		false);
	if (NewNode)
	{
		Ctx.Entry->SetStringField(TEXT("nodePath"), NewNode->GetNodePath());
		MarkCRDirty(Ctx);
	}
	else
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("AddUnitNode failed"));
	}
}

static void HandleCR_AddControl(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FControlRigActionState* S = CRState(Ctx);
	const FNexusArgs A(Op);
	const FString ElemName = A.Str(TEXT("elementName"));
	if (ElemName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("elementName required"));
		return;
	}
	FRigControlSettings Settings;
	const FRigElementKey NewKey = S->Controller->AddControl(
		FName(*ElemName), ParentBoneKey(A.Str(TEXT("parentName"))), Settings, FRigControlValue(), false);
	if (!NewKey.IsValid()) Ctx.Entry->SetStringField(TEXT("error"), TEXT("Failed to add element"));
	else
	{
		Ctx.Entry->SetStringField(TEXT("elementName"), ElemName);
		MarkCRDirty(Ctx);
	}
}

static void HandleCR_AddBone(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FControlRigActionState* S = CRState(Ctx);
	const FNexusArgs A(Op);
	const FString ElemName = A.Str(TEXT("elementName"));
	if (ElemName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("elementName required"));
		return;
	}
	const FRigElementKey NewKey = S->Controller->AddBone(
		FName(*ElemName), ParentBoneKey(A.Str(TEXT("parentName"))),
		FTransform::Identity, true, ERigTransformType::InitialLocal);
	if (!NewKey.IsValid()) Ctx.Entry->SetStringField(TEXT("error"), TEXT("Failed to add element"));
	else
	{
		Ctx.Entry->SetStringField(TEXT("elementName"), ElemName);
		MarkCRDirty(Ctx);
	}
}

static void HandleCR_SetPinDefault(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	const FNexusArgs A(Op);
	const FString PinPath = A.Str(TEXT("pinPath"));
	const FString PinVal = A.Str(TEXT("pinDefaultValue"));
	if (PinPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_pin_default requires pinPath"));
		return;
	}
	URigVMController* VmCtrl = RequireVmCtrl(Ctx);
	if (!VmCtrl) return;
	const bool bOk = VmCtrl->SetPinDefaultValue(PinPath, PinVal, true, false, false);
	if (!bOk) Ctx.Entry->SetStringField(TEXT("error"), TEXT("SetPinDefaultValue failed"));
	else MarkCRDirty(Ctx);
}

bool FManageAssetControlRigCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UControlRigBlueprint* CRBp = FNexusAssetUtils::LoadAssetWithFallback<UControlRigBlueprint>(AssetPath);
	if (!CRBp)
	{
		OutError = FString::Printf(TEXT("ControlRig Blueprint not found: %s"), *AssetPath);
		return false;
	}
	URigHierarchy* Hier = CRBp->GetHierarchy();
	URigHierarchyController* Controller = Hier ? Hier->GetController(true) : nullptr;
	if (!Controller)
	{
		OutError = TEXT("Unable to get RigHierarchyController");
		return false;
	}
	URigVMGraph* VmModel = CRBp->GetDefaultModel();
	URigVMController* VmCtrl = VmModel ? CRBp->GetOrCreateController(VmModel) : nullptr;
	FControlRigActionState* State = new FControlRigActionState();
	State->CRBp = CRBp;
	State->Hier = Hier;
	State->Controller = Controller;
	State->VmCtrl = VmCtrl;
	OutTarget = State;
	return true;
}

void FManageAssetControlRigCapability::AfterPrepareTarget(
	void* Target,
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& OutTop) const
{
	(void)Args;
	if (FControlRigActionState* State = static_cast<FControlRigActionState*>(Target))
	{
		State->OutTop = OutTop;
	}
}

void FManageAssetControlRigCapability::FinalizeTarget(void* Target) const
{
	FControlRigActionState* State = static_cast<FControlRigActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->CRBp)
	{
		State->CRBp->MarkPackageDirty();
		if (State->OutTop.IsValid())
		{
			State->OutTop->SetStringField(TEXT("note"), TEXT("Modified; persist with save_asset"));
		}
	}
	delete State;
}

void FManageAssetControlRigCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("rename_element"),    &HandleCR_RenameElement);
	OutHandlers.Add(TEXT("set_control_color"), &HandleCR_SetControlColor);
	OutHandlers.Add(TEXT("add_null"),          &HandleCR_AddNull);
	OutHandlers.Add(TEXT("remove_element"),    &HandleCR_RemoveElement);
	OutHandlers.Add(TEXT("add_rig_link"),      &HandleCR_AddRigLink);
	OutHandlers.Add(TEXT("break_rig_link"),    &HandleCR_BreakRigLink);
	OutHandlers.Add(TEXT("add_rig_node"),      &HandleCR_AddRigNode);
	OutHandlers.Add(TEXT("add_control"),       &HandleCR_AddControl);
	OutHandlers.Add(TEXT("add_bone"),          &HandleCR_AddBone);
	OutHandlers.Add(TEXT("set_pin_default"),   &HandleCR_SetPinDefault);
}

REGISTER_MCP_CAPABILITY(FManageAssetControlRigCapability)

#endif // WITH_CONTROL_RIG
