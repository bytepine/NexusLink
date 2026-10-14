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

void FNexusManageAssetCommonTextStyleCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetCommonTextStyle");
	Out.SearchAssetTypes = {TEXT("CommonTextStyle")};
	Out.Description = TEXT("Batch edit CommonTextStyle. operations[].Action=SetProperty.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("SetProperty") }))
		.Prop(TEXT("PropertyPath"), FNexusSchema::Str(TEXT("Property path")))
		.Prop(TEXT("Value"), FNexusSchema::Str(TEXT("New property value")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("CommonTextStyle asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("commonui"), TEXT("text"), TEXT("style") };
	Out.RelatedCapabilities = {
		TEXT("GetAssetCommonTextStyle"), TEXT("CreateAssetCommonTextStyle"),
		TEXT("ManageAssetCommonButtonStyle")
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
	Op->TryGetStringField(TEXT("PropertyPath"), PropPath);
	Op->TryGetStringField(TEXT("Value"), Value);
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetProperty requires PropertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Style, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), Err);
		return;
	}
	CTSState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("PropertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("OldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("NewValue"), ActualVal);
}

bool FNexusManageAssetCommonTextStyleCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UCommonTextStyle* Style = FNexusAssetUtils::LoadAssetOrBlueprintCDO<UCommonTextStyle>(AssetPath);
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

void FNexusManageAssetCommonTextStyleCapability::FinalizeTarget(void* Target) const
{
	FCommonTextStyleState* State = static_cast<FCommonTextStyleState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Style) State->Style->MarkPackageDirty();
	delete State;
}

void FNexusManageAssetCommonTextStyleCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetProperty"), &HandleCTS_SetProperty);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetCommonTextStyleCapability)
#endif
