// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Texture/NexusManageAssetRenderTargetCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "Engine/TextureRenderTarget2D.h"
#include "NexusMcpTool.h"

void FManageAssetRenderTargetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_render_target");
	Out.SearchAssetTypes = {TEXT("TextureRenderTarget2D")};
	Out.Description = TEXT("Edit TextureRenderTarget2D: sizeX/sizeY/formatValue/clearColor(r,g,b,a).");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("Action"), { TEXT("set") }))
		.Prop(TEXT("sizeX"),        FNexusSchema::Int(TEXT("Width (≥1)"), 0, 1))
		.Prop(TEXT("sizeY"),        FNexusSchema::Int(TEXT("Height (≥1)"), 0, 1))
		.Prop(TEXT("formatValue"),  FNexusSchema::Int(TEXT("ETextureRenderTargetFormat enum: 0=RGBA8,1=RGBA16f…")))
		.Prop(TEXT("clearColorR"),  FNexusSchema::Num(TEXT("ClearColor R [0,1]")))
		.Prop(TEXT("clearColorG"),  FNexusSchema::Num(TEXT("ClearColor G [0,1]")))
		.Prop(TEXT("clearColorB"),  FNexusSchema::Num(TEXT("ClearColor B [0,1]")))
		.Prop(TEXT("clearColorA"),  FNexusSchema::Num(TEXT("ClearColor A [0,1]")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("RenderTarget asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("render"), TEXT("target"), TEXT("resize"), TEXT("format"), TEXT("clear") };
	Out.RelatedCapabilities = { TEXT("create_asset_render_target"), TEXT("get_asset_render_target") };
}

struct FRTActionState
{
	UTextureRenderTarget2D* RT = nullptr;
	bool bDirty = false;
};

static FRTActionState* RTState(FNexusActionContext& Ctx)
{
	return static_cast<FRTActionState*>(Ctx.Target);
}

static void HandleRT_Set(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UTextureRenderTarget2D* RT = RTState(Ctx)->RT;
	bool bResized = false;
	if (Op->HasField(TEXT("sizeX"))) { RT->SizeX = FMath::Max(1, static_cast<int32>(Op->GetNumberField(TEXT("sizeX")))); bResized = true; }
	if (Op->HasField(TEXT("sizeY"))) { RT->SizeY = FMath::Max(1, static_cast<int32>(Op->GetNumberField(TEXT("sizeY")))); bResized = true; }
	if (Op->HasField(TEXT("formatValue")))
	{
		RT->RenderTargetFormat = ETextureRenderTargetFormat(static_cast<int32>(Op->GetNumberField(TEXT("formatValue"))));
		bResized = true;
	}
	if (Op->HasField(TEXT("clearColorR"))) RT->ClearColor.R = static_cast<float>(Op->GetNumberField(TEXT("clearColorR")));
	if (Op->HasField(TEXT("clearColorG"))) RT->ClearColor.G = static_cast<float>(Op->GetNumberField(TEXT("clearColorG")));
	if (Op->HasField(TEXT("clearColorB"))) RT->ClearColor.B = static_cast<float>(Op->GetNumberField(TEXT("clearColorB")));
	if (Op->HasField(TEXT("clearColorA"))) RT->ClearColor.A = static_cast<float>(Op->GetNumberField(TEXT("clearColorA")));
	if (bResized) RT->UpdateResourceImmediate(true);
	RTState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("name"),        RT->GetName());
	Ctx.Entry->SetNumberField(TEXT("sizeX"),       RT->SizeX);
	Ctx.Entry->SetNumberField(TEXT("sizeY"),       RT->SizeY);
	Ctx.Entry->SetNumberField(TEXT("formatValue"), static_cast<double>(static_cast<int32>(RT->RenderTargetFormat)));
}

bool FManageAssetRenderTargetCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UTextureRenderTarget2D* RT = LoadObject<UTextureRenderTarget2D>(nullptr, *AssetPath);
	if (!RT)
	{
		OutError = FString::Printf(TEXT("Failed to load RenderTarget: %s"), *AssetPath);
		return false;
	}
	FRTActionState* State = new FRTActionState();
	State->RT = RT;
	OutTarget = State;
	return true;
}

void FManageAssetRenderTargetCapability::FinalizeTarget(void* Target) const
{
	FRTActionState* State = static_cast<FRTActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->RT) State->RT->MarkPackageDirty();
	delete State;
}

void FManageAssetRenderTargetCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set"), &HandleRT_Set);
}

REGISTER_MCP_CAPABILITY(FManageAssetRenderTargetCapability)
