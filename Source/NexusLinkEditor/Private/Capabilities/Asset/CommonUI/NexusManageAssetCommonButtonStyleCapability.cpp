// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/CommonUI/NexusManageAssetCommonButtonStyleCapability.h"
#if WITH_COMMON_UI
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusAssetEditUtils.h"
#include "CommonButtonBase.h"
#include "NexusMcpTool.h"

void FNexusManageAssetCommonButtonStyleCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetCommonButtonStyle");
	Out.SearchAssetTypes = {TEXT("CommonButtonStyle")};
	Out.Description = TEXT("Batch edit CommonButtonStyle. operations[].Action=SetProperty.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("SetProperty") }))
		.Prop(TEXT("PropertyPath"), FNexusSchema::Str(TEXT("Property path")))
		.Prop(TEXT("Value"), FNexusSchema::Str(TEXT("New property value")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("CommonButtonStyle asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Widget };
	Out.ExtraSearchKeywords = { TEXT("commonui"), TEXT("button"), TEXT("style") };
	Out.RelatedCapabilities = {
		TEXT("GetAssetCommonButtonStyle"), TEXT("CreateAssetCommonButtonStyle"),
		TEXT("ManageAssetCommonTextStyle")
	};
}

struct FCommonBtnStyleState
{
	UCommonButtonStyle* Style = nullptr;
	bool bDirty = false;
};

static void HandleCBS_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	auto* State = static_cast<FCommonBtnStyleState*>(Ctx.Target);
	if (FNexusAssetEditUtils::SetProperty(State->Style, Op, *Ctx.Entry))
	{
		State->bDirty = true;
	}
}

bool FNexusManageAssetCommonButtonStyleCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UCommonButtonStyle* Style = FNexusAssetUtils::LoadAssetOrBlueprintCDO<UCommonButtonStyle>(AssetPath);
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

void FNexusManageAssetCommonButtonStyleCapability::FinalizeTarget(void* Target) const
{
	FCommonBtnStyleState* State = static_cast<FCommonBtnStyleState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Style) State->Style->MarkPackageDirty();
	delete State;
}

void FNexusManageAssetCommonButtonStyleCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetProperty"), &HandleCBS_SetProperty);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetCommonButtonStyleCapability)
#endif
