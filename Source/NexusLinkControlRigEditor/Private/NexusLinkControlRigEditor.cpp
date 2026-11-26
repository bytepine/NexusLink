// Copyright byteyang. All Rights Reserved.
#include "Modules/ModuleManager.h"
#include "Utils/NexusAnimationAdvancedTools.h"
#if WITH_CONTROL_RIG
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "ControlRig.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyDefines.h"
static FString NexusAnimationControlTypeName(const FString& Name)
{
    FString Result;
    for (int32 Index = 0; Index < Name.Len(); ++Index) {
        if (Index > 0 && FChar::IsUpper(Name[Index]) && (FChar::IsLower(Name[Index - 1]) || (Index + 1 < Name.Len() && FChar::IsLower(Name[Index + 1])))) Result += TEXT('_');
        Result += FChar::ToUpper(Name[Index]);
    }
    return Result;
}

static ERigControlType NexusAnimationControlType(const URigHierarchy* Hierarchy, const FRigControlElement* Control)
{
#if NX_UE_HAS_RIG_CONTROL_SETTINGS_TOPOLOGY
    return Hierarchy->GetControlSettingsTopology(Control).ControlType;
#else
    return Control->Settings.ControlType;
#endif
}

static TOptional<bool> NexusReadRigControls(UObject* Rig, const TSharedPtr<FJsonObject>& Result, FString& Error)
{
if (UControlRig* ControlRig = Cast<UControlRig>(Rig)) {
    URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
    if (!Hierarchy) { Error = TEXT("Control Rig hierarchy is unavailable"); return false; }
    TArray<TSharedPtr<FJsonValue>> Controls;
    for (const auto& Key : Hierarchy->GetControlKeys()) if (const auto* Control = Hierarchy->Find<FRigControlElement>(Key)) {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("name"), Key.Name.ToString());
        Item->SetStringField(TEXT("type"), NexusAnimationControlTypeName(StaticEnum<ERigControlType>()->GetNameStringByValue(static_cast<int64>(NexusAnimationControlType(Hierarchy, Control)))));
        Controls.Add(MakeShared<FJsonValueObject>(Item));
    }
    Result->SetArrayField(TEXT("Items"), Controls); return true;
}
    return {};
}

static bool NexusValidateRigControl(UObject* Rig, const TSharedPtr<FJsonObject>& Args, const FString& Tool, UFunction*& Function, FString& Error)
{
    if (Rig && Args->HasTypedField<EJson::String>(TEXT("control_name"))) {
        UControlRig* ControlRig = Cast<UControlRig>(Rig); URigHierarchy* Hierarchy = ControlRig ? ControlRig->GetHierarchy() : nullptr;
        const FRigElementKey Key(FName(*FNexusArgs(Args).Str(TEXT("control_name"))), ERigElementType::Control);
        const auto* Control = Hierarchy ? Hierarchy->Find<FRigControlElement>(Key) : nullptr;
        if (!Control) { Error = TEXT("Named control was not found in the bound Control Rig"); return false; }
        const ERigControlType ControlType = NexusAnimationControlType(Hierarchy, Control);
        if ((Tool == TEXT("get_transform") || Tool == TEXT("set_transform")) && ControlType == ERigControlType::EulerTransform) {
            const FName Name(Tool == TEXT("get_transform") ? TEXT("GetLocalControlRigEulerTransform") : TEXT("SetLocalControlRigEulerTransform"));
            Function = Function->GetOuterUClass()->FindFunctionByName(Name);
            if (!Function) { Error = TEXT("Euler transform control scripting is unavailable"); return false; }
        }
        else if ((Tool == TEXT("get_transform") || Tool == TEXT("set_transform")) && ControlType != ERigControlType::Transform) { Error = TEXT("Named control is not a transform control"); return false; }
        if ((Tool == TEXT("get_bool") || Tool == TEXT("set_bool")) && ControlType != ERigControlType::Bool) { Error = TEXT("Named control is not a Bool control"); return false; }
        if (Tool == TEXT("set_int") && ControlType != ERigControlType::Integer) { Error = TEXT("Named control is not an Integer control"); return false; }
        if (Tool == TEXT("get_float") || Tool == TEXT("set_float")) {
            const FString TypeName = StaticEnum<ERigControlType>()->GetNameStringByValue(static_cast<int64>(ControlType));
            if (TypeName != TEXT("Float") && TypeName != TEXT("ScaleFloat")) { Error = TEXT("Named control is not a Float control"); return false; }
        }
        if (Tool == TEXT("set_euler_transform") && ControlType != ERigControlType::EulerTransform) { Error = TEXT("Named control is not an Euler transform control"); return false; }
    }
    return true;
}
#endif

class FNexusLinkControlRigEditorModule final : public IModuleInterface
{
    void StartupModule() override
    {
#if WITH_CONTROL_RIG
        FNexusAnimationAdvancedTools::ReadRigControls = &NexusReadRigControls;
        FNexusAnimationAdvancedTools::ValidateRigControl = &NexusValidateRigControl;
#endif
    }

    void ShutdownModule() override
    {
        FNexusAnimationAdvancedTools::ReadRigControls = nullptr;
        FNexusAnimationAdvancedTools::ValidateRigControl = nullptr;
    }
};

IMPLEMENT_MODULE(FNexusLinkControlRigEditorModule, NexusLinkControlRigEditor)
