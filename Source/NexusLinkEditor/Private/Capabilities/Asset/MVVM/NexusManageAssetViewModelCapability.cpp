// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MVVM/NexusManageAssetViewModelCapability.h"

#if WITH_MVVM

#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusAssetEditorUtils.h"
#include "NexusMcpTool.h"
#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "WidgetBlueprintExtension.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "MVVMBlueprintView.h"
#include "MVVMBlueprintViewBinding.h"
#include "MVVMBlueprintViewModelContext.h"
#endif

void FNexusManageAssetViewModelCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetViewModel");
	Out.SearchAssetTypes = {TEXT("Widget")};
	Out.Description = TEXT("Edit WBP MVVM: add/remove ViewModel and Binding. UE 5.5+.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("AddViewModel"), TEXT("RemoveViewModel"), TEXT("AddBinding"), TEXT("RemoveBinding") }))
		.Prop(TEXT("ViewModelName"), FNexusSchema::Str(TEXT("ViewModel name")))
		.Prop(TEXT("ViewModelClass"), FNexusSchema::Str(TEXT("ViewModel UClass name (AddViewModel)")))
		.Prop(TEXT("BindingIndex"), FNexusSchema::Int(TEXT("Binding index (RemoveBinding)")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("WidgetBlueprint path")))
		.Required(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("mvvm"), TEXT("viewmodel"), TEXT("binding") };
	Out.RelatedCapabilities = { TEXT("GetAssetViewModel"), TEXT("GetAssetUserWidget"), TEXT("SaveAsset") };
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
	const FString VmName = A.Str(TEXT("ViewModelName"));
	const FString VmClassName = A.Str(TEXT("ViewModelClass"));
	if (VmName.IsEmpty() || VmClassName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddViewModel requires ViewModelName and ViewModelClass"));
		return;
	}
	UClass* VmClass = FNexusAssetUtils::FindClassWithUPrefix(VmClassName);
	if (!VmClass)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("ViewModel class not found: %s"), *VmClassName));
		return;
	}
	FMVVMBlueprintViewModelContext VmCtx;
	VmCtx.ViewModelName = FName(*VmName);
	VmCtx.NotifyFieldValueClass = VmClass;
	View->AddViewModel(VmCtx);
	Ctx.Entry->SetStringField(TEXT("ViewModelName"), VmName);
	MarkVMDirty(Ctx);
}

static void HandleVM_RemoveViewModel(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMVVMBlueprintView* View = VMState(Ctx)->View;
	const FString VmName = FNexusArgs(Op).Str(TEXT("ViewModelName"));
	bool bRemoved = false;
	for (const FMVVMBlueprintViewModelContext& VmCtx : View->GetViewModels())
	{
		if (VmCtx.GetViewModelName().ToString().Equals(VmName, ESearchCase::IgnoreCase))
		{
			View->RemoveViewModel(VmCtx.GetViewModelId());
			bRemoved = true;
			break;
		}
	}
	if (!bRemoved) Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ViewModel not found"));
	else MarkVMDirty(Ctx);
}

static void HandleVM_AddBinding(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	(void)Op;
	UMVVMBlueprintView* View = VMState(Ctx)->View;
	View->AddDefaultBinding();
	Ctx.Entry->SetNumberField(TEXT("BindingsCount"), View->GetNumBindings());
	MarkVMDirty(Ctx);
}

static void HandleVM_RemoveBinding(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMVVMBlueprintView* View = VMState(Ctx)->View;
	int32 Idx = 0;
	if (Op->HasField(TEXT("BindingIndex"))) Idx = static_cast<int32>(Op->GetNumberField(TEXT("BindingIndex")));
	if (Idx < 0 || Idx >= View->GetNumBindings())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("BindingIndex out of bounds"));
		return;
	}
	View->RemoveBindingAt(Idx);
	MarkVMDirty(Ctx);
}
#endif

bool FNexusManageAssetViewModelCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
#if !WITH_EDITOR
	OutError = TEXT("ManageAssetViewModel only available in editor builds");
	return false;
#else
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UWidgetBlueprint* WBP = FNexusAssetEditorUtils::LoadWidgetBP(AssetPath);
	if (!WBP)
	{
		OutError = FString::Printf(TEXT("WidgetBlueprint not found: %s"), *AssetPath);
		return false;
	}
	UMVVMWidgetBlueprintExtension_View* MvvmExt = UMVVMWidgetBlueprintExtension_View::RequestExtension<UMVVMWidgetBlueprintExtension_View>(WBP);
	if (!MvvmExt) { OutError = TEXT("Unable to get MVVM extension"); return false; }
	UMVVMBlueprintView* View = MvvmExt->GetBlueprintView();
	if (!View)
	{
		// RequestExtension 只挂扩展，不建 View；首次编辑必须自己建一个
		MvvmExt->CreateBlueprintViewInstance();
		View = MvvmExt->GetBlueprintView();
	}
	if (!View) { OutError = TEXT("BlueprintView is empty"); return false; }
	FViewModelActionState* State = new FViewModelActionState();
	State->WBP = WBP;
	State->View = View;
	OutTarget = State;
	return true;
#endif
}

void FNexusManageAssetViewModelCapability::AfterPrepareTarget(
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

void FNexusManageAssetViewModelCapability::FinalizeTarget(void* Target) const
{
#if WITH_EDITOR
	FViewModelActionState* State = static_cast<FViewModelActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->WBP)
	{
		State->WBP->MarkPackageDirty();
		if (State->OutTop.IsValid())
		{
			State->OutTop->SetStringField(TEXT("Hint"), TEXT("Call SaveAsset to persist changes"));
		}
	}
	delete State;
#else
	(void)Target;
#endif
}

void FNexusManageAssetViewModelCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
#if WITH_EDITOR
	OutHandlers.Add(TEXT("AddViewModel"),    &HandleVM_AddViewModel);
	OutHandlers.Add(TEXT("RemoveViewModel"), &HandleVM_RemoveViewModel);
	OutHandlers.Add(TEXT("AddBinding"),       &HandleVM_AddBinding);
	OutHandlers.Add(TEXT("RemoveBinding"),    &HandleVM_RemoveBinding);
#endif
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetViewModelCapability)

#endif
