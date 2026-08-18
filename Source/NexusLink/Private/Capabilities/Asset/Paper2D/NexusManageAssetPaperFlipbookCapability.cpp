// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusManageAssetPaperFlipbookCapability.h"
#if WITH_PAPER2D
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "NexusMcpTool.h"

void FManageAssetPaperFlipbookCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_paper_flipbook");
	Out.SearchAssetTypes = {TEXT("PaperFlipbook")};
	Out.Description = TEXT("Batch edit PaperFlipbook. action=add_key/remove_key/set_frames_per_second.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("add_key"), TEXT("remove_key"), TEXT("set_frames_per_second") }))
		.Prop(TEXT("spritePath"), FNexusSchema::Str(TEXT("PaperSprite path (add_key)")))
		.Prop(TEXT("frameRun"), FNexusSchema::Int(TEXT("Duration in frames (add_key)"), 1))
		.Prop(TEXT("keyIndex"), FNexusSchema::Int(TEXT("Keyframe index (remove_key)")))
		.Prop(TEXT("framesPerSecond"), FNexusSchema::Num(TEXT("Frame rate (set_frames_per_second)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("PaperFlipbook asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("flipbook"), TEXT("fps"), TEXT("frame") };
	Out.RelatedCapabilities = { TEXT("get_asset_paper_flipbook"), TEXT("create_asset_paper_flipbook") };
}

struct FFlipbookActionState
{
	UPaperFlipbook* Book = nullptr;
	bool bDirty = false;
};

static FFlipbookActionState* FlipState(FNexusActionContext& Ctx)
{
	return static_cast<FFlipbookActionState*>(Ctx.Target);
}

static UPaperFlipbook* FlipFrom(FNexusActionContext& Ctx)
{
	FFlipbookActionState* S = FlipState(Ctx);
	return S ? S->Book : nullptr;
}

static void MarkFlipDirty(FNexusActionContext& Ctx)
{
	if (FFlipbookActionState* S = FlipState(Ctx))
	{
		S->bDirty = true;
	}
}

static void HandleFlip_AddKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperFlipbook* Book = FlipFrom(Ctx);
	FPaperFlipbookKeyFrame Kf;
	Kf.FrameRun = Op->HasField(TEXT("frameRun")) ? static_cast<int32>(Op->GetNumberField(TEXT("frameRun"))) : 1;
	FString SpritePath;
	if (Op->TryGetStringField(TEXT("spritePath"), SpritePath) && !SpritePath.IsEmpty())
	{
		Kf.Sprite = FNexusAssetUtils::LoadAssetWithFallback<UPaperSprite>(SpritePath);
	}
	Book->KeyFrames.Add(Kf);
	MarkFlipDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("keyCount"), Book->KeyFrames.Num());
}

static void HandleFlip_RemoveKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperFlipbook* Book = FlipFrom(Ctx);
	if (!Op->HasField(TEXT("keyIndex")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_key requires keyIndex"));
		return;
	}
	const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("keyIndex")));
	if (!Book->KeyFrames.IsValidIndex(Idx))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("keyIndex out of bounds"));
		return;
	}
	Book->KeyFrames.RemoveAt(Idx);
	MarkFlipDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("keyCount"), Book->KeyFrames.Num());
}

static void HandleFlip_SetFPS(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperFlipbook* Book = FlipFrom(Ctx);
	if (!Op->HasField(TEXT("framesPerSecond")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_frames_per_second requires framesPerSecond"));
		return;
	}
	Book->SetFramesPerSecond(static_cast<float>(Op->GetNumberField(TEXT("framesPerSecond"))));
	MarkFlipDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("framesPerSecond"), Book->GetFramesPerSecond());
}

bool FManageAssetPaperFlipbookCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UPaperFlipbook* Book = FNexusAssetUtils::LoadAssetWithFallback<UPaperFlipbook>(AssetPath);
	if (!Book)
	{
		OutError = FString::Printf(TEXT("Failed to load PaperFlipbook: %s"), *AssetPath);
		return false;
	}
	FFlipbookActionState* State = new FFlipbookActionState();
	State->Book = Book;
	OutTarget = State;
	return true;
}

void FManageAssetPaperFlipbookCapability::FinalizeTarget(void* Target) const
{
	FFlipbookActionState* State = static_cast<FFlipbookActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Book)
	{
		State->Book->MarkPackageDirty();
	}
	delete State;
}

void FManageAssetPaperFlipbookCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("add_key"),              &HandleFlip_AddKey);
	OutHandlers.Add(TEXT("remove_key"),           &HandleFlip_RemoveKey);
	OutHandlers.Add(TEXT("set_frames_per_second"), &HandleFlip_SetFPS);
}

REGISTER_MCP_CAPABILITY(FManageAssetPaperFlipbookCapability)
#endif
