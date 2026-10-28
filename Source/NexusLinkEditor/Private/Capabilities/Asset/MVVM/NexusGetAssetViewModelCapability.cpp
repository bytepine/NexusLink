// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MVVM/NexusGetAssetViewModelCapability.h"

#if WITH_MVVM

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusAssetEditorUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpTool.h"
#if WITH_EDITOR
#include "Editor.h"
#include "MVVMEditorSubsystem.h"
#include "MVVMBlueprintViewConversionFunction.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintExtension.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "MVVMBlueprintView.h"
#include "MVVMBlueprintViewBinding.h"
#include "MVVMBlueprintViewModelContext.h"
#include "MVVMPropertyPath.h"
#include "Types/MVVMBindingMode.h"
#endif // WITH_EDITOR

// ── 辅助：BindingMode → 字符串 ───────────────────────────────────────────────

#if WITH_EDITOR
static FString BindingModeToStr(EMVVMBindingMode Mode)
{
	switch (Mode)
	{
		case EMVVMBindingMode::OneWayToDestination: return TEXT("OneWayToDestination");
		case EMVVMBindingMode::TwoWay:              return TEXT("TwoWay");
		case EMVVMBindingMode::OneWayToSource:      return TEXT("OneWayToSource");
		case EMVVMBindingMode::OneTimeToDestination:return TEXT("OneTimeToDestination");
		case EMVVMBindingMode::OneTimeToSource:     return TEXT("OneTimeToSource");
		default:                                    return TEXT("Unknown");
	}
}

// ── 辅助：CreationType → 字符串 ──────────────────────────────────────────────

static FString CreationTypeToStr(EMVVMBlueprintViewModelContextCreationType Type)
{
	switch (Type)
	{
		case EMVVMBlueprintViewModelContextCreationType::Manual:                  return TEXT("Manual");
		case EMVVMBlueprintViewModelContextCreationType::CreateInstance:          return TEXT("CreateInstance");
		case EMVVMBlueprintViewModelContextCreationType::GlobalViewModelCollection: return TEXT("GlobalViewModelCollection");
		case EMVVMBlueprintViewModelContextCreationType::PropertyPath:            return TEXT("PropertyPath");
		case EMVVMBlueprintViewModelContextCreationType::Resolver:                return TEXT("Resolver");
		default:                                                                  return TEXT("Unknown");
	}
}

// ── 辅助：FMVVMBlueprintPropertyPath → 简洁字符串（不需要 SelfContext）───────

static FString PropertyPathToString(const FMVVMBlueprintPropertyPath& Path)
{
	// 将原始 fieldPath 段名用 "." 拼接
	TArray<FString> Parts;
	if (!Path.GetWidgetName().IsNone())
	{
		Parts.Add(Path.GetWidgetName().ToString());
	}
	for (const FMVVMBlueprintFieldPath& FieldPath : Path.GetFieldPaths())
	{
		FName RawName = FieldPath.GetRawFieldName();
		if (!RawName.IsNone())
		{
			Parts.Add(RawName.ToString());
		}
	}
	return FString::Join(Parts, TEXT("."));
}

static FString BindingContextToString(const FMVVMBlueprintPropertyPath& Path, const UMVVMBlueprintView* View)
{
	if (Path.GetViewModelId().IsValid())
	{
		const FMVVMBlueprintViewModelContext* Model = View->FindViewModel(Path.GetViewModelId());
		return Model ? TEXT("ViewModel:") + Model->GetViewModelName().ToString() : TEXT("MissingViewModel:") + Path.GetViewModelId().ToString();
	}
	if (!Path.GetWidgetName().IsNone()) return TEXT("Widget:") + Path.GetWidgetName().ToString();
	return Path.HasPaths() ? TEXT("Self") : FString();
}

static FString BindingConversionToString(const UMVVMBlueprintViewConversionFunction* Conversion, const UWidgetBlueprint* Blueprint)
{
	if (!Conversion) return FString();
	const FMVVMBlueprintFunctionReference Reference = Conversion->GetConversionFunction();
	const UFunction* Function = Reference.GetFunction(Blueprint);
	return Function ? Function->GetPathName() : Reference.ToString();
}
#endif // WITH_EDITOR

// ── Capability ────────────────────────────────────────────────────────────────

void FNexusGetAssetViewModelCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetViewModel");
	Out.Description = TEXT("Inspect WBP data bindings. Contexts, paths, directions and optional unary conversion discovery; UE 5.5+.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Widget Blueprint asset path (/Game/…/WBP_Foo)")))
		.Prop(TEXT("bIncludeConversionFunctions"), FNexusSchema::Bool(TEXT("List available unary UFunctions (bounded)"), true, false))
		.Prop(TEXT("ConversionFilter"), FNexusSchema::Str(TEXT("Conversion path substring filter")))
		.Prop(TEXT("ConversionLimit"), FNexusSchema::Int(TEXT("Maximum conversion functions to return"), 100, 1, 500))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Blueprint, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = {
		TEXT("mvvm"), TEXT("viewmodel"), TEXT("ViewModel"), TEXT("binding"), TEXT("Bindings"),
		TEXT("fieldnotify"), TEXT("umvm"), TEXT("UE5MVVM")
	};
	Out.RelatedCapabilities = { TEXT("GetAssetUserWidget"), TEXT("ManageAssetUserWidget"), TEXT("SearchAsset"), TEXT("GetAssetBlueprint"), TEXT("ManageAssetViewModel") };
	Out.WhenToUse = TEXT("Read MVVM ViewModels and bindings on Widget BP (source<->target/direction/conversion)");
}

FCapabilityResult FNexusGetAssetViewModelCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		const FString AssetPath = A.Str(TEXT("AssetPath"));

#if WITH_EDITOR
		UWidgetBlueprint* WBP = FNexusAssetEditorUtils::LoadWidgetBP(AssetPath);
		if (!WBP)
		{
			OutError = FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath);
			return;
		}

		OutEntry->SetStringField(TEXT("AssetType"), TEXT("WidgetBlueprint"));
		OutEntry->SetStringField(TEXT("name"), WBP->GetName());
		OutEntry->SetStringField(TEXT("Path"), FNexusAssetUtils::PackagePathOf(WBP));

		if (A.Bool(TEXT("bIncludeConversionFunctions")))
		{
			UMVVMEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>() : nullptr;
			if (!Subsystem) { OutError = TEXT("MVVM editor subsystem unavailable"); return; }
			const FString Filter = A.Str(TEXT("ConversionFilter"));
			const int32 Limit = FMath::Clamp(static_cast<int32>(A.Num(TEXT("ConversionLimit"), 100)), 1, 500);
			TArray<FString> Paths;
			for (const UE::MVVM::FConversionFunctionValue& Candidate : Subsystem->GetConversionFunctions(WBP, nullptr, nullptr))
			{
				const UFunction* Function = Candidate.GetFunction();
				if (Function && Subsystem->IsSimpleConversionFunction(Function) && (Filter.IsEmpty() || Function->GetPathName().Contains(Filter)))
					Paths.AddUnique(Function->GetPathName());
			}
			Paths.Sort();
			TArray<TSharedPtr<FJsonValue>> Functions;
			for (int32 FunctionIndex = 0; FunctionIndex < FMath::Min(Limit, Paths.Num()); ++FunctionIndex)
				Functions.Add(MakeShared<FJsonValueString>(Paths[FunctionIndex]));
			OutEntry->SetArrayField(TEXT("ConversionFunctions"), Functions);
			OutEntry->SetNumberField(TEXT("ConversionFunctionsCount"), Paths.Num());
			OutEntry->SetBoolField(TEXT("bConversionsTruncated"), Paths.Num() > Limit);
		}


		// 从 WBP 扩展中取 MVVM 视图对象
		UMVVMWidgetBlueprintExtension_View* MvvmExt =
			UWidgetBlueprintExtension::GetExtension<UMVVMWidgetBlueprintExtension_View>(WBP);

		if (!MvvmExt)
		{
			OutEntry->SetStringField(TEXT("Note"), TEXT("Widget Blueprint has no MVVM (missing MVVMWidgetBlueprintExtension_View)"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		const UMVVMBlueprintView* View = MvvmExt->GetBlueprintView();
		if (!View)
		{
			OutEntry->SetStringField(TEXT("Note"), TEXT("MVVM extension exists but BlueprintView is empty"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		// ── ViewModel 列表 ─────────────────────────────────────────────────────
		TArray<TSharedPtr<FJsonValue>> VmArray;
		for (const FMVVMBlueprintViewModelContext& Ctx : View->GetViewModels())
		{
			TSharedPtr<FJsonObject> VmObj = MakeShared<FJsonObject>();
			VmObj->SetStringField(TEXT("name"), Ctx.ViewModelName.ToString());
			VmObj->SetStringField(TEXT("Id"), Ctx.GetViewModelId().ToString());
			VmObj->SetStringField(TEXT("CreationType"), CreationTypeToStr(Ctx.CreationType));
			if (Ctx.NotifyFieldValueClass)
			{
				VmObj->SetStringField(TEXT("Class"), Ctx.NotifyFieldValueClass->GetName());
				VmObj->SetStringField(TEXT("ClassPath"), Ctx.NotifyFieldValueClass->GetPathName());
			}
			VmObj->SetBoolField(TEXT("bOptional"), Ctx.bOptional);
			VmObj->SetBoolField(TEXT("bHasPublicSetter"), Ctx.bCreateSetterFunction);
			VmArray.Add(MakeShared<FJsonValueObject>(VmObj));
		}
		OutEntry->SetNumberField(TEXT("ViewModelsCount"), VmArray.Num());
		OutEntry->SetArrayField(TEXT("ViewModels"), VmArray);

		// ── Bindings 列表 ──────────────────────────────────────────────────────
		TArray<TSharedPtr<FJsonValue>> BindArr;
		for (int32 i = 0; i < View->GetNumBindings(); ++i)
		{
			const FMVVMBlueprintViewBinding* B = View->GetBindingAt(i);
			if (!B)
			{
				continue;
			}
			TSharedPtr<FJsonObject> BObj = MakeShared<FJsonObject>();
			BObj->SetStringField(TEXT("Id"), B->BindingId.ToString());
			BObj->SetNumberField(TEXT("BindingIndex"), i);
			BObj->SetStringField(TEXT("SourceContext"), BindingContextToString(B->SourcePath, View));
			BObj->SetStringField(TEXT("DestinationContext"), BindingContextToString(B->DestinationPath, View));
			// 保留旧 SourcePath/DestinationPath 文本，另返回可直接用于 AddBinding 的纯字段路径。
			BObj->SetStringField(TEXT("SourcePropertyPath"), B->SourcePath.GetPropertyPath(WBP->GeneratedClass));
			BObj->SetStringField(TEXT("DestinationPropertyPath"), B->DestinationPath.GetPropertyPath(WBP->GeneratedClass));
			BObj->SetStringField(TEXT("ConversionFunction"), BindingConversionToString(B->Conversion.SourceToDestinationConversion, WBP));
			BObj->SetStringField(TEXT("ReverseConversionFunction"), BindingConversionToString(B->Conversion.DestinationToSourceConversion, WBP));
			BObj->SetStringField(TEXT("SourcePath"), PropertyPathToString(B->SourcePath));
			BObj->SetStringField(TEXT("DestinationPath"), PropertyPathToString(B->DestinationPath));
			BObj->SetStringField(TEXT("BindingType"), BindingModeToStr(B->BindingType));
			BObj->SetBoolField(TEXT("bEnabled"), B->bEnabled);
			BObj->SetBoolField(TEXT("bCompile"), B->bCompile);
			BindArr.Add(MakeShared<FJsonValueObject>(BObj));
		}
		OutEntry->SetNumberField(TEXT("BindingsCount"), BindArr.Num());
		OutEntry->SetArrayField(TEXT("Bindings"), BindArr);


#if NX_UE_HAS_MVVM_EVENTS_CONDITIONS
		// UE 5.5+：Events 与 Conditions
		int32 EventCount = 0;
		int32 ConditionCount = 0;
		for (const TObjectPtr<UMVVMBlueprintViewEvent>& Ev : View->GetEvents())
		{
			if (Ev)
			{
				++EventCount;
			}
		}
		for (const TObjectPtr<UMVVMBlueprintViewCondition>& Cond : View->GetConditions())
		{
			if (Cond)
			{
				++ConditionCount;
			}
		}
		if (EventCount > 0)
		{
			OutEntry->SetNumberField(TEXT("EventsCount"), EventCount);
		}
		if (ConditionCount > 0)
		{
			OutEntry->SetNumberField(TEXT("ConditionsCount"), ConditionCount);
		}
#endif // NX_UE_HAS_MVVM_EVENTS_CONDITIONS

#else
		OutEntry->SetStringField(TEXT("Note"), TEXT("MVVM data only available in WITH_EDITOR builds"));
#endif // WITH_EDITOR

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetViewModelCapability)

#endif // WITH_MVVM
