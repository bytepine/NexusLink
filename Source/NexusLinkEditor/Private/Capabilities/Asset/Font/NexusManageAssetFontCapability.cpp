// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Font/NexusManageAssetFontCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPropertyUtils.h"
#include "Engine/Font.h"
#include "NexusMcpTool.h"

void FManageAssetFontCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_font");
	Out.SearchAssetTypes = {TEXT("Font")};
	Out.Description = TEXT("Batch edit Font. action=set_property (ScalingFactor etc.).");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("set_property") }))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("propertypath (e.g. ScalingFactor)")))
		.Prop(TEXT("value"), FNexusSchema::Str(TEXT("New property value string")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Font asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("typeface"), TEXT("scale"), TEXT("ttf") };
	Out.RelatedCapabilities = { TEXT("get_asset_font"), TEXT("create_asset_font"), TEXT("reimport_asset") };
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
	Op->TryGetStringField(TEXT("propertyPath"), PropPath);
	Op->TryGetStringField(TEXT("value"), Value);
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_property requires propertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Font, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("error"), Err);
		return;
	}
	FontState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("propertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("oldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("newValue"), ActualVal);
}

bool FManageAssetFontCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
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

void FManageAssetFontCapability::FinalizeTarget(void* Target) const
{
	FFontActionState* State = static_cast<FFontActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Font) State->Font->MarkPackageDirty();
	delete State;
}

void FManageAssetFontCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_property"), &HandleFont_SetProperty);
}

REGISTER_MCP_CAPABILITY(FManageAssetFontCapability)
