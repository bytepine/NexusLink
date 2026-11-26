// Copyright byteyang. All Rights Reserved.

#include "Modules/ModuleManager.h"

class FNexusLinkModelViewViewModelEditorModule final : public IModuleInterface
{
    void StartupModule() override;
    void ShutdownModule() override;
};

IMPLEMENT_MODULE(FNexusLinkModelViewViewModelEditorModule, NexusLinkModelViewViewModelEditor)


#include "Utils/NexusWidgetDiagnostics.h"

#if WITH_MVVM
#include "WidgetBlueprint.h"
#include "WidgetBlueprintExtension.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "MVVMBlueprintView.h"
#include "MVVMBlueprintViewBinding.h"
#include "MVVMBlueprintViewModelContext.h"
#include "Types/MVVMBindingMode.h"

static void NexusGatherMVVMBindings(UWidgetBlueprint* Blueprint, FNexusWidgetDiagnostics::FAppendBinding Append)
{
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
}
#endif

void FNexusLinkModelViewViewModelEditorModule::StartupModule()
{
#if WITH_MVVM
    FNexusWidgetDiagnostics::GatherMVVMBindings = &NexusGatherMVVMBindings;
#endif
}

void FNexusLinkModelViewViewModelEditorModule::ShutdownModule()
{
    FNexusWidgetDiagnostics::GatherMVVMBindings = nullptr;
}
