// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusManageAssetPaperSpriteCapability.h"
#if WITH_PAPER2D
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "PaperSprite.h"
#include "SpriteEditorOnlyTypes.h"
#include "Engine/Texture2D.h"
#include "NexusMcpTool.h"

void FNexusManageAssetPaperSpriteCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetPaperSprite");
	Out.SearchAssetTypes = {TEXT("PaperSprite")};
	Out.Description = TEXT("Batch edit PaperSprite. Action=SetSource/SetPivot.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("SetSource"), TEXT("SetPivot") }))
		.Prop(TEXT("SourceTexturePath"), FNexusSchema::Str(TEXT("Source Texture2D (SetSource)")))
		.Prop(TEXT("PivotX"), FNexusSchema::Num(TEXT("Pivot X (SetPivot)")))
		.Prop(TEXT("PivotY"), FNexusSchema::Num(TEXT("Pivot Y (SetPivot)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("PaperSprite asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("Sprite"), TEXT("pivot"), TEXT("Source") };
	Out.RelatedCapabilities = { TEXT("GetAssetPaperSprite"), TEXT("CreateAssetPaperSprite") };
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
	if (!Sprite)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PaperSprite is null"));
		return;
	}
	FString TexPath;
	if (!Op->TryGetStringField(TEXT("SourceTexturePath"), TexPath) || TexPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetSource requires SourceTexturePath"));
		return;
	}
	UTexture2D* Tex = FNexusAssetUtils::LoadAssetWithFallback<UTexture2D>(TexPath);
	if (!Tex)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Texture2D not found: %s"), *TexPath));
		return;
	}
	FSpriteAssetInitParameters Init;
	Init.SetTextureAndFill(Tex);
	Sprite->InitializeSprite(Init);
	MarkSpriteDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("SourceTexture"), Tex->GetPathName());
}

static void HandleSprite_SetPivot(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperSprite* Sprite = SpriteFrom(Ctx);
	if (!Sprite)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PaperSprite is null"));
		return;
	}
	FVector2D Pivot = Sprite->GetPivotPosition();
	if (Op->HasField(TEXT("PivotX"))) Pivot.X = static_cast<float>(Op->GetNumberField(TEXT("PivotX")));
	if (Op->HasField(TEXT("PivotY"))) Pivot.Y = static_cast<float>(Op->GetNumberField(TEXT("PivotY")));
	Sprite->SetPivotMode(ESpritePivotMode::Custom, Pivot, true);
	MarkSpriteDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("PivotX"), Pivot.X);
	Ctx.Entry->SetNumberField(TEXT("PivotY"), Pivot.Y);
}

bool FNexusManageAssetPaperSpriteCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetPaperSpriteCapability::FinalizeTarget(void* Target) const
{
	FPaperSpriteActionState* State = static_cast<FPaperSpriteActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Sprite)
	{
		State->Sprite->MarkPackageDirty();
	}
	delete State;
}

void FNexusManageAssetPaperSpriteCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetSource"), &HandleSprite_SetSource);
	OutHandlers.Add(TEXT("SetPivot"),  &HandleSprite_SetPivot);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetPaperSpriteCapability)
#endif
