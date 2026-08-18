// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/CommonUI/NexusManageAssetCommonButtonStyleCapability.h"
#if WITH_COMMON_UI
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPropertyUtils.h"
#include "CommonButtonBase.h"
#include "NexusMcpTool.h"

void FManageAssetCommonButtonStyleCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_common_button_style");
	Out.SearchAssetTypes = {TEXT("CommonButtonStyle")};
	Out.Description = TEXT("Batch edit CommonButtonStyle. operations[].action=set_property.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("set_property") }))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("Property path")))
		.Prop(TEXT("value"), FNexusSchema::Str(TEXT("New property value")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("CommonButtonStyle asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("commonui"), TEXT("button"), TEXT("style") };
	Out.RelatedCapabilities = {
		TEXT("get_asset_common_button_style"), TEXT("create_asset_common_button_style"),
		TEXT("manage_asset_common_text_style")
	};
}

struct FCommonBtnStyleState
{
	UCommonButtonStyle* Style = nullptr;
	bool bDirty = false;
};

static FCommonBtnStyleState* CBSState(FNexusActionContext& Ctx)
{
	return static_cast<FCommonBtnStyleState*>(Ctx.Target);
}

static void HandleCBS_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UCommonButtonStyle* Style = CBSState(Ctx)->Style;
	FString PropPath, Value;
	Op->TryGetStringField(TEXT("propertyPath"), PropPath);
	Op->TryGetStringField(TEXT("value"), Value);
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_property requires propertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Style, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("error"), Err);
		return;
	}
	CBSState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("propertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("oldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("newValue"), ActualVal);
}

bool FManageAssetCommonButtonStyleCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UCommonButtonStyle* Style = FNexusAssetUtils::LoadAssetWithFallback<UCommonButtonStyle>(AssetPath);
	if (!Style)
	{
		OutError = FString::Printf(TEXT("Failed to load CommonButtonStyle: %s"), *AssetPath);
		return false;
	}
	FCommonBtnStyleState* State = new FCommonBtnStyleState();
	State->Style = Style;
	OutTarget = State;
	return true;
}

void FManageAssetCommonButtonStyleCapability::FinalizeTarget(void* Target) const
{
	FCommonBtnStyleState* State = static_cast<FCommonBtnStyleState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Style) State->Style->MarkPackageDirty();
	delete State;
}

void FManageAssetCommonButtonStyleCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_property"), &HandleCBS_SetProperty);
}

REGISTER_MCP_CAPABILITY(FManageAssetCommonButtonStyleCapability)
#endif
