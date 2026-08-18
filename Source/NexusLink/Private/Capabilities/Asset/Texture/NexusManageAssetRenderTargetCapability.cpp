// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Texture/NexusManageAssetRenderTargetCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
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

FCapabilityResult FManageAssetRenderTargetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));
		UTextureRenderTarget2D* RT = LoadObject<UTextureRenderTarget2D>(nullptr, *AssetPath);
		if (!RT)
		{
			OutError = FString::Printf(TEXT("Failed to load RenderTarget: %s"), *AssetPath);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("Missing or empty operations");
			return;
		}

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject>* OpPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpPtr) || !OpPtr)
			{
				Entry->SetStringField(TEXT("error"), TEXT("Invalid operation item"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			const TSharedPtr<FJsonObject>& Op = *OpPtr;

			const FString Action = FNexusArgs(Op).Str(TEXT("action")).ToLower();
			Entry->SetStringField(TEXT("action"), Action);
			if (Action != TEXT("set"))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported operation: '%s' (set only)"), *Action));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			bool bResized = false;
			if (Op->HasField(TEXT("sizeX"))) { RT->SizeX = FMath::Max(1, (int32)Op->GetNumberField(TEXT("sizeX"))); bResized = true; }
			if (Op->HasField(TEXT("sizeY"))) { RT->SizeY = FMath::Max(1, (int32)Op->GetNumberField(TEXT("sizeY"))); bResized = true; }
			if (Op->HasField(TEXT("formatValue")))
			{
				RT->RenderTargetFormat = ETextureRenderTargetFormat((int32)Op->GetNumberField(TEXT("formatValue")));
				bResized = true;
			}
			if (Op->HasField(TEXT("clearColorR"))) RT->ClearColor.R = (float)Op->GetNumberField(TEXT("clearColorR"));
			if (Op->HasField(TEXT("clearColorG"))) RT->ClearColor.G = (float)Op->GetNumberField(TEXT("clearColorG"));
			if (Op->HasField(TEXT("clearColorB"))) RT->ClearColor.B = (float)Op->GetNumberField(TEXT("clearColorB"));
			if (Op->HasField(TEXT("clearColorA"))) RT->ClearColor.A = (float)Op->GetNumberField(TEXT("clearColorA"));

			if (bResized)
				RT->UpdateResourceImmediate(true);

			RT->MarkPackageDirty();

			Entry->SetStringField(TEXT("name"),        RT->GetName());
			Entry->SetNumberField(TEXT("sizeX"),       RT->SizeX);
			Entry->SetNumberField(TEXT("sizeY"),       RT->SizeY);
			Entry->SetNumberField(TEXT("formatValue"), (double)(int32)RT->RenderTargetFormat);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetRenderTargetCapability)
