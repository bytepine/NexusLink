// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/CommonUI/NexusManageAssetCommonTextStyleCapability.h"
#if WITH_COMMON_UI
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPropertyUtils.h"
#include "CommonTextBlock.h"
#include "NexusMcpTool.h"

void FManageAssetCommonTextStyleCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_common_text_style");
	Out.SearchAssetTypes = {TEXT("CommonTextStyle")};
	Out.Description = TEXT("Batch edit CommonTextStyle. operations[].action=set_property.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("set_property") }))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("Property path")))
		.Prop(TEXT("value"), FNexusSchema::Str(TEXT("New property value")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("CommonTextStyle asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("commonui"), TEXT("text"), TEXT("style") };
	Out.RelatedCapabilities = {
		TEXT("get_asset_common_text_style"), TEXT("create_asset_common_text_style"),
		TEXT("manage_asset_common_button_style")
	};
}

struct FCommonTextStyleState
{
	UCommonTextStyle* Style = nullptr;
	bool bDirty = false;
};

static FCommonTextStyleState* CTSState(FNexusActionContext& Ctx)
{
	return static_cast<FCommonTextStyleState*>(Ctx.Target);
}

static void HandleCTS_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UCommonTextStyle* Style = CTSState(Ctx)->Style;
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
	CTSState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("propertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("oldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("newValue"), ActualVal);
}

bool FManageAssetCommonTextStyleCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UCommonTextStyle* Style = FNexusAssetUtils::LoadAssetWithFallback<UCommonTextStyle>(AssetPath);
	if (!Style)
	{
		OutError = FString::Printf(TEXT("Failed to load CommonTextStyle: %s"), *AssetPath);
		return false;
	}
	FCommonTextStyleState* State = new FCommonTextStyleState();
	State->Style = Style;
	OutTarget = State;
	return true;
}

void FManageAssetCommonTextStyleCapability::FinalizeTarget(void* Target) const
{
	FCommonTextStyleState* State = static_cast<FCommonTextStyleState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Style) State->Style->MarkPackageDirty();
	delete State;
}

void FManageAssetCommonTextStyleCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_property"), &HandleCTS_SetProperty);
}

REGISTER_MCP_CAPABILITY(FManageAssetCommonTextStyleCapability)
#endif
