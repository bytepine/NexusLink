// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MVVM/NexusGetAssetViewModelCapability.h"

#if WITH_MVVM

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpTool.h"
#if WITH_EDITOR
#include "WidgetBlueprint.h"
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
#endif // WITH_EDITOR

// ── Capability ────────────────────────────────────────────────────────────────

void FGetAssetViewModelCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_view_model");
	Out.Description = TEXT("检查 WBP 上的 MVVM ViewModel 列表与 Binding 快照。只读。UE 5.5+。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Widget 蓝图资产路径（/Game/…/WBP_Foo）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Blueprint, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = {
		TEXT("mvvm"), TEXT("viewmodel"), TEXT("view_model"), TEXT("binding"), TEXT("bindings"),
		TEXT("fieldnotify"), TEXT("umvm"), TEXT("ue5_mvvm")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_user_widget"), TEXT("manage_asset_user_widget"), TEXT("search_asset"), TEXT("get_asset_blueprint") };
	Out.WhenToUse = TEXT("读 Widget 蓝图上挂载的 MVVM ViewModel 列表、属性绑定（源↔目标/方向/转换）");
}

FCapabilityResult FGetAssetViewModelCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

#if WITH_EDITOR
		UWidgetBlueprint* WBP = FNexusAssetUtils::LoadWidgetBP(AssetPath);
		if (!WBP)
		{
			OutError = FString::Printf(TEXT("Widget 蓝图未找到: %s"), *AssetPath);
			return;
		}

		OutEntry->SetStringField(TEXT("assetType"), TEXT("WidgetBlueprint"));
		OutEntry->SetStringField(TEXT("name"), WBP->GetName());
		OutEntry->SetStringField(TEXT("path"), FNexusAssetUtils::PackagePathOf(WBP));

		// 从 WBP 扩展中取 MVVM 视图对象
		UMVVMWidgetBlueprintExtension_View* MvvmExt = nullptr;
		for (UWidgetBlueprintExtension* Ext : WBP->GetExtensions())
		{
			MvvmExt = Cast<UMVVMWidgetBlueprintExtension_View>(Ext);
			if (MvvmExt)
			{
				break;
			}
		}

		if (!MvvmExt)
		{
			OutEntry->SetStringField(TEXT("note"), TEXT("该 Widget 蓝图未启用 MVVM（无 MVVMWidgetBlueprintExtension_View）"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		const UMVVMBlueprintView* View = MvvmExt->GetBlueprintView();
		if (!View)
		{
			OutEntry->SetStringField(TEXT("note"), TEXT("MVVM 扩展存在但 BlueprintView 为空"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		// ── ViewModel 列表 ─────────────────────────────────────────────────────
		TArray<TSharedPtr<FJsonValue>> VmArray;
		for (const FMVVMBlueprintViewModelContext& Ctx : View->GetViewModels())
		{
			TSharedPtr<FJsonObject> VmObj = MakeShared<FJsonObject>();
			VmObj->SetStringField(TEXT("name"), Ctx.ViewModelName.ToString());
			VmObj->SetStringField(TEXT("creationType"), CreationTypeToStr(Ctx.CreationType));
			if (Ctx.NotifyFieldValueClass)
			{
				VmObj->SetStringField(TEXT("class"), Ctx.NotifyFieldValueClass->GetName());
			}
			VmObj->SetBoolField(TEXT("optional"), Ctx.bOptional);
			VmObj->SetBoolField(TEXT("hasPublicSetter"), Ctx.bCreateSetterFunction);
			VmArray.Add(MakeShared<FJsonValueObject>(VmObj));
		}
		OutEntry->SetNumberField(TEXT("viewModelsCount"), VmArray.Num());
		OutEntry->SetArrayField(TEXT("viewModels"), VmArray);

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
			BObj->SetStringField(TEXT("id"), B->BindingId.ToString());
			BObj->SetStringField(TEXT("sourcePath"), PropertyPathToString(B->SourcePath));
			BObj->SetStringField(TEXT("destinationPath"), PropertyPathToString(B->DestinationPath));
			BObj->SetStringField(TEXT("bindingType"), BindingModeToStr(B->BindingType));
			BObj->SetBoolField(TEXT("enabled"), B->bEnabled);
			BObj->SetBoolField(TEXT("compile"), B->bCompile);
			BindArr.Add(MakeShared<FJsonValueObject>(BObj));
		}
		OutEntry->SetNumberField(TEXT("bindingsCount"), BindArr.Num());
		OutEntry->SetArrayField(TEXT("bindings"), BindArr);

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
			OutEntry->SetNumberField(TEXT("eventsCount"), EventCount);
		}
		if (ConditionCount > 0)
		{
			OutEntry->SetNumberField(TEXT("conditionsCount"), ConditionCount);
		}
#endif // NX_UE_HAS_MVVM_EVENTS_CONDITIONS

#else
		OutEntry->SetStringField(TEXT("note"), TEXT("MVVM 数据仅在 WITH_EDITOR 构建下可用"));
#endif // WITH_EDITOR

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetViewModelCapability)

#endif // WITH_MVVM
