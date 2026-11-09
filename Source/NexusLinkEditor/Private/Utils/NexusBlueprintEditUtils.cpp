// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusBlueprintEditUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusPinTypeUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "K2Node_Variable.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/InheritableComponentHandler.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompilerModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#if WITH_GAS
#include "GameplayAbilityBlueprint.h"
#include "Abilities/GameplayAbility.h"
#endif

static UClass* ResolveReparentClass(const FString& Name)
{
	if (Name.StartsWith(TEXT("/")) && !Name.StartsWith(TEXT("/Script/")) && !Name.EndsWith(TEXT("_C")))
		if (UBlueprint* Parent = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(Name)) return Parent->GeneratedClass;
	return FNexusAssetUtils::FindClassWithUPrefix(Name);
}

bool FNexusBlueprintEditUtils::ValidateLeaf(UBlueprint* Blueprint, FString& Error)
{
	if (!Blueprint) { Error = TEXT("Blueprint is required"); return false; }
	if (FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().IsLoadingAssets())
	{ Error = TEXT("Wait for Asset Registry scanning before variable edits"); return false; }
	TArray<FAssetData> Children;
	FBlueprintEditorUtils::GetChildrenOfBlueprint(Blueprint, Children);
	bool bHasChildren = Children.Num() > 0;
	for (TObjectIterator<UBlueprint> Child; Child && !bHasChildren; ++Child)
		bHasChildren = *Child != Blueprint && Blueprint->GeneratedClass && Child->ParentClass && Child->ParentClass->IsChildOf(Blueprint->GeneratedClass);
	if (bHasChildren) { Error = TEXT("Variable edits require a leaf Blueprint to protect descendant defaults and names"); return false; }
	return true;
}

bool FNexusBlueprintEditUtils::PreviewReparent(UBlueprint* Blueprint, const FString& ParentClass, const TSharedPtr<FJsonObject>& Result, FString& Error)
{
	Result->SetBoolField(TEXT("bCompatible"), false);
	Result->SetStringField(TEXT("PreviewKind"), TEXT("CompatibilityOnly"));
	if (!Blueprint || Blueprint->BlueprintType != BPTYPE_Normal)
	{ Error = TEXT("Reparent requires a normal class Blueprint, WidgetBlueprint, AnimBlueprint or GameplayAbilityBlueprint"); return false; }
	if (GEditor && (GEditor->PlayWorld || GEditor->IsPlayingSessionInEditor()))
	{ Error = TEXT("Stop PIE before reparenting a Blueprint"); return false; }
	UClass* Parent = ResolveReparentClass(ParentClass);
	UClass* Previous = Blueprint->ParentClass;
	if (!Previous || !Parent || !FKismetEditorUtilities::CanCreateBlueprintOfClass(Parent)
		|| Parent->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Interface)
		|| Parent->GetName().StartsWith(TEXT("SKEL_")) || Parent->GetName().StartsWith(TEXT("REINST_")))
	{ Error = TEXT("ParentClass must resolve to a valid Blueprint parent class"); return false; }
	if (Blueprint->GeneratedClass && Parent->IsChildOf(Blueprint->GeneratedClass))
	{ Error = TEXT("Reparent would create an inheritance cycle"); return false; }
	if (Blueprint->IsA(UWidgetBlueprint::StaticClass()))
	{
		if (!Parent->IsChildOf(UUserWidget::StaticClass())) { Error = TEXT("WidgetBlueprint parent must derive from UserWidget"); return false; }
	}
	else if (UAnimBlueprint* Animation = Cast<UAnimBlueprint>(Blueprint))
	{
		if (!Parent->IsChildOf(UAnimInstance::StaticClass())) { Error = TEXT("AnimBlueprint parent must derive from AnimInstance"); return false; }
		if (UAnimBlueprint* ParentAnimation = Cast<UAnimBlueprint>(Parent->ClassGeneratedBy))
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			const bool bDifferentSkeleton = Animation->TargetSkeleton && ParentAnimation->TargetSkeleton && Animation->TargetSkeleton != ParentAnimation->TargetSkeleton;
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
			if (bDifferentSkeleton) { Error = TEXT("Animation parent must use the same skeleton"); return false; }
		}
	}
#if WITH_GAS
	else if (Blueprint->IsA(UGameplayAbilityBlueprint::StaticClass()))
	{
		if (!Parent->IsChildOf(UGameplayAbility::StaticClass())) { Error = TEXT("GameplayAbilityBlueprint parent must derive from GameplayAbility"); return false; }
	}
#endif
	else
	{
		UClass* BlueprintType = nullptr;
		UClass* GeneratedType = nullptr;
		FModuleManager::LoadModuleChecked<IKismetCompilerInterface>(TEXT("KismetCompiler")).GetBlueprintTypesForClass(Parent, BlueprintType, GeneratedType);
		if (Blueprint->GetClass() != UBlueprint::StaticClass() || BlueprintType != Blueprint->GetClass())
		{ Error = TEXT("ParentClass requires a different specialized Blueprint asset type"); return false; }
	}
	if (Previous->IsChildOf(AActor::StaticClass()) != Parent->IsChildOf(AActor::StaticClass())
		|| Previous->IsChildOf(UActorComponent::StaticClass()) != Parent->IsChildOf(UActorComponent::StaticClass()))
	{ Error = TEXT("Reparent cannot cross Actor, ActorComponent and non-Actor families"); return false; }
	Result->SetStringField(TEXT("OldParentClass"), Previous->GetPathName());
	Result->SetStringField(TEXT("ParentClass"), Parent->GetPathName());
	Result->SetStringField(TEXT("BlueprintClass"), Blueprint->GetClass()->GetPathName());
	Result->SetBoolField(TEXT("bChanged"), Parent != Previous);
	Result->SetBoolField(TEXT("bPotentialDataLoss"), !Parent->IsChildOf(Previous));
	TArray<TSharedPtr<FJsonValue>> Conflicts, AtRisk, Children;
	if (Parent != Previous)
	{
		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
			if (Parent->FindPropertyByName(Variable.VarName)) Conflicts.Add(MakeShared<FJsonValueString>(Variable.VarName.ToString()));
		for (TFieldIterator<FProperty> Field(Previous); Field && AtRisk.Num() < 64; ++Field)
		{
			const FProperty* NewField = Parent->FindPropertyByName(Field->GetFName());
			if (!NewField || NewField->GetCPPType() != Field->GetCPPType()) AtRisk.Add(MakeShared<FJsonValueString>(Field->GetName()));
		}
		for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
			if (Interface.Interface && Parent->ImplementsInterface(Interface.Interface)) Conflicts.Add(MakeShared<FJsonValueString>(TEXT("Interface:") + Interface.Interface->GetPathName()));
	}
	Result->SetArrayField(TEXT("Conflicts"), Conflicts);
	Result->SetArrayField(TEXT("InheritedMembersAtRisk"), AtRisk);
	const bool bScanning = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().IsLoadingAssets();
	Result->SetBoolField(TEXT("bDependencyScanComplete"), !bScanning);
	TArray<FAssetData> Descendants;
	FBlueprintEditorUtils::GetChildrenOfBlueprint(Blueprint, Descendants);
	for (const auto& Child : Descendants)
	{
		if (Children.Num() >= 64) break;
#if NX_UE_HAS_ASSET_SOFT_OBJECT_PATH
		Children.Add(MakeShared<FJsonValueString>(Child.GetSoftObjectPath().ToString()));
#else
		Children.Add(MakeShared<FJsonValueString>(Child.ObjectPath.ToString()));
#endif
	}
	Result->SetNumberField(TEXT("DescendantCount"), Descendants.Num());
	Result->SetArrayField(TEXT("Descendants"), Children);
	Result->SetStringField(TEXT("Note"), TEXT("Compatibility snapshot only; inherited member list is bounded and compilation is required to validate graph references and descendants"));
	if (Conflicts.Num()) { Error = TEXT("New parent conflicts with owned variables or explicitly implemented interfaces"); return false; }
	Result->SetBoolField(TEXT("bCompatible"), true);
	return true;
}

bool FNexusBlueprintEditUtils::ChangeVariableType(UBlueprint* Blueprint, const FString& Name, const FString& Type, bool bAllowPotentialDataLoss, bool bPreviewOnly, const TSharedPtr<FJsonObject>& Result, FString& Error)
{
	if (!Blueprint) { Error = TEXT("Blueprint is required"); return false; }
	if (GEditor && GEditor->IsPlayingSessionInEditor()) { Error = TEXT("Stop PIE before changing variable types"); return false; }
	const int32 VariableIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(*Name));
	if (VariableIndex == INDEX_NONE) { Error = TEXT("VariableName must name an owned member variable"); return false; }
	FBPVariableDescription& Variable = Blueprint->NewVariables[VariableIndex];
	FEdGraphPinType NewType;
	if (!FNexusPinTypeUtils::ParsePinType(Type, NewType, Error)) return false;
	Result->SetObjectField(TEXT("OldType"), FNexusPinTypeUtils::DescribePinType(Variable.VarType));
	Result->SetObjectField(TEXT("NewType"), FNexusPinTypeUtils::DescribePinType(NewType));
	Result->SetStringField(TEXT("VariableName"), Name);
	Result->SetStringField(TEXT("Guid"), Variable.VarGuid.ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetBoolField(TEXT("bChanged"), Variable.VarType != NewType);
	if (Variable.VarType == NewType) return true;
	if (!Variable.RepNotifyFunc.IsNone() || (Variable.PropertyFlags & CPF_RepNotify) || ((NewType.IsMap() || NewType.IsSet()) && (Variable.PropertyFlags & CPF_Net)))
	{ Error = TEXT("Type changes cannot silently alter RepNotify or Set/Map replication settings"); return false; }
	if (!ValidateLeaf(Blueprint, Error)) return false;
	TArray<UK2Node_Variable*> References;
	FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, References);
	References.RemoveAll([&](UK2Node_Variable* Node) { return !Node || Node->GetVarName() != Variable.VarName || (Node->VariableReference.GetMemberGuid().IsValid() && Node->VariableReference.GetMemberGuid() != Variable.VarGuid); });
	Result->SetNumberField(TEXT("ReferenceCount"), References.Num());
	Result->SetBoolField(TEXT("bPotentialDataLoss"), true);
	Result->SetStringField(TEXT("Note"), TEXT("Type change clears the old default and reconstructs owned graph references; incompatible connections may break. Compile and inspect dependents afterwards"));
	if (bPreviewOnly) return true;
	if (!bAllowPotentialDataLoss) { Error = TEXT("Inspect the type preview and explicitly set bAllowPotentialDataLoss=true"); return false; }
	// 原生 ChangeMemberVariableType 遇到引用会弹对话框；复用同样的结构标记和节点重建，显式前置条件取代对话框。
	Blueprint->Modify();
	Variable.VarType = NewType;
	Variable.DefaultValue.Reset();
	Variable.FriendlyName = FName::NameToDisplayString(Variable.VarName.ToString(), NewType.PinCategory == UEdGraphSchema_K2::PC_Boolean);
	if (UClass* ObjectClass = Cast<UClass>(NewType.PinSubCategoryObject.Get()))
	{
		if (NewType.PinCategory == UEdGraphSchema_K2::PC_Object && ObjectClass->IsChildOf(AActor::StaticClass())) Variable.PropertyFlags |= CPF_DisableEditOnTemplate;
		else Variable.PropertyFlags &= ~CPF_DisableEditOnTemplate;
	}
	else Variable.PropertyFlags &= ~CPF_DisableEditOnTemplate;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	for (UK2Node_Variable* Node : References) { Node->Modify(); GetDefault<UEdGraphSchema_K2>()->ReconstructNode(*Node, true); }
	return true;
}

bool FNexusBlueprintEditUtils::Reparent(UBlueprint* Blueprint, const FString& ParentClass, bool bAllowPotentialDataLoss, const TSharedPtr<FJsonObject>& Result, FString& Error)
{
	if (!PreviewReparent(Blueprint, ParentClass, Result, Error)) return false;
	if (!Result->GetBoolField(TEXT("bChanged"))) return true;
	if (Result->GetBoolField(TEXT("bPotentialDataLoss")) && !bAllowPotentialDataLoss)
	{ Error = TEXT("Leaving the old parent hierarchy can discard defaults or break references; inspect preview and set bAllowPotentialDataLoss=true"); return false; }
	UClass* Parent = ResolveReparentClass(ParentClass);
	Blueprint->Modify();
	if (Blueprint->SimpleConstructionScript)
	{
		Blueprint->SimpleConstructionScript->Modify();
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes()) if (Node) Node->Modify();
	}
	if (UWidgetBlueprint* Widget = Cast<UWidgetBlueprint>(Blueprint)) if (Widget->WidgetTree) Widget->WidgetTree->Modify();
	Blueprint->ParentClass = Parent;
	if (Blueprint->SimpleConstructionScript) Blueprint->SimpleConstructionScript->ValidateSceneRootNodes();
	FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

UActorComponent* FNexusBlueprintEditUtils::FindComponent(UBlueprint* Blueprint, const FString& Name, bool bForWrite, FString& Error)
{
	if (!Blueprint || Name.IsEmpty()) { Error = TEXT("ComponentName is required"); return nullptr; }
	if (Blueprint->SimpleConstructionScript)
		if (USCS_Node* Node = Blueprint->SimpleConstructionScript->FindSCSNode(*Name)) return Node->ComponentTemplate;
	for (UClass* Class = Blueprint->GeneratedClass; Class; Class = Class->GetSuperClass())
	{
		UBlueprint* Owner = Cast<UBlueprint>(Class->ClassGeneratedBy);
		USCS_Node* Node = Owner && Owner->SimpleConstructionScript ? Owner->SimpleConstructionScript->FindSCSNode(*Name) : nullptr;
		if (!Node) continue;
		if (Owner == Blueprint) return Node->ComponentTemplate;
		UBlueprintGeneratedClass* Generated = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
		if (!Generated) break;
		if (bForWrite)
		{
			Blueprint->Modify();
			UInheritableComponentHandler* Handler = Blueprint->GetInheritableComponentHandler(true);
			if (Handler)
			{
				const FComponentKey Key(Node);
				if (UActorComponent* Existing = Handler->GetOverridenComponentTemplate(Key)) return Existing;
				Handler->Modify();
				return Handler->CreateOverridenComponentTemplate(Key);
			}
		}
		return Node->GetActualComponentTemplate(Generated);
	}
	AActor* Defaults = Blueprint->GeneratedClass ? Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject(false)) : nullptr;
	if (Defaults)
	{
		if (FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Defaults->GetClass(), *Name))
			if (UActorComponent* Component = Cast<UActorComponent>(Property->GetObjectPropertyValue_InContainer(Defaults))) return Component;
		TInlineComponentArray<UActorComponent*> Components;
		Defaults->GetComponents(Components);
		for (UActorComponent* Component : Components) if (Component && Component->GetName() == Name) return Component;
	}
	Error = TEXT("Component template not found in the target Blueprint");
	return nullptr;
}

bool FNexusBlueprintEditUtils::ResolveActorDefaultProperty(AActor* Actor, const TArray<FString>& Segments, FProperty*& Property, void*& Value, FString& Error)
{
	if (!Actor) { Error = TEXT("Actor is required"); return false; }
	if (Segments.Num() > 1)
	{
		FObjectPropertyBase* Member = FindFProperty<FObjectPropertyBase>(Actor->GetClass(), *Segments[0]);
		UBlueprint* Blueprint = Cast<UBlueprint>(Actor->GetClass()->ClassGeneratedBy);
		if (Blueprint && Member && Member->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
		{
			UActorComponent* Template = FindComponent(Blueprint, Segments[0], false, Error);
			if (!Template) return false;
			return FNexusPropertyUtils::ResolvePropertyWrite(Template, Segments, 1, Property, Value, Error);
		}
	}
	UObject* Defaults = Actor->GetClass()->GetDefaultObject(false);
	if (!Defaults) { Error = TEXT("Actor class has no CDO"); return false; }
	return FNexusPropertyUtils::ResolvePropertyWrite(Defaults, Segments, 0, Property, Value, Error);
}

UActorComponent* FNexusBlueprintEditUtils::FindComponentBaseline(UBlueprint* Blueprint, UActorComponent* Component, const FString& Name, const FString& Baseline, FString& Error)
{
	if (!Component) return nullptr;
	if (Baseline.Equals(TEXT("ParentTemplate"), ESearchCase::IgnoreCase))
	{
		if (UBlueprint* Parent = Blueprint && Blueprint->ParentClass ? Cast<UBlueprint>(Blueprint->ParentClass->ClassGeneratedBy) : nullptr)
		{
			FString Ignored;
			if (UActorComponent* ParentComponent = FindComponent(Parent, Name, false, Ignored)) return ParentComponent;
		}
		AActor* ParentDefaults = Blueprint && Blueprint->ParentClass ? Cast<AActor>(Blueprint->ParentClass->GetDefaultObject(false)) : nullptr;
		if (ParentDefaults)
		{
			TInlineComponentArray<UActorComponent*> Components;
			ParentDefaults->GetComponents(Components);
			for (UActorComponent* ParentComponent : Components) if (ParentComponent && ParentComponent->GetName() == Component->GetName()) return ParentComponent;
		}
		Error = TEXT("No matching parent component template; explicitly choose ClassDefault for an owned component");
		return nullptr;
	}
	return Cast<UActorComponent>(Component->GetClass()->GetDefaultObject());
}
