// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/ControlRig/NexusManageAssetControlRigCapability.h"

#if WITH_CONTROL_RIG

#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "Utils/NexusEditorToolsets.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#if NX_UE_HAS_CONTROL_RIG_BLUEPRINT_LEGACY_HEADER
#include "ControlRigBlueprintLegacy.h"
#else
#include "ControlRigBlueprint.h"
#endif
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyController.h"
#include "Rigs/RigHierarchyDefines.h"
#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMPin.h"
#include "NexusMcpTool.h"

void FNexusManageAssetControlRigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetControlRig");
	Out.SearchAssetTypes = {TEXT("ControlRig"), TEXT("ControlRigBlueprint")};
	Out.Description = TEXT("Edit ControlRig hierarchy and RigVM graph. See operations[].action.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("RenameElement"), TEXT("SetControlColor"), TEXT("AddNull"), TEXT("RemoveElement"),
			  TEXT("AddRigLink"), TEXT("BreakRigLink"), TEXT("AddRigNode"),
			  TEXT("AddControl"), TEXT("AddBone"), TEXT("SetPinDefault") }))
		.Prop(TEXT("ElementName"),  FNexusSchema::Str(TEXT("Target element name")))
		.Prop(TEXT("NewName"),      FNexusSchema::Str(TEXT("New name (RenameElement)")))
		.Prop(TEXT("r"),            FNexusSchema::Num(TEXT("Color R (SetControlColor)")))
		.Prop(TEXT("g"),            FNexusSchema::Num(TEXT("Color G")))
		.Prop(TEXT("b"),            FNexusSchema::Num(TEXT("Color B")))
		.Prop(TEXT("a"),            FNexusSchema::Num(TEXT("Color A")))
		.Prop(TEXT("ParentName"),   FNexusSchema::Str(TEXT("Parent element name (AddNull; empty=root)")))
		.Prop(TEXT("ElementType"),  FNexusSchema::Enum(TEXT("Element type (RemoveElement)"),
			{ TEXT("Bone"), TEXT("Control"), TEXT("null") }))
		.Prop(TEXT("SourcePinPath"), FNexusSchema::Str(TEXT("Source pin path (AddRigLink/BreakRigLink) e.g. 'NodeName.PinName'")))
		.Prop(TEXT("TargetPinPath"), FNexusSchema::Str(TEXT("Target pin path")))
		.Prop(TEXT("StructType"),    FNexusSchema::Str(TEXT("UScriptStruct name (AddRigNode) e.g. 'RigUnit_GetTransform'")))
		.Prop(TEXT("NodeName"),      FNexusSchema::Str(TEXT("New node name (AddRigNode, optional)")))
		.Prop(TEXT("PinPath"),       FNexusSchema::Str(TEXT("Pin path (SetPinDefault)")))
		.Prop(TEXT("PinDefaultValue"), FNexusSchema::Str(TEXT("Pin default value (SetPinDefault)")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("ControlRig Blueprint asset path")))
		.Required(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("controlrig"), TEXT("rig"), TEXT("Rename"), TEXT("null"), TEXT("Control"), TEXT("rigvm"), TEXT("link"), TEXT("wire"), TEXT("Connect"), TEXT("Node") };
	Out.RelatedCapabilities = { TEXT("GetAssetControlRig"), TEXT("CreateAssetControlRig") };
	Out.WhenToUse = TEXT("Edit ControlRig hierarchy and RigVM graph; persist with SaveAsset");
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
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Unable to get RigVMController"));
		return nullptr;
	}
	return S->VmCtrl;
}

static void HandleCR_RenameElement(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FControlRigActionState* S = CRState(Ctx);
	const FNexusArgs A(Op);
	const FString ElemName = A.Str(TEXT("ElementName"));
	const FString NewName = A.Str(TEXT("NewName"));
	if (ElemName.IsEmpty() || NewName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RenameElement requires ElementName + NewName"));
		return;
	}
	const FRigElementKey Key = FindElementKey(S->Hier, ElemName);
	if (!Key.IsValid())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Element not found: %s"), *ElemName));
		return;
	}
	const FRigElementKey Renamed = S->Controller->RenameElement(Key, FName(*NewName));
	if (!Renamed.IsValid()) Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RenameElement failed"));
	else
	{
		Ctx.Entry->SetStringField(TEXT("NewName"), NewName);
		MarkCRDirty(Ctx);
	}
}

static void HandleCR_SetControlColor(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FControlRigActionState* S = CRState(Ctx);
	const FNexusArgs A(Op);
	const FString ElemName = A.Str(TEXT("ElementName"));
	if (ElemName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetControlColor requires ElementName"));
		return;
	}
	const FRigElementKey Key(FName(*ElemName), ERigElementType::Control);
	FRigControlElement* Ctrl = S->Hier->Find<FRigControlElement>(Key);
	if (!Ctrl)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Control not found: %s"), *ElemName));
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
	const FString ElemName = A.Str(TEXT("ElementName"));
	if (ElemName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddNull requires ElementName"));
		return;
	}
	const FRigElementKey NewKey = S->Controller->AddNull(
		FName(*ElemName), ParentBoneKey(A.Str(TEXT("ParentName"))), FTransform::Identity);
	if (!NewKey.IsValid()) Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddNull failed"));
	else
	{
		Ctx.Entry->SetStringField(TEXT("ElementName"), ElemName);
		MarkCRDirty(Ctx);
	}
}

static void HandleCR_RemoveElement(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FControlRigActionState* S = CRState(Ctx);
	const FNexusArgs A(Op);
	const FString ElemName = A.Str(TEXT("ElementName"));
	const FString ElemTypeStr = A.Str(TEXT("ElementType"));
	ERigElementType ElemType = ERigElementType::None;
	if (ElemTypeStr.Equals(TEXT("Bone"), ESearchCase::IgnoreCase))      ElemType = ERigElementType::Bone;
	else if (ElemTypeStr.Equals(TEXT("Control"), ESearchCase::IgnoreCase)) ElemType = ERigElementType::Control;
	else if (ElemTypeStr.Equals(TEXT("null"), ESearchCase::IgnoreCase))    ElemType = ERigElementType::Null;
	if (ElemName.IsEmpty() || ElemType == ERigElementType::None)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveElement requires ElementName + ElementType"));
		return;
	}
	const FRigElementKey Key(FName(*ElemName), ElemType);
	const bool bOk = S->Controller->RemoveElement(Key);
	if (bOk) MarkCRDirty(Ctx);
	Ctx.Entry->SetBoolField(TEXT("bRemoved"), bOk);
}

static void HandleCR_AddRigLink(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	const FNexusArgs A(Op);
	const FString SrcPin = A.Str(TEXT("SourcePinPath"));
	const FString DstPin = A.Str(TEXT("TargetPinPath"));
	if (SrcPin.IsEmpty() || DstPin.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddRigLink requires SourcePinPath + TargetPinPath"));
		return;
	}
	URigVMController* VmCtrl = RequireVmCtrl(Ctx);
	if (!VmCtrl) return;
	const bool bOk = VmCtrl->AddLink(SrcPin, DstPin, false);
	if (!bOk) Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddLink failed; check pin paths or type compatibility"));
	else MarkCRDirty(Ctx);
}

static void HandleCR_BreakRigLink(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	const FNexusArgs A(Op);
	const FString SrcPin = A.Str(TEXT("SourcePinPath"));
	const FString DstPin = A.Str(TEXT("TargetPinPath"));
	if (SrcPin.IsEmpty() || DstPin.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("BreakRigLink requires SourcePinPath + TargetPinPath"));
		return;
	}
	URigVMController* VmCtrl = RequireVmCtrl(Ctx);
	if (!VmCtrl) return;
	const bool bOk = VmCtrl->BreakLink(SrcPin, DstPin, false);
	if (!bOk) Ctx.Entry->SetStringField(TEXT("Error"), TEXT("BreakLink failed; check pin paths are connected"));
	else MarkCRDirty(Ctx);
}

static void HandleCR_AddRigNode(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	const FNexusArgs A(Op);
	const FString StructType = A.Str(TEXT("StructType"));
	const FString NodeName = A.Str(TEXT("NodeName"));
	if (StructType.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddRigNode requires StructType"));
		return;
	}
	URigVMController* VmCtrl = RequireVmCtrl(Ctx);
	if (!VmCtrl) return;
	UScriptStruct* Struct = FindFirstObject<UScriptStruct>(*StructType,
		EFindFirstObjectOptions::NativeFirst | EFindFirstObjectOptions::EnsureIfAmbiguous);
	if (!Struct)
	{
		Ctx.Entry->SetStringField(TEXT("Error"),
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
		Ctx.Entry->SetStringField(TEXT("NodePath"), NewNode->GetNodePath());
		MarkCRDirty(Ctx);
	}
	else
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddUnitNode failed"));
	}
}

static void HandleCR_AddControl(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FControlRigActionState* S = CRState(Ctx);
	const FNexusArgs A(Op);
	const FString ElemName = A.Str(TEXT("ElementName"));
	if (ElemName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ElementName required"));
		return;
	}
	FRigControlSettings Settings;
	const FRigElementKey NewKey = S->Controller->AddControl(
		FName(*ElemName), ParentBoneKey(A.Str(TEXT("ParentName"))), Settings, FRigControlValue(),
		FTransform::Identity, FTransform::Identity, false);
	if (!NewKey.IsValid()) Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Failed to add element"));
	else
	{
		Ctx.Entry->SetStringField(TEXT("ElementName"), ElemName);
		MarkCRDirty(Ctx);
	}
}

static void HandleCR_AddBone(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FControlRigActionState* S = CRState(Ctx);
	const FNexusArgs A(Op);
	const FString ElemName = A.Str(TEXT("ElementName"));
	if (ElemName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ElementName required"));
		return;
	}
	const FRigElementKey NewKey = S->Controller->AddBone(
		FName(*ElemName), ParentBoneKey(A.Str(TEXT("ParentName"))),
		FTransform::Identity, true, ERigBoneType::User);
	if (!NewKey.IsValid()) Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Failed to add element"));
	else
	{
		Ctx.Entry->SetStringField(TEXT("ElementName"), ElemName);
		MarkCRDirty(Ctx);
	}
}

static void HandleCR_SetPinDefault(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	const FNexusArgs A(Op);
	const FString PinPath = A.Str(TEXT("PinPath"));
	const FString PinVal = A.Str(TEXT("PinDefaultValue"));
	if (PinPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetPinDefault requires PinPath"));
		return;
	}
	// 空值必须自己拦下：RigVMController 内部对空默认值有 ensure，透传过去会污染编辑器日志
	if (PinVal.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetPinDefault requires non-empty PinDefaultValue"));
		return;
	}
	URigVMController* VmCtrl = RequireVmCtrl(Ctx);
	if (!VmCtrl) return;
    URigVMGraph* Graph = VmCtrl->GetGraph(); URigVMPin* Pin = Graph ? Graph->FindPin(PinPath) : nullptr;
    if (Graph && Pin && CRState(Ctx) && CRState(Ctx)->CRBp) {
        auto Input = MakeShared<FJsonObject>(); Input->SetObjectField(TEXT("control_rig"), FNexusNativeEditorTools::Reference(CRState(Ctx)->CRBp->GetPathName())); Input->SetObjectField(TEXT("graph"), FNexusNativeEditorTools::Reference(Graph->GetPathName())); Input->SetObjectField(TEXT("pin"), FNexusNativeEditorTools::Reference(Pin->GetPathName())); Input->SetStringField(TEXT("value"), PinVal);
        if (FNexusNativeEditorTools::EditSync(TEXT("animation_toolset.toolsets.controlrig.ControlRigTools"), TEXT("set_pin_value"), Input, Ctx.Entry)) { if (!Ctx.Entry->HasField(TEXT("Error"))) MarkCRDirty(Ctx); return; }
    }
	const bool bOk = VmCtrl->SetPinDefaultValue(PinPath, PinVal, true, false, false);
	if (!bOk) Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetPinDefaultValue failed"));
	else MarkCRDirty(Ctx);
}

bool FNexusManageAssetControlRigCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UControlRigBlueprint* CRBp = FNexusAssetUtils::LoadAssetWithFallback<UControlRigBlueprint>(AssetPath);
	if (!CRBp)
	{
		OutError = FString::Printf(TEXT("ControlRig Blueprint not found: %s"), *AssetPath);
		return false;
	}
#if NX_UE_HAS_CONTROL_RIG_BLUEPRINT_GET_HIERARCHY
	URigHierarchy* Hier = CRBp->GetHierarchy();
#else
	URigHierarchy* Hier = CRBp->Hierarchy;
#endif
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

void FNexusManageAssetControlRigCapability::AfterPrepareTarget(
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

void FNexusManageAssetControlRigCapability::FinalizeTarget(void* Target) const
{
	FControlRigActionState* State = static_cast<FControlRigActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->CRBp)
	{
		State->CRBp->MarkPackageDirty();
		if (State->OutTop.IsValid())
		{
			State->OutTop->SetStringField(TEXT("Note"), TEXT("Modified; persist with SaveAsset"));
		}
	}
	delete State;
}

void FNexusManageAssetControlRigCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("RenameElement"),    &HandleCR_RenameElement);
	OutHandlers.Add(TEXT("SetControlColor"), &HandleCR_SetControlColor);
	OutHandlers.Add(TEXT("AddNull"),          &HandleCR_AddNull);
	OutHandlers.Add(TEXT("RemoveElement"),    &HandleCR_RemoveElement);
	OutHandlers.Add(TEXT("AddRigLink"),      &HandleCR_AddRigLink);
	OutHandlers.Add(TEXT("BreakRigLink"),    &HandleCR_BreakRigLink);
	OutHandlers.Add(TEXT("AddRigNode"),      &HandleCR_AddRigNode);
	OutHandlers.Add(TEXT("AddControl"),       &HandleCR_AddControl);
	OutHandlers.Add(TEXT("AddBone"),          &HandleCR_AddBone);
	OutHandlers.Add(TEXT("SetPinDefault"),   &HandleCR_SetPinDefault);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetControlRigCapability)

#endif // WITH_CONTROL_RIG
