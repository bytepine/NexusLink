// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusWidgetDiagnostics.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_BaseMCDelegate.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#if WITH_MVVM
#include "WidgetBlueprintExtension.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "MVVMBlueprintView.h"
#include "MVVMBlueprintViewBinding.h"
#include "MVVMBlueprintViewModelContext.h"
#include "Types/MVVMBindingMode.h"
#endif

struct FNexusWidgetConnection final
{
	FString Kind, Widget, Property, Source;
	TSharedPtr<FJsonObject> Evidence;
};
static FString NexusWidgetTarget(UEdGraphNode* Node)
{
	UEdGraphPin* Target = Node ? Node->FindPin(TEXT("self")) : nullptr;
	if (Target && Target->LinkedTo.Num() == 1)
		if (auto* Variable = Cast<UK2Node_VariableGet>(Target->LinkedTo[0]->GetOwningNode())) return Variable->VariableReference.GetMemberName().ToString();
	return Target && Target->LinkedTo.Num() ? TEXT("Unresolved") : TEXT("Self");
}
static TArray<UWidgetBlueprint*> NexusWidgetBlueprintChain(UWidgetBlueprint* Blueprint)
{
	TArray<UWidgetBlueprint*> Chain;
	for (auto* Current = Blueprint; Current && Chain.Num() < 64 && !Chain.Contains(Current); Current = Current->ParentClass ? Cast<UWidgetBlueprint>(Current->ParentClass->ClassGeneratedBy) : nullptr) Chain.Insert(Current, 0);
	return Chain;
}
static void NexusGatherWidgetConnections(const TArray<UWidgetBlueprint*>& Chain, TArray<FNexusWidgetConnection>& Connections)
{
	for (auto* Blueprint : Chain)
	{
		auto Append = [&](const FString& Kind, const FString& Widget, const FString& Property, const FString& Source, const TSharedPtr<FJsonObject>& Evidence)
		{
			Evidence->SetStringField(TEXT("Blueprint"), Blueprint->GetPathName());
			Evidence->SetStringField(TEXT("Status"), TEXT("DeclaredButUnconfirmed"));
			Connections.Add({Kind, Widget, Property, Source, Evidence});
		};
		for (const auto& Binding : Blueprint->Bindings)
		{
			auto Evidence = MakeShared<FJsonObject>();
			Evidence->SetStringField(TEXT("FunctionName"), Binding.FunctionName.ToString());
			Evidence->SetStringField(TEXT("SourceProperty"), Binding.SourceProperty.ToString());
			const FString DelegateProperty = Binding.PropertyName.ToString();
			Evidence->SetStringField(TEXT("DelegateProperty"), DelegateProperty);
			Append(TEXT("LegacyBinding"), Binding.ObjectName, DelegateProperty.EndsWith(TEXT("Delegate")) ? DelegateProperty.LeftChop(8) : DelegateProperty, Binding.FunctionName.ToString(), Evidence);
		}
#if WITH_MVVM
		if (auto* Extension = UWidgetBlueprintExtension::GetExtension<UMVVMWidgetBlueprintExtension_View>(Blueprint))
		{
			if (const auto* View = Extension->GetBlueprintView())
			for (int32 Index = 0; Index < View->GetNumBindings(); ++Index)
			{
				const auto* Binding = View->GetBindingAt(Index);
				if (!Binding) continue;
				auto AddDirection = [&](bool bReverse)
				{
					const auto& Target = bReverse ? Binding->SourcePath : Binding->DestinationPath;
					const auto& Origin = bReverse ? Binding->DestinationPath : Binding->SourcePath;
					auto Evidence = MakeShared<FJsonObject>();
					Evidence->SetStringField(TEXT("BindingId"), Binding->BindingId.ToString());
					Evidence->SetBoolField(TEXT("bEnabled"), Binding->bEnabled); Evidence->SetBoolField(TEXT("bCompile"), Binding->bCompile);
					Evidence->SetBoolField(TEXT("bReverse"), bReverse);
					Evidence->SetStringField(TEXT("SourceWidget"), Origin.GetWidgetName().ToString());
					Evidence->SetStringField(TEXT("SourceViewModelId"), Origin.GetViewModelId().ToString());
					Evidence->SetStringField(TEXT("TargetViewModelId"), Target.GetViewModelId().ToString());
					Evidence->SetStringField(TEXT("BindingMode"), UEnum::GetValueAsString(Binding->BindingType));
					const FString Source = Blueprint->GeneratedClass ? Origin.GetPropertyPath(Blueprint->GeneratedClass) : FString();
					const FString Destination = Blueprint->GeneratedClass ? Target.GetPropertyPath(Blueprint->GeneratedClass) : FString();
					const FString TargetWidget = !Target.GetWidgetName().IsNone() ? Target.GetWidgetName().ToString() : (Target.GetViewModelId().IsValid() ? TEXT("ViewModel:") + Target.GetViewModelId().ToString() : TEXT("Self"));
					Append(TEXT("MVVMBinding"), TargetWidget, Destination, Source, Evidence);
					if (!Binding->bCompile || !Binding->bEnabled) Evidence->SetStringField(TEXT("Status"), TEXT("KnownNotAppliedByDefault"));
				};
				if (Binding->BindingType != EMVVMBindingMode::OneWayToSource && Binding->BindingType != EMVVMBindingMode::OneTimeToSource) AddDirection(false);
				if (Binding->BindingType == EMVVMBindingMode::TwoWay || Binding->BindingType == EMVVMBindingMode::OneWayToSource || Binding->BindingType == EMVVMBindingMode::OneTimeToSource) AddDirection(true);
			}
		}
#endif
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (auto* Graph : Graphs)
		{
			if (!Graph) continue;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node || Connections.Num() >= 100000) continue;
				auto Evidence = MakeShared<FJsonObject>();
				Evidence->SetStringField(TEXT("Graph"), Graph->GetName());
				Evidence->SetStringField(TEXT("NodeGuid"), Node->NodeGuid.ToString());
				Evidence->SetStringField(TEXT("NodeClass"), Node->GetClass()->GetName());
				if (auto* BoundEvent = Cast<UK2Node_ComponentBoundEvent>(Node))
					Append(TEXT("BoundEvent"), BoundEvent->ComponentPropertyName.ToString(), BoundEvent->DelegatePropertyName.ToString(), Node->GetName(), Evidence);
				else if (auto* Delegate = Cast<UK2Node_BaseMCDelegate>(Node))
					Append(TEXT("DelegateNode"), NexusWidgetTarget(Node), Delegate->DelegateReference.GetMemberName().ToString(), Node->GetName(), Evidence);
				else if (auto* Set = Cast<UK2Node_VariableSet>(Node))
					Append(TEXT("GraphPropertyWrite"), NexusWidgetTarget(Node), Set->VariableReference.GetMemberName().ToString(), Node->GetName(), Evidence);
				else if (auto* Call = Cast<UK2Node_CallFunction>(Node))
				{
					const FString Function = Call->FunctionReference.GetMemberName().ToString();
					const FString Property = Function == TEXT("SetVisibility") ? TEXT("Visibility") : (Function == TEXT("SetRenderOpacity") ? TEXT("RenderOpacity") : (Function == TEXT("SetIsEnabled") ? TEXT("bIsEnabled") : FString()));
					if (!Property.IsEmpty()) Append(TEXT("GraphPropertyWrite"), NexusWidgetTarget(Node), Property, Function, Evidence);
				}
			}
		}
	}
}
static TSharedPtr<FJsonObject> NexusWidgetConnectionJson(const FNexusWidgetConnection& Connection)
{
	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("Kind"), Connection.Kind); Result->SetStringField(TEXT("WidgetName"), Connection.Widget);
	Result->SetStringField(TEXT("PropertyPath"), Connection.Property); Result->SetStringField(TEXT("Source"), Connection.Source);
	Result->SetObjectField(TEXT("Evidence"), Connection.Evidence);
	return Result;
}
static TSharedPtr<FJsonObject> NexusWidgetPropertySource(UObject* Object, const FString& Path, const FString& Kind)
{
	TArray<FString> Segments; Path.ParseIntoArray(Segments, TEXT("."), true);
	if (!Object || !Segments.Num()) return nullptr;
	FProperty* Property = nullptr; void* Storage = nullptr; FString Error;
	// 只定位反射存储并导出，不能使用会回退执行 Getter 的通用读取器。
	if (!FNexusPropertyUtils::ResolvePropertyWrite(Object, Segments, 0, Property, Storage, Error) || !Property || !Storage) return nullptr;
	FString Text; FNexusPropertyUtils::ExportText(Property, Text, Storage);
	auto Value = MakeShared<FJsonObject>();
	Value->SetStringField(TEXT("Name"), Path); Value->SetStringField(TEXT("Type"), Property->GetCPPType()); Value->SetStringField(TEXT("Value"), Text);
	auto Source = MakeShared<FJsonObject>();
	Source->SetStringField(TEXT("Kind"), Kind); Source->SetStringField(TEXT("ObjectPath"), Object->GetPathName());
	Source->SetObjectField(TEXT("Property"), Value); Source->SetStringField(TEXT("Status"), TEXT("StaticValue"));
	return Source;
}

void FNexusWidgetDiagnostics::Read(UWidgetBlueprint* Blueprint, const FString& Section, const TSharedPtr<FJsonObject>& Args, const TSharedPtr<FJsonObject>& Result, FString& Error)
{
	const FNexusArgs Arguments(Args);
	const int32 Offset = Arguments.Int(TEXT("Offset"), 0), Limit = Arguments.Int(TEXT("Limit"), 100);
	if (Offset < 0 || Offset > 1000000 || Limit < 1 || Limit > 500) { Error = TEXT("Diagnostic Offset 0-1000000 and Limit 1-500 required"); return; }
	const FString NameFilter = Arguments.Str(TEXT("NameFilter"));
	const auto Chain = NexusWidgetBlueprintChain(Blueprint);
	TArray<FNexusWidgetConnection> Connections; NexusGatherWidgetConnections(Chain, Connections);
	TArray<TSharedPtr<FJsonValue>> Records;
	int32 TotalRecords = 0;
	if (Section == TEXT("Connections"))
	{
		for (const auto& Connection : Connections)
		{
			if (!NameFilter.IsEmpty() && !Connection.Widget.Contains(NameFilter) && !Connection.Source.Contains(NameFilter)) continue;
			if (TotalRecords++ >= Offset && Records.Num() < Limit) Records.Add(MakeShared<FJsonValueObject>(NexusWidgetConnectionJson(Connection)));
		}
	}
	else
	{
		TArray<FString> Paths; FNexusPropertyUtils::ReadStringArray(Args, TEXT("PropertyPaths"), Paths);
		if (Paths.Num() > 32) { Error = TEXT("At most 32 PropertyPaths per diagnostic query"); return; }
		if (!Paths.Num()) Paths = {TEXT("Visibility"), TEXT("RenderOpacity"), TEXT("bIsEnabled")};
		TMap<FString, UWidget*> Widgets;
		for (auto* Owner : Chain) if (Owner->WidgetTree) Owner->WidgetTree->ForEachWidget([&](UWidget* Widget) { if (Widget) Widgets.Add(Widget->GetName(), Widget); });
		TArray<FString> Names; Widgets.GetKeys(Names); Names.Sort();
		for (const auto& Name : Names)
		{
			UWidget* Widget = Widgets[Name];
			if ((!NameFilter.IsEmpty() && !Name.Contains(NameFilter)) || (!Arguments.Str(TEXT("TypeFilter")).IsEmpty() && !Widget->GetClass()->GetName().Contains(Arguments.Str(TEXT("TypeFilter"))))) continue;
			for (const auto& Path : Paths)
			{
				if (TotalRecords++ < Offset || Records.Num() >= Limit) continue;
				TArray<TSharedPtr<FJsonValue>> Sources;
				TArray<UClass*> Classes; for (UClass* Class = Widget->GetClass(); Class && Class->IsChildOf(UWidget::StaticClass()); Class = Class->GetSuperClass()) Classes.Insert(Class, 0);
				for (UClass* Class : Classes)
					if (const auto Source = NexusWidgetPropertySource(Class->GetDefaultObject(), Path, Class->HasAnyClassFlags(CLASS_Native) ? TEXT("NativeClassDefault") : TEXT("GeneratedClassDefault"))) Sources.Add(MakeShared<FJsonValueObject>(Source));
				for (auto* Owner : Chain)
				{
					UWidget* Archetype = Owner->WidgetTree ? Owner->WidgetTree->FindWidget(*Name) : nullptr;
					if (const auto Source = NexusWidgetPropertySource(Archetype, Path, TEXT("WidgetTreeArchetype"))) Sources.Add(MakeShared<FJsonValueObject>(Source));
				}
				auto Record = MakeShared<FJsonObject>();
				Record->SetStringField(TEXT("WidgetName"), Name); Record->SetStringField(TEXT("PropertyPath"), Path);
				Record->SetBoolField(TEXT("bRuntimeWinnerCertain"), false);
				if (Sources.Num()) Record->SetObjectField(TEXT("LastStaticSource"), Sources.Last()->AsObject());
				else Record->SetStringField(TEXT("UnavailableReason"), TEXT("Property path not resolved on static widget sources"));
				for (const auto& Connection : Connections)
					if (Connection.Widget == Name && (Connection.Property.Equals(Path, ESearchCase::IgnoreCase) || Connection.Property.StartsWith(Path + TEXT("."))))
						Sources.Add(MakeShared<FJsonValueObject>(NexusWidgetConnectionJson(Connection)));
				Record->SetArrayField(TEXT("Sources"), Sources);
				Records.Add(MakeShared<FJsonValueObject>(Record));
			}
		}
	}
	Result->SetArrayField(*Section, Records);
	Result->SetNumberField(Section == TEXT("Connections") ? TEXT("ConnectionTotal") : TEXT("PropertyOverrideTotal"), TotalRecords);
	Result->SetBoolField(Section == TEXT("Connections") ? TEXT("bConnectionsTruncated") : TEXT("bPropertyOverridesTruncated"), Offset + Records.Num() < TotalRecords);
	Result->SetBoolField(TEXT("bSourceScanTruncated"), Connections.Num() >= 100000);
	Result->SetStringField(TEXT("AnalysisScope"), TEXT("Static CDO/archetypes, legacy/MVVM bindings, direct bound/delegate/variable-set nodes and selected widget setter calls. Graph execution, aliases, runtime handlers and arbitrary native setters are not proven."));
	Result->SetBoolField(TEXT("bCompleteConnectionAnalysis"), false);
}
