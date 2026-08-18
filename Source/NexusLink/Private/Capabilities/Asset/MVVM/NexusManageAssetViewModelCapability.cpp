// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MVVM/NexusManageAssetViewModelCapability.h"

#if WITH_MVVM

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusJsonUtils.h"
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

FCapabilityResult FManageAssetViewModelCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
#if !WITH_EDITOR
		OutError = TEXT("manage_asset_view_model only available in editor builds");
		return;
#else
		const FString AssetPath = A.Str(TEXT("assetPath"));
		UWidgetBlueprint* WBP = FNexusAssetUtils::LoadWidgetBP(AssetPath);
		if (!WBP) { OutError = FString::Printf(TEXT("WidgetBlueprint not found: %s"), *AssetPath); return; }

		UMVVMWidgetBlueprintExtension_View* MvvmExt = UMVVMWidgetBlueprintExtension_View::Request(WBP);
		if (!MvvmExt) { OutError = TEXT("Unable to get MVVM extension"); return; }
		UMVVMBlueprintView* View = MvvmExt->GetBlueprintView();
		if (!View) { OutError = TEXT("BlueprintView is empty"); return; }

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("operations is a required array");
			return;
		}

		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			TSharedPtr<FJsonObject> Op = OpVal->AsObject();
			if (!Op.IsValid()) continue;
			TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);
			Res->SetStringField(TEXT("action"), Action);

			if (Action == TEXT("add_view_model"))
			{
				FString VmName, VmClassName;
				Op->TryGetStringField(TEXT("viewModelName"), VmName);
				Op->TryGetStringField(TEXT("viewModelClass"), VmClassName);
				if (VmName.IsEmpty() || VmClassName.IsEmpty())
				{
					Res->SetStringField(TEXT("error"), TEXT("add_view_model requires viewModelName and viewModelClass"));
				}
				else
				{
					UClass* VmClass = FNexusAssetUtils::FindClassWithUPrefix(VmClassName);
					if (!VmClass)
					{
						Res->SetStringField(TEXT("error"), FString::Printf(TEXT("ViewModel class not found: %s"), *VmClassName));
					}
					else
					{
						FMVVMBlueprintViewModelContext Ctx;
						Ctx.ViewModelName = FName(*VmName);
						Ctx.NotifyFieldValueClass = VmClass;
						View->AddViewModel(Ctx);
						Res->SetStringField(TEXT("viewModelName"), VmName);
						bDirty = true;
					}
				}
			}
			else if (Action == TEXT("remove_view_model"))
			{
				FString VmName;
				Op->TryGetStringField(TEXT("viewModelName"), VmName);
				bool bRemoved = false;
				const TArray<FMVVMBlueprintViewModelContext> Vms = View->GetViewModels();
				for (const FMVVMBlueprintViewModelContext& Ctx : Vms)
				{
					if (Ctx.ViewModelName.ToString().Equals(VmName, ESearchCase::IgnoreCase))
					{
						View->RemoveViewModel(Ctx.ViewModelContextId);
						bRemoved = true;
						break;
					}
				}
				if (!bRemoved) Res->SetStringField(TEXT("error"), TEXT("ViewModel not found"));
				else bDirty = true;
			}
			else if (Action == TEXT("add_binding"))
			{
				View->AddDefaultBinding();
				Res->SetNumberField(TEXT("bindingsCount"), View->GetNumBindings());
				bDirty = true;
			}
			else if (Action == TEXT("remove_binding"))
			{
				int32 Idx = 0;
				if (Op->HasField(TEXT("bindingIndex"))) Idx = static_cast<int32>(Op->GetNumberField(TEXT("bindingIndex")));
				if (Idx < 0 || Idx >= View->GetNumBindings())
				{
					Res->SetStringField(TEXT("error"), TEXT("bindingIndex out of bounds"));
				}
				else
				{
					View->RemoveBindingAt(Idx);
					bDirty = true;
				}
			}
			else
			{
				Res->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: %s"), *Action));
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(Res));
		}
		if (bDirty)
		{
			WBP->MarkPackageDirty();
			OutTop->SetStringField(TEXT("hint"), TEXT("Call save_asset to persist changes"));
		}
#endif
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetViewModelCapability)

#endif
