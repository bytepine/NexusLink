// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusWidgetAdvancedOps.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusEditorToolsets.h"
#include "Utils/NexusVersionCompat.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "K2Node_ComponentBoundEvent.h"
#include "EdGraph/EdGraph.h"
#if NX_UE_HAS_UI_COMPONENTS
#include "UIComponentWidgetBlueprintExtension.h"
#include "Extensions/UIComponent.h"
#endif

bool FNexusWidgetAdvancedOps::Apply(UWidgetBlueprint* Blueprint, const FString& Action, const TSharedPtr<FJsonObject>& Args, const TSharedPtr<FJsonObject>& Entry, FString& Error)
{
    const FNexusArgs Arguments(Args);
    UWidget* Widget=Blueprint && Blueprint->WidgetTree?Blueprint->WidgetTree->FindWidget(FName(*Arguments.Str(TEXT("WidgetName")))):nullptr;
    if (!Widget) { Error=TEXT("WidgetName must identify a widget in this blueprint"); return false; }
    auto Input=MakeShared<FJsonObject>(); Input->SetObjectField(TEXT("WidgetBlueprint"),FNexusNativeEditorTools::Reference(Blueprint->GetPathName()));
    Input->SetObjectField(TEXT("Widget"),FNexusNativeEditorTools::Reference(Widget->GetPathName())); FString Tool;
    UClass* ComponentClass=nullptr; UClass* RelativeClass=nullptr;
    FMulticastDelegateProperty* Delegate=nullptr; FObjectProperty* Variable=nullptr;
    const FName OldName=Widget->GetFName(),NewName(*Arguments.Str(TEXT("NewWidgetName")));
    if (Action==TEXT("Rename")) {
        FText NameError; FKismetNameValidator Validator(Blueprint,OldName);
        if (NewName.IsNone() || !NewName.IsValidObjectName(NameError) || (NewName!=OldName && (Blueprint->WidgetTree->FindWidget(NewName) || Validator.IsValid(NewName)!=EValidatorResult::Ok))) { Error=TEXT("NewWidgetName must be a unique valid Blueprint member name"); return false; }
        Tool=TEXT("RenameWidget"); Input->SetStringField(TEXT("NewDisplayName"),NewName.ToString());
    } else if (Action==TEXT("SetVariable")) {
        if (!Args->HasTypedField<EJson::Boolean>(TEXT("bIsVariable"))) { Error=TEXT("SetVariable requires bIsVariable"); return false; }
        Tool=TEXT("ToggleWidgetAsVariable"); Input->SetBoolField(TEXT("bIsVariable"),Arguments.Bool(TEXT("bIsVariable")));
    } else if (Action==TEXT("BindEvent")) {
        Delegate=FindFProperty<FMulticastDelegateProperty>(Widget->GetClass(),*Arguments.Str(TEXT("EventName")));
        Variable=Blueprint->SkeletonGeneratedClass?FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass,OldName):nullptr;
        if (!Delegate || !Delegate->HasAnyPropertyFlags(CPF_BlueprintAssignable) || !Widget->bIsVariable || !Variable || Blueprint->UbergraphPages.Num()==0) { Error=TEXT("BindEvent requires an exposed widget variable, assignable event and EventGraph; compile SetVariable first"); return false; }
        Tool=TEXT("BindToEventProperty"); Input->SetStringField(TEXT("EventName"),Delegate->GetName()); Input->SetStringField(TEXT("PropertyName"),OldName.ToString()); Input->SetObjectField(TEXT("PropertyClass"),FNexusNativeEditorTools::Reference(Widget->GetClass()->GetPathName()));
    } else {
#if NX_UE_HAS_UI_COMPONENTS
        ComponentClass=FNexusAssetUtils::FindClassWithUPrefix(Arguments.Str(TEXT("ComponentClass")));
        if (!ComponentClass || !ComponentClass->IsChildOf(UUIComponent::StaticClass()) || ComponentClass->HasAnyClassFlags(CLASS_Abstract)) { Error=TEXT("ComponentClass must be a concrete UIComponent class"); return false; }
        auto* Extension=UWidgetBlueprintExtension::GetExtension<UUIComponentWidgetBlueprintExtension>(Blueprint);
        UUIComponent* Existing=Extension?Extension->GetComponent(ComponentClass,OldName):nullptr;
        if ((Action==TEXT("AddUIComponent") && Existing) || (Action!=TEXT("AddUIComponent") && !Existing)) { Error=TEXT("Component already exists or cannot be found on this widget"); return false; }
        Tool=Action; Input->SetStringField(TEXT("WidgetName"),OldName.ToString()); Input->SetObjectField(Action==TEXT("MoveUIComponent")?TEXT("ComponentClassToMove"):TEXT("ComponentClass"),FNexusNativeEditorTools::Reference(ComponentClass->GetPathName()));
        if (Action==TEXT("MoveUIComponent")) {
            RelativeClass=FNexusAssetUtils::FindClassWithUPrefix(Arguments.Str(TEXT("RelativeComponentClass")));
            if (!RelativeClass || RelativeClass==ComponentClass || !Extension->GetComponent(RelativeClass,OldName)) { Error=TEXT("RelativeComponentClass must identify a different component on the same widget"); return false; }
            Input->SetObjectField(TEXT("RelativeToComponentClass"),FNexusNativeEditorTools::Reference(RelativeClass->GetPathName())); Input->SetBoolField(TEXT("bMoveAfter"),Arguments.Bool(TEXT("bMoveAfter"),true));
        }
#else
        Error=TEXT("UIComponent editing requires UE 5.6 or newer"); return false;
#endif
    }
    if (FNexusNativeEditorTools::EditSync(TEXT("UMGToolSet"),Tool,Input,Entry)) return !Entry->HasField(TEXT("Error"));
    Blueprint->Modify(); Widget->Modify();
    if (Action==TEXT("Rename")) {
        if (NewName==OldName) return true;
        if (!Widget->Rename(*NewName.ToString(),nullptr,REN_DontCreateRedirectors)) { Error=TEXT("Widget rename failed"); return false; }
#if NX_UE_HAS_BLUEPRINT_VARIABLE_RENAME_NOTICE
        Blueprint->OnVariableRenamed(OldName,NewName);
#endif
        for (auto& Binding : Blueprint->Bindings) if (Binding.ObjectName==OldName.ToString()) Binding.ObjectName=NewName.ToString();
        for (UWidgetAnimation* Animation : Blueprint->Animations) if (Animation) {
            Animation->Modify(); for (auto& Binding : Animation->AnimationBindings) if (Binding.WidgetName==OldName) Binding.WidgetName=NewName;
        }
#if NX_UE_HAS_UI_COMPONENTS
        if (auto* Extension=UWidgetBlueprintExtension::GetExtension<UUIComponentWidgetBlueprintExtension>(Blueprint)) { Extension->Modify(); Extension->RenameWidget(OldName,NewName); }
#endif
        FBlueprintEditorUtils::ReplaceVariableReferences(Blueprint,OldName,NewName);
        Entry->SetStringField(TEXT("WidgetName"),NewName.ToString());
    } else if (Action==TEXT("SetVariable")) Widget->bIsVariable=Arguments.Bool(TEXT("bIsVariable"));
    else if (Action==TEXT("BindEvent")) {
        const FName EventName=Delegate->GetFName();
        if (!FKismetEditorUtilities::FindBoundEventForComponent(Blueprint,EventName,OldName)) {
            UEdGraph* Graph=Blueprint->UbergraphPages[0]; Graph->Modify();
            auto* Event=NewObject<UK2Node_ComponentBoundEvent>(Graph,NAME_None,RF_Transactional); Event->CreateNewGuid(); Event->InitializeComponentBoundEventParams(Variable,Delegate); Graph->AddNode(Event,true,false); Event->AllocateDefaultPins();
        }
    }
#if NX_UE_HAS_UI_COMPONENTS
    else {
        auto* Extension=UWidgetBlueprintExtension::GetExtension<UUIComponentWidgetBlueprintExtension>(Blueprint);
        if (!Extension) { Extension=UWidgetBlueprintExtension::RequestExtension<UUIComponentWidgetBlueprintExtension>(Blueprint); }
        Extension->Modify();
        if (Action==TEXT("AddUIComponent")) {
#if NX_UE_HAS_WIDGET_BLUEPRINT_OPERATIONS
            FText Reason; UUIComponent* Added=Extension->AddComponent(ComponentClass,OldName,Reason);
#else
            UUIComponent* Added=Extension->AddComponent(ComponentClass,OldName);
#endif
            if (!Added) { Error=TEXT("UIComponent could not be added to this widget"); return false; }
        }
        else if (Action==TEXT("RemoveUIComponent")) Extension->RemoveComponent(ComponentClass,OldName);
        else Extension->MoveComponent(OldName,ComponentClass,RelativeClass,Arguments.Bool(TEXT("bMoveAfter"),true));
    }
#endif
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); Entry->SetStringField(TEXT("Backend"),TEXT("EngineCompatibility")); return true;
}
