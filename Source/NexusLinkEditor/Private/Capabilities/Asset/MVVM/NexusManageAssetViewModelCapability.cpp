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
#include "Editor.h"
#include "Blueprint/WidgetTree.h"
#include "Bindings/MVVMBindingHelper.h"
#include "Bindings/MVVMConversionFunctionHelper.h"
#include "MVVMEditorSubsystem.h"
#include "MVVMViewModelBase.h"
#include "MVVMBlueprintViewConversionFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
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
	Out.bInjectCompile = true;
	Out.SearchAssetTypes = {TEXT("Widget")};
	Out.Description = TEXT("Edit WBP data bindings. Contexts, property paths, direction and unary conversions; UE 5.5+.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("AddViewModel"), TEXT("RemoveViewModel"), TEXT("AddBinding"), TEXT("RemoveBinding") }))
		.Prop(TEXT("ViewModelName"), FNexusSchema::Str(TEXT("ViewModel name")))
		.Prop(TEXT("ViewModelClass"), FNexusSchema::Str(TEXT("ViewModel UClass name (AddViewModel)")))
		.Prop(TEXT("BindingIndex"), FNexusSchema::Int(TEXT("Binding index (RemoveBinding)")))
		.Prop(TEXT("BindingId"), FNexusSchema::Str(TEXT("Binding GUID (RemoveBinding; preferred over index)")))
		.Prop(TEXT("SourceContext"), FNexusSchema::Str(TEXT("Self, ViewModel:<name>, or Widget:<name>"), TEXT("Self")))
		.Prop(TEXT("SourcePath"), FNexusSchema::Str(TEXT("Source dotted property path (AddBinding)")))
		.Prop(TEXT("DestinationContext"), FNexusSchema::Str(TEXT("Self, ViewModel:<name>, or Widget:<name>"), TEXT("Self")))
		.Prop(TEXT("DestinationPath"), FNexusSchema::Str(TEXT("Destination dotted property path (AddBinding)")))
		.Prop(TEXT("BindingType"), FNexusSchema::Enum(TEXT("Binding direction (AddBinding)"),
			{ TEXT("OneWayToDestination"), TEXT("TwoWay"), TEXT("OneWayToSource"), TEXT("OneTimeToDestination"), TEXT("OneTimeToSource") }, TEXT("OneWayToDestination")))
		.Prop(TEXT("ConversionFunction"), FNexusSchema::Str(TEXT("Forward unary UFunction path; discover via get")))
		.Prop(TEXT("ReverseConversionFunction"), FNexusSchema::Str(TEXT("Reverse unary UFunction path; discover via get")))
		.Prop(TEXT("bEnabled"), FNexusSchema::Bool(TEXT("Binding enabled at runtime"), true, true))
		.Prop(TEXT("bCompile"), FNexusSchema::Bool(TEXT("Include binding in compilation"), true, true))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("WidgetBlueprint path")))
		.Required(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("mvvm"), TEXT("viewmodel"), TEXT("binding") };
	Out.RelatedCapabilities = { TEXT("GetAssetViewModel"), TEXT("GetAssetUserWidget"), TEXT("SaveAsset") };
	Out.WhenToUse = TEXT("Add/remove WBP ViewModels and bindings; specify both paths for a complete binding; compile and save separately");
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
	FViewModelActionState* State = VMState(Ctx);
	if (!State || !State->View || !State->WBP) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("MVVM target unavailable")); return; }
	UMVVMBlueprintView* View = State->View;
	const FNexusArgs A(Op);
	const FString VmName = A.Str(TEXT("ViewModelName"));
	const FString VmClassName = A.Str(TEXT("ViewModelClass"));
	if (VmName.IsEmpty() || VmClassName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddViewModel requires ViewModelName and ViewModelClass"));
		return;
	}
	UClass* VmClass = FNexusAssetUtils::FindClassWithUPrefix(VmClassName);
	if (!VmClass || !VmClass->IsChildOf(UMVVMViewModelBase::StaticClass()))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("ViewModel class not found: %s"), *VmClassName));
		return;
	}
	if (View->FindViewModel(FName(*VmName))) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ViewModel name already exists")); return; }
	State->WBP->Modify();
	View->Modify();
	// 引擎构造器同时初始化 GUID 和允许的创建策略；默认构造后填类/名会留下无效 GUID。
	FMVVMBlueprintViewModelContext VmCtx(VmClass, FName(*VmName));
	if (!VmCtx.GetViewModelId().IsValid()) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ViewModel class has no supported creation strategy")); return; }
	View->AddViewModel(VmCtx);
	Ctx.Entry->SetStringField(TEXT("ViewModelName"), VmName);
	Ctx.Entry->SetStringField(TEXT("ViewModelId"), VmCtx.GetViewModelId().ToString());
	MarkVMDirty(Ctx);
}

static void HandleVM_RemoveViewModel(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FViewModelActionState* State = VMState(Ctx);
	if (!State || !State->View || !State->WBP) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("MVVM target unavailable")); return; }
	UMVVMBlueprintView* View = State->View;
	const FString VmName = FNexusArgs(Op).Str(TEXT("ViewModelName"));
	bool bRemoved = false;
	for (const FMVVMBlueprintViewModelContext& VmCtx : View->GetViewModels())
	{
		if (VmCtx.GetViewModelName().ToString().Equals(VmName, ESearchCase::IgnoreCase))
		{
			State->WBP->Modify();
			View->Modify();
			View->RemoveViewModel(VmCtx.GetViewModelId());
			bRemoved = true;
			break;
		}
	}
	if (!bRemoved) Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ViewModel not found"));
	else MarkVMDirty(Ctx);
}

// 每一段都写入引擎的字段引用，不能仅保存最后一个字段；同类多个 VM 用名字区分。
static bool ResolveVMPath(FViewModelActionState& State, const FString& Context, const FString& Text,
	FMVVMBlueprintPropertyPath& OutPath, const FProperty*& OutLeaf, FString& OutError)
{
	UStruct* Owner = nullptr;
	if (Context == TEXT("Self"))
	{
		Owner = State.WBP->SkeletonGeneratedClass ? State.WBP->SkeletonGeneratedClass : State.WBP->GeneratedClass;
		OutPath.SetSelfContext();
	}
	else if (Context.StartsWith(TEXT("ViewModel:")))
	{
		const FMVVMBlueprintViewModelContext* Model = State.View->FindViewModel(FName(*Context.Mid(10)));
		if (Model) { Owner = Model->GetViewModelClass(); OutPath.SetViewModelId(Model->GetViewModelId()); }
	}
	else if (Context.StartsWith(TEXT("Widget:")) && State.WBP->WidgetTree)
	{
		if (UWidget* Widget = State.WBP->WidgetTree->FindWidget(FName(*Context.Mid(7))))
		{
			Owner = Widget->GetClass();
			OutPath.SetWidgetName(Widget->GetFName());
		}
	}
	if (!Owner) { OutError = FString::Printf(TEXT("Unknown binding context: %s"), *Context); return false; }
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT("."), false);
	OutLeaf = nullptr;
	for (int32 PartIndex = 0; PartIndex < Parts.Num(); ++PartIndex)
	{
		FProperty* Property = Owner && !Parts[PartIndex].IsEmpty() ? FindFProperty<FProperty>(Owner, FName(*Parts[PartIndex])) : nullptr;
		if (!Property) { OutError = FString::Printf(TEXT("Invalid property path %s in %s at %s"), *Text, *Context, *Parts[PartIndex]); return false; }
		OutPath.AppendPropertyPath(State.WBP, UE::MVVM::FMVVMConstFieldVariant(Property));
		OutLeaf = Property;
		Owner = nullptr;
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property)) Owner = ObjectProperty->PropertyClass;
		else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property)) Owner = StructProperty->Struct;
	}
	if (!OutLeaf) { OutError = TEXT("Both SourcePath and DestinationPath must be non-empty"); return false; }
	return true;
}

static bool ResolveVMConversion(UMVVMEditorSubsystem* Subsystem, UWidgetBlueprint* Blueprint,
	const FString& FunctionPath, const FProperty* Source, const FProperty* Destination,
	const UFunction*& OutFunction, FString& OutError)
{
	OutFunction = nullptr;
	if (!UE::MVVM::BindingHelper::IsValidForSourceBinding(Source) || !UE::MVVM::BindingHelper::IsValidForDestinationBinding(Destination))
	{
		OutError = TEXT("Binding direction requires a readable source and writable destination"); return false;
	}
	if (FunctionPath.IsEmpty())
	{
		if (UE::MVVM::BindingHelper::ArePropertiesCompatible(Source, Destination)) return true;
		OutError = FString::Printf(TEXT("Incompatible property types (%s -> %s); specify an explicit unary conversion"), *Source->GetCPPType(), *Destination->GetCPPType()); return false;
	}
	for (const UE::MVVM::FConversionFunctionValue& Candidate : Subsystem->GetConversionFunctions(Blueprint, Source, Destination))
	{
		const UFunction* Function = Candidate.GetFunction();
		if (Function && Function->GetPathName() == FunctionPath && Subsystem->IsSimpleConversionFunction(Function)
			&& Subsystem->IsValidConversionFunction(Blueprint, Function, Source, Destination))
		{
			OutFunction = Function; return true;
		}
	}
	OutError = FString::Printf(TEXT("Unavailable or incompatible unary conversion: %s"), *FunctionPath);
	return false;
}

static void HandleVM_AddBinding(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FViewModelActionState* State = VMState(Ctx);
	if (!State || !State->View || !State->WBP) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("MVVM target unavailable")); return; }
	const FNexusArgs Args(Op);
	const bool bHasPaths = Op->HasField(TEXT("SourcePath")) || Op->HasField(TEXT("DestinationPath"));
	// 兼容旧版的空绑定；只要使用了新配置字段，就必须提供完整两端。
	const bool bConfigured = bHasPaths || Op->HasField(TEXT("SourceContext")) || Op->HasField(TEXT("DestinationContext"))
		|| Op->HasField(TEXT("BindingType")) || Op->HasField(TEXT("ConversionFunction")) || Op->HasField(TEXT("ReverseConversionFunction"))
		|| Op->HasField(TEXT("bEnabled")) || Op->HasField(TEXT("bCompile"));
	FMVVMBlueprintPropertyPath SourcePath, DestinationPath;
	const FProperty* Source = nullptr;
	const FProperty* Destination = nullptr;
	const UFunction* ForwardConversion = nullptr;
	const UFunction* ReverseConversion = nullptr;
	UMVVMEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>() : nullptr;
	EMVVMBindingMode Mode = EMVVMBindingMode::OneWayToDestination;
	if (bConfigured)
	{
		const FString ModeName = Args.Str(TEXT("BindingType"), TEXT("OneWayToDestination"));
		if (ModeName == TEXT("TwoWay")) Mode = EMVVMBindingMode::TwoWay;
		else if (ModeName == TEXT("OneWayToSource")) Mode = EMVVMBindingMode::OneWayToSource;
		else if (ModeName == TEXT("OneTimeToDestination")) Mode = EMVVMBindingMode::OneTimeToDestination;
		else if (ModeName == TEXT("OneTimeToSource")) Mode = EMVVMBindingMode::OneTimeToSource;
		else if (ModeName != TEXT("OneWayToDestination")) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Unknown BindingType")); return; }
		FString Error;
		if (!Subsystem) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("MVVM editor subsystem unavailable")); return; }
		if (!ResolveVMPath(*State, Args.Str(TEXT("SourceContext"), TEXT("Self")), Args.Str(TEXT("SourcePath")), SourcePath, Source, Error)
			|| !ResolveVMPath(*State, Args.Str(TEXT("DestinationContext"), TEXT("Self")), Args.Str(TEXT("DestinationPath")), DestinationPath, Destination, Error))
		{ Ctx.Entry->SetStringField(TEXT("Error"), Error); return; }
		const bool bForward = Mode != EMVVMBindingMode::OneWayToSource && Mode != EMVVMBindingMode::OneTimeToSource;
		const bool bReverse = Mode == EMVVMBindingMode::TwoWay || !bForward;
		if ((!bForward && !Args.Str(TEXT("ConversionFunction")).IsEmpty()) || (!bReverse && !Args.Str(TEXT("ReverseConversionFunction")).IsEmpty()))
		{ Ctx.Entry->SetStringField(TEXT("Error"), TEXT("ConversionFunction specified for an inactive binding direction")); return; }
		if ((bForward && !ResolveVMConversion(Subsystem, State->WBP, Args.Str(TEXT("ConversionFunction")), Source, Destination, ForwardConversion, Error))
			|| (bReverse && !ResolveVMConversion(Subsystem, State->WBP, Args.Str(TEXT("ReverseConversionFunction")), Destination, Source, ReverseConversion, Error)))
		{ Ctx.Entry->SetStringField(TEXT("Error"), Error); return; }
		if ((ForwardConversion && !Subsystem->IsValidConversionFunction(State->WBP, ForwardConversion, SourcePath, DestinationPath))
			|| (ReverseConversion && !Subsystem->IsValidConversionFunction(State->WBP, ReverseConversion, DestinationPath, SourcePath)))
		{ Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Conversion rejected for the resolved property paths")); return; }
	}
	State->WBP->Modify();
	State->View->Modify();
	FMVVMBlueprintViewBinding& Binding = State->View->AddDefaultBinding();
	if (bConfigured)
	{
		Binding.SourcePath = SourcePath;
		Binding.DestinationPath = DestinationPath;
		Binding.BindingType = Mode;
		Binding.bEnabled = Args.Bool(TEXT("bEnabled"), true);
		Binding.bCompile = Args.Bool(TEXT("bCompile"), true);
		// 直接初始化已校验的转换对象，避免各版本高层 setter 清空路径及反向校验差异。
		for (bool bForward : {true, false})
		{
			const UFunction* Function = bForward ? ForwardConversion : ReverseConversion;
			if (!Function) continue;
			UMVVMBlueprintViewConversionFunction* Conversion = NewObject<UMVVMBlueprintViewConversionFunction>(State->WBP, NAME_None, RF_Transactional);
			if (!Conversion)
			{
				const int32 BindingIndex = State->View->GetNumBindings() - 1;
				State->View->RemoveBindingAt(BindingIndex);
				Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Unable to create conversion object; new binding removed"));
				return;
			}
			Conversion->SetDestinationPath(bForward ? DestinationPath : SourcePath);
			Conversion->Initialize(State->WBP, UE::MVVM::ConversionFunctionHelper::CreateWrapperName(Binding, bForward), FMVVMBlueprintFunctionReference(State->WBP, Function));
			if (bForward) Binding.Conversion.SourceToDestinationConversion = Conversion;
			else Binding.Conversion.DestinationToSourceConversion = Conversion;
			if (const FProperty* Argument = UE::MVVM::BindingHelper::GetFirstArgumentProperty(Function))
			{
				FName ArgumentName = Argument->GetFName();
				Subsystem->SetPathForConversionFunctionArgument(State->WBP, Binding, FMVVMBlueprintPinId(MakeArrayView(&ArgumentName, 1)), bForward ? SourcePath : DestinationPath, bForward);
			}
		}
	}
	Ctx.Entry->SetStringField(TEXT("BindingId"), Binding.BindingId.ToString());
	Ctx.Entry->SetNumberField(TEXT("BindingIndex"), State->View->GetNumBindings() - 1);
	Ctx.Entry->SetNumberField(TEXT("BindingsCount"), State->View->GetNumBindings());
	MarkVMDirty(Ctx);
}

static void HandleVM_RemoveBinding(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FViewModelActionState* State = VMState(Ctx);
	if (!State || !State->View || !State->WBP) { Ctx.Entry->SetStringField(TEXT("Error"), TEXT("MVVM target unavailable")); return; }
	UMVVMBlueprintView* View = State->View;
	int32 Idx = INDEX_NONE;
	if (Op->HasField(TEXT("BindingIndex"))) Idx = static_cast<int32>(Op->GetNumberField(TEXT("BindingIndex")));
	if (Op->HasField(TEXT("BindingId")))
	{
		FGuid BindingId;
		if (Op->HasField(TEXT("BindingIndex")) || !FGuid::Parse(FNexusArgs(Op).Str(TEXT("BindingId")), BindingId))
		{ Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Provide one valid BindingId or BindingIndex")); return; }
		for (int32 BindingIndex = 0; BindingIndex < View->GetNumBindings(); ++BindingIndex)
		{
			const FMVVMBlueprintViewBinding* Binding = View->GetBindingAt(BindingIndex);
			if (Binding && Binding->BindingId == BindingId) { Idx = BindingIndex; break; }
		}
	}
	if (Idx < 0 || Idx >= View->GetNumBindings())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("BindingIndex out of bounds"));
		return;
	}
	State->WBP->Modify();
	View->Modify();
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
	WBP->Modify();
	UMVVMWidgetBlueprintExtension_View* MvvmExt = UMVVMWidgetBlueprintExtension_View::RequestExtension<UMVVMWidgetBlueprintExtension_View>(WBP);
	if (!MvvmExt) { OutError = TEXT("Unable to get MVVM extension"); return false; }
	UMVVMBlueprintView* View = MvvmExt->GetBlueprintView();
	if (!View)
	{
		// RequestExtension 只挂扩展，不建 View；首次编辑必须自己建一个
		MvvmExt->Modify();
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
		FBlueprintEditorUtils::MarkBlueprintAsModified(State->WBP);
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
