// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Texture/NexusManageAssetRenderTargetCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "Engine/TextureRenderTarget2D.h"
#include "NexusMcpTool.h"

void FNexusManageAssetRenderTargetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetRenderTarget");
	Out.SearchAssetTypes = {TEXT("TextureRenderTarget2D")};
	Out.Description = TEXT("Edit TextureRenderTarget2D: SizeX/SizeY/FormatValue/ClearColor(r,g,b,a).");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),       FNexusSchema::Enum(TEXT("Action"), { TEXT("Set") }))
		.Prop(TEXT("SizeX"),        FNexusSchema::Int(TEXT("Width (≥1)"), 0, 1))
		.Prop(TEXT("SizeY"),        FNexusSchema::Int(TEXT("Height (≥1)"), 0, 1))
		.Prop(TEXT("FormatValue"),  FNexusSchema::Int(TEXT("ETextureRenderTargetFormat enum: 0=RGBA8,1=RGBA16f…")))
		.Prop(TEXT("ClearColorR"),  FNexusSchema::Num(TEXT("ClearColor R [0,1]")))
		.Prop(TEXT("ClearColorG"),  FNexusSchema::Num(TEXT("ClearColor G [0,1]")))
		.Prop(TEXT("ClearColorB"),  FNexusSchema::Num(TEXT("ClearColor B [0,1]")))
		.Prop(TEXT("ClearColorA"),  FNexusSchema::Num(TEXT("ClearColor A [0,1]")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("RenderTarget asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("render"), TEXT("Target"), TEXT("resize"), TEXT("Format"), TEXT("clear") };
	Out.RelatedCapabilities = { TEXT("CreateAssetRenderTarget"), TEXT("GetAssetRenderTarget") };
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
	if (Op->HasField(TEXT("SizeX"))) { RT->SizeX = FMath::Max(1, static_cast<int32>(Op->GetNumberField(TEXT("SizeX")))); bResized = true; }
	if (Op->HasField(TEXT("SizeY"))) { RT->SizeY = FMath::Max(1, static_cast<int32>(Op->GetNumberField(TEXT("SizeY")))); bResized = true; }
	if (Op->HasField(TEXT("FormatValue")))
	{
		RT->RenderTargetFormat = ETextureRenderTargetFormat(static_cast<int32>(Op->GetNumberField(TEXT("FormatValue"))));
		bResized = true;
	}
	if (Op->HasField(TEXT("ClearColorR"))) RT->ClearColor.R = static_cast<float>(Op->GetNumberField(TEXT("ClearColorR")));
	if (Op->HasField(TEXT("ClearColorG"))) RT->ClearColor.G = static_cast<float>(Op->GetNumberField(TEXT("ClearColorG")));
	if (Op->HasField(TEXT("ClearColorB"))) RT->ClearColor.B = static_cast<float>(Op->GetNumberField(TEXT("ClearColorB")));
	if (Op->HasField(TEXT("ClearColorA"))) RT->ClearColor.A = static_cast<float>(Op->GetNumberField(TEXT("ClearColorA")));
	if (bResized) RT->UpdateResourceImmediate(true);
	RTState(Ctx)->bDirty = true;
	Ctx.Entry->SetStringField(TEXT("name"),        RT->GetName());
	Ctx.Entry->SetNumberField(TEXT("SizeX"),       RT->SizeX);
	Ctx.Entry->SetNumberField(TEXT("SizeY"),       RT->SizeY);
	Ctx.Entry->SetNumberField(TEXT("FormatValue"), static_cast<double>(static_cast<int32>(RT->RenderTargetFormat)));
}

bool FNexusManageAssetRenderTargetCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetRenderTargetCapability::FinalizeTarget(void* Target) const
{
	FRTActionState* State = static_cast<FRTActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->RT) State->RT->MarkPackageDirty();
	delete State;
}

void FNexusManageAssetRenderTargetCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("Set"), &HandleRT_Set);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetRenderTargetCapability)
