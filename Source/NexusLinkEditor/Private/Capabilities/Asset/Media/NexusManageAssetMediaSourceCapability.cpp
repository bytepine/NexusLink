// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Media/NexusManageAssetMediaSourceCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPropertyUtils.h"
#include "FileMediaSource.h"
#include "NexusMcpTool.h"

void FNexusManageAssetMediaSourceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetMediaSource");
	Out.SearchAssetTypes = {TEXT("FileMediaSource"), TEXT("MediaSource")};
	Out.Description = TEXT("Batch edit FileMediaSource. Action=SetFilePath/SetLoop.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("SetFilePath"), TEXT("SetLoop") }))
		.Prop(TEXT("MediaPath"), FNexusSchema::Str(TEXT("Media file path (SetFilePath)")))
		.Prop(TEXT("bLoop"), FNexusSchema::Bool(TEXT("Loop flag (SetLoop, reflected field)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("MediaSource asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("video"), TEXT("Path"), TEXT("bLoop"), TEXT("media") };
	Out.RelatedCapabilities = { TEXT("GetAssetMediaSource"), TEXT("CreateAssetMediaSource") };
}

struct FMediaSourceActionState
{
	UFileMediaSource* Source = nullptr;
	bool bDirty = false;
};

static FMediaSourceActionState* MSState(FNexusActionContext& Ctx)
{
	return static_cast<FMediaSourceActionState*>(Ctx.Target);
}

static UFileMediaSource* MSFrom(FNexusActionContext& Ctx)
{
	FMediaSourceActionState* S = MSState(Ctx);
	return S ? S->Source : nullptr;
}

static void MarkMSDirty(FNexusActionContext& Ctx)
{
	if (FMediaSourceActionState* S = MSState(Ctx))
	{
		S->bDirty = true;
	}
}

static void HandleMS_SetFilePath(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UFileMediaSource* Source = MSFrom(Ctx);
	if (!Source)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("FileMediaSource is null"));
		return;
	}
	FString FilePath;
	if (!Op->TryGetStringField(TEXT("MediaPath"), FilePath) || FilePath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetFilePath requires MediaPath"));
		return;
	}
	Source->SetFilePath(FilePath);
	MarkMSDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("MediaPath"), Source->GetFilePath());
}

static void HandleMS_SetLoop(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UFileMediaSource* Source = MSFrom(Ctx);
	if (!Op->HasField(TEXT("bLoop")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetLoop requires loop"));
		return;
	}
	const bool bLoop = Op->GetBoolField(TEXT("bLoop"));
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(
		Source, { TEXT("Loop") }, 0, bLoop ? TEXT("True") : TEXT("False"), OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("Error"),
			Err.IsEmpty() ? TEXT("no Loop field") : Err);
		return;
	}
	MarkMSDirty(Ctx);
	Ctx.Entry->SetBoolField(TEXT("bLoop"), bLoop);
}

bool FNexusManageAssetMediaSourceCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UFileMediaSource* Source = FNexusAssetUtils::LoadAssetWithFallback<UFileMediaSource>(AssetPath);
	if (!Source)
	{
		OutError = FString::Printf(TEXT("Failed to load FileMediaSource: %s"), *AssetPath);
		return false;
	}
	FMediaSourceActionState* State = new FMediaSourceActionState();
	State->Source = Source;
	OutTarget = State;
	return true;
}

void FNexusManageAssetMediaSourceCapability::FinalizeTarget(void* Target) const
{
	FMediaSourceActionState* State = static_cast<FMediaSourceActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Source)
	{
		State->Source->MarkPackageDirty();
	}
	delete State;
}

void FNexusManageAssetMediaSourceCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetFilePath"), &HandleMS_SetFilePath);
	OutHandlers.Add(TEXT("SetLoop"),      &HandleMS_SetLoop);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetMediaSourceCapability)
