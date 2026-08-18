// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Texture/NexusManageAssetTextureCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPropertyUtils.h"
#include "Engine/Texture2D.h"
#include "NexusMcpTool.h"

void FManageAssetTextureCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_texture");
	Out.SearchAssetTypes = {TEXT("Texture2D")};
	Out.Description = TEXT("Batch edit Texture properties. Compression/sRGB/LODGroup.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("Action"), { TEXT("set_property") }))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("propertypath (e.g. CompressionSettings/sRGB/LODGroup)")))
		.Prop(TEXT("value"),        FNexusSchema::Str(TEXT("New property value string")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("Texture asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch property ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("texture"), TEXT("image"), TEXT("compression"), TEXT("srgb"), TEXT("lod") };
	Out.RelatedCapabilities = { TEXT("get_asset_texture"), TEXT("search_asset") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("Edit Texture compression/sRGB/LODGroup; persist with save_asset");
}

struct FTextureActionState
{
	UTexture* Texture = nullptr;
	bool bDirty = false;
};

static FTextureActionState* TexState(FNexusActionContext& Ctx)
{
	return static_cast<FTextureActionState*>(Ctx.Target);
}

static void HandleTex_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UTexture* Texture = TexState(Ctx)->Texture;
	FString PropPath, Value;
	Op->TryGetStringField(TEXT("propertyPath"), PropPath);
	Op->TryGetStringField(TEXT("value"), Value);
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_property requires propertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Texture, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("error"), Err);
		return;
	}
	TexState(Ctx)->bDirty = true;
	// UpdateMips 仅 UE5+ 可用；UE4 通过编辑器重新导入刷新
	Ctx.Entry->SetStringField(TEXT("propertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("oldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("newValue"), ActualVal);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset; compression changes need reimport"));
}

bool FManageAssetTextureCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UTexture* Texture = FNexusAssetUtils::LoadAssetWithFallback<UTexture>(AssetPath);
	if (!Texture)
	{
		OutError = FString::Printf(TEXT("Texture not found: %s"), *AssetPath);
		return false;
	}
	FTextureActionState* State = new FTextureActionState();
	State->Texture = Texture;
	OutTarget = State;
	return true;
}

void FManageAssetTextureCapability::FinalizeTarget(void* Target) const
{
	FTextureActionState* State = static_cast<FTextureActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Texture) State->Texture->MarkPackageDirty();
	delete State;
}

void FManageAssetTextureCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_property"), &HandleTex_SetProperty);
}

REGISTER_MCP_CAPABILITY(FManageAssetTextureCapability)
