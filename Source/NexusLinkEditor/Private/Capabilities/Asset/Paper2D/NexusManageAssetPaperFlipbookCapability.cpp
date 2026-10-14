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

void FNexusManageAssetPaperFlipbookCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetPaperFlipbook");
	Out.SearchAssetTypes = {TEXT("PaperFlipbook")};
	Out.Description = TEXT("Batch edit PaperFlipbook. Action=AddKey/RemoveKey/SetFramesPerSecond.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("AddKey"), TEXT("RemoveKey"), TEXT("SetFramesPerSecond") }))
		.Prop(TEXT("SpritePath"), FNexusSchema::Str(TEXT("PaperSprite path (AddKey)")))
		.Prop(TEXT("FrameRun"), FNexusSchema::Int(TEXT("Duration in frames (AddKey)"), 1))
		.Prop(TEXT("KeyIndex"), FNexusSchema::Int(TEXT("Keyframe index (RemoveKey)")))
		.Prop(TEXT("FramesPerSecond"), FNexusSchema::Num(TEXT("Frame rate (SetFramesPerSecond)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("PaperFlipbook asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("flipbook"), TEXT("fps"), TEXT("frame") };
	Out.RelatedCapabilities = { TEXT("GetAssetPaperFlipbook"), TEXT("CreateAssetPaperFlipbook") };
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
	if (!Book)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PaperFlipbook is null"));
		return;
	}
	FPaperFlipbookKeyFrame Kf;
	Kf.FrameRun = Op->HasField(TEXT("FrameRun")) ? static_cast<int32>(Op->GetNumberField(TEXT("FrameRun"))) : 1;
	FString SpritePath;
	if (Op->TryGetStringField(TEXT("SpritePath"), SpritePath) && !SpritePath.IsEmpty())
	{
		Kf.Sprite = FNexusAssetUtils::LoadAssetWithFallback<UPaperSprite>(SpritePath);
	}
	{
		FScopedFlipbookMutator Mut(Book);
		Mut.KeyFrames.Add(Kf);
	}
	MarkFlipDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("KeyCount"), Book->GetNumKeyFrames());
}

static void HandleFlip_RemoveKey(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperFlipbook* Book = FlipFrom(Ctx);
	if (!Book)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PaperFlipbook is null"));
		return;
	}
	if (!Op->HasField(TEXT("KeyIndex")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveKey requires KeyIndex"));
		return;
	}
	const int32 Idx = static_cast<int32>(Op->GetNumberField(TEXT("KeyIndex")));
	if (!Book->IsValidKeyFrameIndex(Idx))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("KeyIndex out of bounds"));
		return;
	}
	{
		FScopedFlipbookMutator Mut(Book);
		Mut.KeyFrames.RemoveAt(Idx);
	}
	MarkFlipDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("KeyCount"), Book->GetNumKeyFrames());
}

static void HandleFlip_SetFPS(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPaperFlipbook* Book = FlipFrom(Ctx);
	if (!Book)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PaperFlipbook is null"));
		return;
	}
	if (!Op->HasField(TEXT("FramesPerSecond")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetFramesPerSecond requires FramesPerSecond"));
		return;
	}
	{
		FScopedFlipbookMutator Mut(Book);
		Mut.FramesPerSecond = static_cast<float>(Op->GetNumberField(TEXT("FramesPerSecond")));
	}
	MarkFlipDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("FramesPerSecond"), Book->GetFramesPerSecond());
}

bool FNexusManageAssetPaperFlipbookCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetPaperFlipbookCapability::FinalizeTarget(void* Target) const
{
	FFlipbookActionState* State = static_cast<FFlipbookActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Book)
	{
		State->Book->MarkPackageDirty();
	}
	delete State;
}

void FNexusManageAssetPaperFlipbookCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("AddKey"),              &HandleFlip_AddKey);
	OutHandlers.Add(TEXT("RemoveKey"),           &HandleFlip_RemoveKey);
	OutHandlers.Add(TEXT("SetFramesPerSecond"), &HandleFlip_SetFPS);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetPaperFlipbookCapability)
#endif
