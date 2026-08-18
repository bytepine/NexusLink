// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MVVM/NexusManageAssetViewModelCapability.h"

#if WITH_MVVM

#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "MVVMBlueprintView.h"
#include "MVVMBlueprintViewBinding.h"
#include "MVVMBlueprintViewModelContext.h"
#endif

void FManageAssetViewModelCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_view_model");
	Out.SearchAssetTypes = {TEXT("Widget")};
	Out.Description = TEXT("Edit WBP MVVM: add/remove ViewModel and Binding. UE 5.5+.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("add_view_model"), TEXT("remove_view_model"), TEXT("add_binding"), TEXT("remove_binding") }))
		.Prop(TEXT("viewModelName"), FNexusSchema::Str(TEXT("ViewModel name")))
		.Prop(TEXT("viewModelClass"), FNexusSchema::Str(TEXT("ViewModel UClass name (add_view_model)")))
		.Prop(TEXT("bindingIndex"), FNexusSchema::Int(TEXT("Binding index (remove_binding)")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("WidgetBlueprint path")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("mvvm"), TEXT("viewmodel"), TEXT("binding") };
	Out.RelatedCapabilities = { TEXT("get_asset_view_model"), TEXT("get_asset_user_widget"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("For WBP add/remove MVVM ViewModel/Binding");
}

#if WITH_EDITOR
struct FViewModelActionState
{
	UWidgetBlueprint* WBP = nullptr;
	UMVVMBlueprintView* View = nullptr;
	bool bDirty = false;
	TSharedPtr<FJsonObject> OutTop;
};

static FViewModelActionState* VMState(FNexusActionContext& Ctx)
{
	return static_cast<FViewModelActionState*>(Ctx.Target);
}

static void MarkVMDirty(FNexusActionContext& Ctx)
{
	if (FViewModelActionState* S = VMState(Ctx))
	{
		S->bDirty = true;
	}
}

static void HandleVM_AddViewModel(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMVVMBlueprintView* View = VMState(Ctx)->View;
	const FNexusArgs A(Op);
	const FString VmName = A.Str(TEXT("viewModelName"));
	const FString VmClassName = A.Str(TEXT("viewModelClass"));
	if (VmName.IsEmpty() || VmClassName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_view_model requires viewModelName and viewModelClass"));
		return;
	}
	UClass* VmClass = FNexusAssetUtils::FindClassWithUPrefix(VmClassName);
	if (!VmClass)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("ViewModel class not found: %s"), *VmClassName));
		return;
	}
	FMVVMBlueprintViewModelContext VmCtx;
	VmCtx.ViewModelName = FName(*VmName);
	VmCtx.NotifyFieldValueClass = VmClass;
	View->AddViewModel(VmCtx);
	Ctx.Entry->SetStringField(TEXT("viewModelName"), VmName);
	MarkVMDirty(Ctx);
}

static void HandleVM_RemoveViewModel(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMVVMBlueprintView* View = VMState(Ctx)->View;
	const FString VmName = FNexusArgs(Op).Str(TEXT("viewModelName"));
	bool bRemoved = false;
	const TArray<FMVVMBlueprintViewModelContext> Vms = View->GetViewModels();
	for (const FMVVMBlueprintViewModelContext& VmCtx : Vms)
	{
		if (VmCtx.ViewModelName.ToString().Equals(VmName, ESearchCase::IgnoreCase))
		{
			View->RemoveViewModel(VmCtx.ViewModelContextId);
			bRemoved = true;
			break;
		}
	}
	if (!bRemoved) Ctx.Entry->SetStringField(TEXT("error"), TEXT("ViewModel not found"));
	else MarkVMDirty(Ctx);
}

static void HandleVM_AddBinding(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	(void)Op;
	UMVVMBlueprintView* View = VMState(Ctx)->View;
	View->AddDefaultBinding();
	Ctx.Entry->SetNumberField(TEXT("bindingsCount"), View->GetNumBindings());
	MarkVMDirty(Ctx);
}

static void HandleVM_RemoveBinding(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMVVMBlueprintView* View = VMState(Ctx)->View;
	int32 Idx = 0;
	if (Op->HasField(TEXT("bindingIndex"))) Idx = static_cast<int32>(Op->GetNumberField(TEXT("bindingIndex")));
	if (Idx < 0 || Idx >= View->GetNumBindings())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("bindingIndex out of bounds"));
		return;
	}
	View->RemoveBindingAt(Idx);
	MarkVMDirty(Ctx);
}
#endif

bool FManageAssetViewModelCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
#if !WITH_EDITOR
	OutError = TEXT("manage_asset_view_model only available in editor builds");
	return false;
#else
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UWidgetBlueprint* WBP = FNexusAssetUtils::LoadWidgetBP(AssetPath);
	if (!WBP)
	{
		OutError = FString::Printf(TEXT("WidgetBlueprint not found: %s"), *AssetPath);
		return false;
	}
	UMVVMWidgetBlueprintExtension_View* MvvmExt = UMVVMWidgetBlueprintExtension_View::Request(WBP);
	if (!MvvmExt) { OutError = TEXT("Unable to get MVVM extension"); return false; }
	UMVVMBlueprintView* View = MvvmExt->GetBlueprintView();
	if (!View) { OutError = TEXT("BlueprintView is empty"); return false; }
	FViewModelActionState* State = new FViewModelActionState();
	State->WBP = WBP;
	State->View = View;
	OutTarget = State;
	return true;
#endif
}

void FManageAssetViewModelCapability::AfterPrepareTarget(
	void* Target,
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& OutTop) const
{
	(void)Args;
#if WITH_EDITOR
	if (FViewModelActionState* State = static_cast<FViewModelActionState*>(Target))
	{
		State->OutTop = OutTop;
	}
#else
	(void)Target;
	(void)OutTop;
#endif
}

void FManageAssetViewModelCapability::FinalizeTarget(void* Target) const
{
#if WITH_EDITOR
	FViewModelActionState* State = static_cast<FViewModelActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->WBP)
	{
		State->WBP->MarkPackageDirty();
		if (State->OutTop.IsValid())
		{
			State->OutTop->SetStringField(TEXT("hint"), TEXT("Call save_asset to persist changes"));
		}
	}
	delete State;
#else
	(void)Target;
#endif
}

void FManageAssetViewModelCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
#if WITH_EDITOR
	OutHandlers.Add(TEXT("add_view_model"),    &HandleVM_AddViewModel);
	OutHandlers.Add(TEXT("remove_view_model"), &HandleVM_RemoveViewModel);
	OutHandlers.Add(TEXT("add_binding"),       &HandleVM_AddBinding);
	OutHandlers.Add(TEXT("remove_binding"),    &HandleVM_RemoveBinding);
#endif
}

REGISTER_MCP_CAPABILITY(FManageAssetViewModelCapability)

#endif
