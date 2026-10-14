// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Texture/NexusManageAssetTextureCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPropertyUtils.h"
#include "Engine/Texture2D.h"
#include "NexusMcpTool.h"

void FNexusManageAssetTextureCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetTexture");
	Out.SearchAssetTypes = {TEXT("Texture2D")};
	Out.Description = TEXT("Batch edit Texture properties. Compression/bSRGB/LODGroup.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),       FNexusSchema::Enum(TEXT("Action"), { TEXT("SetProperty") }))
		.Prop(TEXT("PropertyPath"), FNexusSchema::Str(TEXT("propertypath (e.g. CompressionSettings/bSRGB/LODGroup)")))
		.Prop(TEXT("Value"),        FNexusSchema::Str(TEXT("New property value string")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("Texture asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch property ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("Texture"), TEXT("image"), TEXT("compression"), TEXT("srgb"), TEXT("lod") };
	Out.RelatedCapabilities = { TEXT("GetAssetTexture"), TEXT("SearchAsset") };
	Out.Prerequisites = { TEXT("EditorOnly") };
	Out.WhenToUse = TEXT("Edit Texture compression/bSRGB/LODGroup; persist with SaveAsset");
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
	Op->TryGetStringField(TEXT("PropertyPath"), PropPath);
	Op->TryGetStringField(TEXT("Value"), Value);
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetProperty requires PropertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Texture, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), Err);
		return;
	}
	TexState(Ctx)->bDirty = true;
	// UpdateMips 仅 UE5+ 可用；UE4 通过编辑器重新导入刷新
	Ctx.Entry->SetStringField(TEXT("PropertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("OldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("NewValue"), ActualVal);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset; compression changes need reimport"));
}

bool FNexusManageAssetTextureCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetTextureCapability::FinalizeTarget(void* Target) const
{
	FTextureActionState* State = static_cast<FTextureActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Texture) State->Texture->MarkPackageDirty();
	delete State;
}

void FNexusManageAssetTextureCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetProperty"), &HandleTex_SetProperty);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetTextureCapability)
