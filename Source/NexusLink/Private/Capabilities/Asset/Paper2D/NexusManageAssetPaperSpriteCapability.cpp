// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusManageAssetPaperSpriteCapability.h"
#if WITH_PAPER2D
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "PaperSprite.h"
#include "Engine/Texture2D.h"
#include "NexusMcpTool.h"

void FManageAssetPaperSpriteCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_paper_sprite");
	Out.SearchAssetTypes = {TEXT("PaperSprite")};
	Out.Description = TEXT("Batch edit PaperSprite. action=set_source/set_pivot.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("set_source"), TEXT("set_pivot") }))
		.Prop(TEXT("sourceTexturePath"), FNexusSchema::Str(TEXT("Source Texture2D (set_source)")))
		.Prop(TEXT("pivotX"), FNexusSchema::Num(TEXT("Pivot X (set_pivot)")))
		.Prop(TEXT("pivotY"), FNexusSchema::Num(TEXT("Pivot Y (set_pivot)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("PaperSprite asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("sprite"), TEXT("pivot"), TEXT("source") };
	Out.RelatedCapabilities = { TEXT("get_asset_paper_sprite"), TEXT("create_asset_paper_sprite") };
}

struct FPaperSpriteActionState
{
	UPaperSprite* Sprite = nullptr;
	bool bDirty = false;
};

static FPaperSpriteActionState* SpriteState(FNexusActionContext& Ctx)
{
	return static_cast<FPaperSpriteActionState*>(Ctx.Target);
}

static UPaperSprite* SpriteFrom(FNexusActionContext& Ctx)
{
	FPaperSpriteActionState* S = SpriteState(Ctx);
	return S ? S->Sprite : nullptr;
}

static void MarkSpriteDirty(FNexusActionContext& Ctx)
{
	if (FPaperSpriteActionState* S = SpriteState(Ctx))
	{
		S->bDirty = true;
	}
}

static void HandleSprite_SetSource(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperSprite* Sprite = SpriteFrom(Ctx);
	FString TexPath;
	if (!Op->TryGetStringField(TEXT("sourceTexturePath"), TexPath) || TexPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_source requires sourceTexturePath"));
		return;
	}
	UTexture2D* Tex = FNexusAssetUtils::LoadAssetWithFallback<UTexture2D>(TexPath);
	if (!Tex)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Texture2D not found: %s"), *TexPath));
		return;
	}
	Sprite->SetSourceTexture(Tex);
	MarkSpriteDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("sourceTexture"), Tex->GetPathName());
}

static void HandleSprite_SetPivot(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperSprite* Sprite = SpriteFrom(Ctx);
	FVector2D Pivot = Sprite->GetPivotPosition();
	if (Op->HasField(TEXT("pivotX"))) Pivot.X = static_cast<float>(Op->GetNumberField(TEXT("pivotX")));
	if (Op->HasField(TEXT("pivotY"))) Pivot.Y = static_cast<float>(Op->GetNumberField(TEXT("pivotY")));
	Sprite->SetPivotPosition(Pivot);
	MarkSpriteDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("pivotX"), Pivot.X);
	Ctx.Entry->SetNumberField(TEXT("pivotY"), Pivot.Y);
}

bool FManageAssetPaperSpriteCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UPaperSprite* Sprite = FNexusAssetUtils::LoadAssetWithFallback<UPaperSprite>(AssetPath);
	if (!Sprite)
	{
		OutError = FString::Printf(TEXT("Failed to load PaperSprite: %s"), *AssetPath);
		return false;
	}
	FPaperSpriteActionState* State = new FPaperSpriteActionState();
	State->Sprite = Sprite;
	OutTarget = State;
	return true;
}

void FManageAssetPaperSpriteCapability::FinalizeTarget(void* Target) const
{
	FPaperSpriteActionState* State = static_cast<FPaperSpriteActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Sprite)
	{
		State->Sprite->MarkPackageDirty();
	}
	delete State;
}

void FManageAssetPaperSpriteCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_source"), &HandleSprite_SetSource);
	OutHandlers.Add(TEXT("set_pivot"),  &HandleSprite_SetPivot);
}

REGISTER_MCP_CAPABILITY(FManageAssetPaperSpriteCapability)
#endif
