// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusWidgetAdvancedOps.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusAssetUtils.h"
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
#if !NX_UE_HAS_UI_COMPONENT_MOVE
#include "Extensions/UIComponentContainer.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Components/Button.h"
#endif
#endif
#endif

#if NX_UE_HAS_UI_COMPONENTS && !NX_UE_HAS_UI_COMPONENT_MOVE
static bool NexusMoveLegacyUIComponent(UUIComponentWidgetBlueprintExtension* Extension, FName OwnerName,
    UClass* ComponentClass, UClass* RelativeClass, bool bMoveAfter, FString& Error)
{
    UUIComponent* Component = Extension->GetComponent(ComponentClass, OwnerName);
    UUIComponentContainer* Container = Component ? Cast<UUIComponentContainer>(Component->GetOuter()) : nullptr;
    if (!Container) { Error = TEXT("UIComponent container is unavailable"); return false; }
    TArray<FUIComponentTarget> Targets;
    Container->ForEachComponentTarget([&](const FUIComponentTarget& Target) { Targets.Add(Target); });
    const int32 From = Targets.IndexOfByPredicate([&](const FUIComponentTarget& Target) { return Target.GetTargetName() == OwnerName && Target.GetComponent() == Component; });
    if (From == INDEX_NONE) { Error = TEXT("UIComponent is not registered in its container"); return false; }
    const FUIComponentTarget Moved = Targets[From];
    Targets.RemoveAt(From);
    const int32 Relative = Targets.IndexOfByPredicate([&](const FUIComponentTarget& Target) { return Target.GetTargetName() == OwnerName && Target.GetComponent() && Target.GetComponent()->GetClass() == RelativeClass; });
    if (Relative == INDEX_NONE || Targets.ContainsByPredicate([](const FUIComponentTarget& Target) { return !Target.GetComponent(); }))
    { Error = TEXT("UIComponent relative target is unavailable"); return false; }
    Targets.Insert(Moved, Relative + (bMoveAfter ? 1 : 0));
    // UE5.6 没有移动接口；用公开登记接口重排，保留组件实例、属性及其他控件的顺序。
    for (const auto& Target : Targets) Container->RemoveComponent(Target.GetTargetName(), Target.GetComponent());
    for (const auto& Target : Targets) Container->AddComponent(Target.GetTargetName(), Target.GetComponent());
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusLegacyUIComponentMoveTest, "NexusLink.Independent.UIComponentMove", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusLegacyUIComponentMoveTest::RunTest(const FString&)
{
    // 用真实生成类提供两个具体组件类型，不修改原生抽象类标志。
    UBlueprint* FirstType = FKismetEditorUtilities::CreateBlueprint(UUIComponent::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusMoveFirst")), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    UBlueprint* SecondType = FKismetEditorUtilities::CreateBlueprint(UUIComponent::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusMoveSecond")), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    if (!TestTrue(TEXT("Concrete component classes created"), FirstType && SecondType && FirstType->GeneratedClass && SecondType->GeneratedClass)) return false;
    UWidgetBlueprint* Blueprint = NewObject<UWidgetBlueprint>();
    UUIComponentWidgetBlueprintExtension* Extension = NewObject<UUIComponentWidgetBlueprintExtension>(Blueprint);
    UButton* Owner = NewObject<UButton>();
    UButton* OtherOwner = NewObject<UButton>();
    UUIComponent* First = Extension->AddComponent(FirstType->GeneratedClass, Owner->GetFName());
    UUIComponent* OtherFirst = Extension->AddComponent(FirstType->GeneratedClass, OtherOwner->GetFName());
    UUIComponent* Second = Extension->AddComponent(SecondType->GeneratedClass, Owner->GetFName());
    UUIComponent* OtherSecond = Extension->AddComponent(SecondType->GeneratedClass, OtherOwner->GetFName());
    if (!TestTrue(TEXT("Components for both widgets created"), First && OtherFirst && Second && OtherSecond)) return false;
    First->Initialize(Owner); OtherFirst->Initialize(OtherOwner);
    UUIComponentContainer* Container = Cast<UUIComponentContainer>(First->GetOuter());
    if (!TestNotNull(TEXT("Component owns the real container"), Container)) return false;
    const auto Check = [&](const TArray<UUIComponent*>& Expected)
    {
        TArray<UUIComponent*> Actual;
        Container->ForEachComponentTarget([&](const FUIComponentTarget& Target) { Actual.Add(Target.GetComponent()); });
        TestTrue(TEXT("Global component order and identity match"), Actual == Expected);
        TestTrue(TEXT("Moved widget association is retained"), Extension->GetComponent(SecondType->GeneratedClass, Owner->GetFName()) == Second);
        TestTrue(TEXT("Other widget association is retained"), Extension->GetComponent(SecondType->GeneratedClass, OtherOwner->GetFName()) == OtherSecond);
        TestTrue(TEXT("Component state is retained"), First->GetOwner().Get() == Owner && OtherFirst->GetOwner().Get() == OtherOwner);
    };
    FString Error;
    if (!TestTrue(TEXT("Move before succeeds"), NexusMoveLegacyUIComponent(Extension, Owner->GetFName(), SecondType->GeneratedClass, FirstType->GeneratedClass, false, Error))) return false;
    Check({Second, First, OtherFirst, OtherSecond});
    if (!TestTrue(TEXT("Move after succeeds"), NexusMoveLegacyUIComponent(Extension, Owner->GetFName(), SecondType->GeneratedClass, FirstType->GeneratedClass, true, Error))) return false;
    Check({First, Second, OtherFirst, OtherSecond});
    return true;
}
#endif
#endif

bool FNexusWidgetAdvancedOps::Apply(UWidgetBlueprint* Blueprint, const FString& Action, const TSharedPtr<FJsonObject>& Args, const TSharedPtr<FJsonObject>& Entry, FString& Error)
{
    const FNexusArgs Arguments(Args);
    UWidget* Widget=Blueprint && Blueprint->WidgetTree?Blueprint->WidgetTree->FindWidget(FName(*Arguments.Str(TEXT("WidgetName")))):nullptr;
    if (!Widget) { Error=TEXT("WidgetName must identify a widget in this blueprint"); return false; }
    UClass* ComponentClass=nullptr; UClass* RelativeClass=nullptr;
    FMulticastDelegateProperty* Delegate=nullptr; FObjectProperty* Variable=nullptr;
    const FName OldName=Widget->GetFName(),NewName(*Arguments.Str(TEXT("NewWidgetName")));
    if (Action==TEXT("Rename")) {
        FText NameError; FKismetNameValidator Validator(Blueprint,OldName);
        if (NewName.IsNone() || !NewName.IsValidObjectName(NameError) || (NewName!=OldName && (Blueprint->WidgetTree->FindWidget(NewName) || Validator.IsValid(NewName)!=EValidatorResult::Ok))) { Error=TEXT("NewWidgetName must be a unique valid Blueprint member name"); return false; }
    } else if (Action==TEXT("SetVariable")) {
        if (!Args->HasTypedField<EJson::Boolean>(TEXT("bIsVariable"))) { Error=TEXT("SetVariable requires bIsVariable"); return false; }
    } else if (Action==TEXT("BindEvent")) {
        Delegate=FindFProperty<FMulticastDelegateProperty>(Widget->GetClass(),*Arguments.Str(TEXT("EventName")));
        Variable=Blueprint->SkeletonGeneratedClass?FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass,OldName):nullptr;
        if (!Delegate || !Delegate->HasAnyPropertyFlags(CPF_BlueprintAssignable) || !Widget->bIsVariable || !Variable || Blueprint->UbergraphPages.Num()==0) { Error=TEXT("BindEvent requires an exposed widget variable, assignable event and EventGraph; compile SetVariable first"); return false; }
    } else {
#if NX_UE_HAS_UI_COMPONENTS
        ComponentClass=FNexusAssetUtils::FindClassWithUPrefix(Arguments.Str(TEXT("ComponentClass")));
        if (!ComponentClass || !ComponentClass->IsChildOf(UUIComponent::StaticClass()) || ComponentClass->HasAnyClassFlags(CLASS_Abstract)) { Error=TEXT("ComponentClass must be a concrete UIComponent class"); return false; }
        auto* Extension=UWidgetBlueprintExtension::GetExtension<UUIComponentWidgetBlueprintExtension>(Blueprint);
        UUIComponent* Existing=Extension?Extension->GetComponent(ComponentClass,OldName):nullptr;
        if ((Action==TEXT("AddUIComponent") && Existing) || (Action!=TEXT("AddUIComponent") && !Existing)) { Error=TEXT("Component already exists or cannot be found on this widget"); return false; }
        if (Action==TEXT("MoveUIComponent")) {
            RelativeClass=FNexusAssetUtils::FindClassWithUPrefix(Arguments.Str(TEXT("RelativeComponentClass")));
            if (!RelativeClass || RelativeClass==ComponentClass || !Extension->GetComponent(RelativeClass,OldName)) { Error=TEXT("RelativeComponentClass must identify a different component on the same widget"); return false; }
        }
#else
        Error=TEXT("UIComponent editing requires UE 5.6 or newer"); return false;
#endif
    }
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
        else {
#if NX_UE_HAS_UI_COMPONENT_MOVE
            Extension->MoveComponent(OldName,ComponentClass,RelativeClass,Arguments.Bool(TEXT("bMoveAfter"),true));
#else
            if (!NexusMoveLegacyUIComponent(Extension,OldName,ComponentClass,RelativeClass,Arguments.Bool(TEXT("bMoveAfter"),true),Error)) return false;
#endif
        }
    }
#endif
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); Entry->SetStringField(TEXT("Backend"),TEXT("Independent")); return true;
}
