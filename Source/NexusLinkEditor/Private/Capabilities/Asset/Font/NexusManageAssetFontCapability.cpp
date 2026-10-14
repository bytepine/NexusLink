// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Font/NexusManageAssetFontCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPropertyUtils.h"
#include "Engine/Font.h"
#include "NexusMcpTool.h"

void FNexusManageAssetFontCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetFont");
	Out.SearchAssetTypes = {TEXT("Font")};
	Out.Description = TEXT("Batch edit Font. Action=SetProperty (ScalingFactor etc.).");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("SetProperty") }))
		.Prop(TEXT("PropertyPath"), FNexusSchema::Str(TEXT("propertypath (e.g. ScalingFactor)")))
		.Prop(TEXT("Value"), FNexusSchema::Str(TEXT("New property value string")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Font asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("typeface"), TEXT("Scale"), TEXT("ttf") };
	Out.RelatedCapabilities = { TEXT("GetAssetFont"), TEXT("CreateAssetFont"), TEXT("ReimportAsset") };
}

struct FFontActionState
{
	UFont* Font = nullptr;
	bool bDirty = false;
};

static FFontActionState* FontState(FNexusActionContext& Ctx)
{
	return static_cast<FFontActionState*>(Ctx.Target);
}

static void HandleFont_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UFont* Font = FontState(Ctx)->Font;
	FString PropPath, Value;
	Op->TryGetStringField(TEXT("PropertyPath"), PropPath);
	Op->TryGetStringField(TEXT("Value"), Value);
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetProperty requires PropertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Font, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), Err);
		return;
	}
	FontState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("PropertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("OldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("NewValue"), ActualVal);
}

bool FNexusManageAssetFontCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UFont* Font = FNexusAssetUtils::LoadAssetWithFallback<UFont>(AssetPath);
	if (!Font)
	{
		OutError = FString::Printf(TEXT("Failed to load Font: %s"), *AssetPath);
		return false;
	}
	FFontActionState* State = new FFontActionState();
	State->Font = Font;
	OutTarget = State;
	return true;
}

void FNexusManageAssetFontCapability::FinalizeTarget(void* Target) const
{
	FFontActionState* State = static_cast<FFontActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Font) State->Font->MarkPackageDirty();
	delete State;
}

void FNexusManageAssetFontCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetProperty"), &HandleFont_SetProperty);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetFontCapability)
