// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/CommonUI/NexusManageAssetCommonButtonStyleCapability.h"
#if WITH_COMMON_UI
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "CommonButtonBase.h"
#include "NexusMcpTool.h"

void FManageAssetCommonButtonStyleCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_common_button_style");
	Out.SearchAssetTypes = {TEXT("CommonButtonStyle")};
	Out.Description = TEXT("批量编辑 CommonButtonStyle。operations[].action=set_property。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("操作"), { TEXT("set_property") }))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("属性路径")))
		.Prop(TEXT("value"), FNexusSchema::Str(TEXT("属性新值")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("CommonButtonStyle 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("commonui"), TEXT("button"), TEXT("style") };
	Out.RelatedCapabilities = {
		TEXT("get_asset_common_button_style"), TEXT("create_asset_common_button_style"),
		TEXT("manage_asset_common_text_style")
	};
}

FCapabilityResult FManageAssetCommonButtonStyleCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;
		UCommonButtonStyle* Style = FNexusAssetUtils::LoadAssetWithFallback<UCommonButtonStyle>(AssetPath);
		if (!Style)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("加载 CommonButtonStyle 失败: %s"), *AssetPath));
			return;
		}
		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("缺少 operations 或为空"));
			return;
		}
		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			const TSharedPtr<FJsonObject>* OpPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpPtr) || !OpPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpPtr;
			FString Action, PropPath, Value;
			Op->TryGetStringField(TEXT("action"), Action);
			Op->TryGetStringField(TEXT("propertyPath"), PropPath);
			Op->TryGetStringField(TEXT("value"), Value);
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), AssetPath);
			Entry->SetStringField(TEXT("action"), Action);
			if (!Action.Equals(TEXT("set_property"), ESearchCase::IgnoreCase))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			if (PropPath.IsEmpty() || Value.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_property 需要 propertyPath 和 value"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			FString OldVal, ActualVal, Err;
			if (!FNexusPropertyUtils::WritePropertyAndEcho(Style, { PropPath }, 0, Value, OldVal, ActualVal, Err))
			{
				Entry->SetStringField(TEXT("error"), Err);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			bDirty = true;
			Entry->SetStringField(TEXT("propertyPath"), PropPath);
			if (!OldVal.IsEmpty()) Entry->SetStringField(TEXT("oldValue"), OldVal);
			if (!ActualVal.IsEmpty()) Entry->SetStringField(TEXT("newValue"), ActualVal);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		if (bDirty) Style->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetCommonButtonStyleCapability)
#endif
