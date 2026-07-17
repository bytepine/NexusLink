// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Texture/NexusManageAssetRenderTargetCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Engine/TextureRenderTarget2D.h"
#include "NexusMcpTool.h"

void FManageAssetRenderTargetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_render_target");
	Out.SearchAssetTypes = {TEXT("TextureRenderTarget2D")};
	Out.Description = TEXT("修改 TextureRenderTarget2D：sizeX/sizeY/formatValue/clearColor(r,g,b,a)。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),  FNexusSchema::Str(TEXT("RenderTarget 资产路径")))
		.Prop(TEXT("sizeX"),          FNexusSchema::Int(TEXT("宽度（≥1）"), 0, 1))
		.Prop(TEXT("sizeY"),          FNexusSchema::Int(TEXT("高度（≥1）"), 0, 1))
		.Prop(TEXT("formatValue"),    FNexusSchema::Int(TEXT("ETextureRenderTargetFormat 枚举值：0=RGBA8,1=RGBA16f…")))
		.Prop(TEXT("clearColorR"),    FNexusSchema::Num(TEXT("ClearColor R [0,1]")))
		.Prop(TEXT("clearColorG"),    FNexusSchema::Num(TEXT("ClearColor G [0,1]")))
		.Prop(TEXT("clearColorB"),    FNexusSchema::Num(TEXT("ClearColor B [0,1]")))
		.Prop(TEXT("clearColorA"),    FNexusSchema::Num(TEXT("ClearColor A [0,1]")))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("render"), TEXT("target"), TEXT("resize"), TEXT("format"), TEXT("clear") };
	Out.RelatedCapabilities = { TEXT("create_asset_render_target"), TEXT("get_asset_render_target") };
}

FCapabilityResult FManageAssetRenderTargetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		UTextureRenderTarget2D* RT = LoadObject<UTextureRenderTarget2D>(nullptr, *AssetPath);
		if (!RT)
		{
			OutError = FString::Printf(TEXT("加载 RenderTarget 失败: %s"), *AssetPath);
			return;
		}

		bool bResized = false;
		if (Arguments->HasField(TEXT("sizeX"))) { RT->SizeX = FMath::Max(1, (int32)Arguments->GetNumberField(TEXT("sizeX"))); bResized = true; }
		if (Arguments->HasField(TEXT("sizeY"))) { RT->SizeY = FMath::Max(1, (int32)Arguments->GetNumberField(TEXT("sizeY"))); bResized = true; }
		if (Arguments->HasField(TEXT("formatValue")))
		{
			RT->RenderTargetFormat = ETextureRenderTargetFormat((int32)Arguments->GetNumberField(TEXT("formatValue")));
			bResized = true;
		}
		if (Arguments->HasField(TEXT("clearColorR"))) RT->ClearColor.R = (float)Arguments->GetNumberField(TEXT("clearColorR"));
		if (Arguments->HasField(TEXT("clearColorG"))) RT->ClearColor.G = (float)Arguments->GetNumberField(TEXT("clearColorG"));
		if (Arguments->HasField(TEXT("clearColorB"))) RT->ClearColor.B = (float)Arguments->GetNumberField(TEXT("clearColorB"));
		if (Arguments->HasField(TEXT("clearColorA"))) RT->ClearColor.A = (float)Arguments->GetNumberField(TEXT("clearColorA"));

		if (bResized)
			RT->UpdateResourceImmediate(true);

		RT->MarkPackageDirty();

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),         RT->GetName());
		Entry->SetNumberField(TEXT("sizeX"),        RT->SizeX);
		Entry->SetNumberField(TEXT("sizeY"),        RT->SizeY);
		Entry->SetNumberField(TEXT("formatValue"),  (double)(int32)RT->RenderTargetFormat);
		Entry->SetBoolField(TEXT("success"),        true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetRenderTargetCapability)
