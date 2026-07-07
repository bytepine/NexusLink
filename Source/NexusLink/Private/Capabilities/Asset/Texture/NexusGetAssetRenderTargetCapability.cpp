// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Texture/NexusGetAssetRenderTargetCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Engine/TextureRenderTarget2D.h"
#include "NexusMcpTool.h"

void FGetAssetRenderTargetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_render_target");
	Out.Description = TEXT("读取 TextureRenderTarget2D：尺寸/格式/清除色/生成Mips。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("RenderTarget 资产路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("render"), TEXT("target"), TEXT("texture"), TEXT("rt"), TEXT("size"), TEXT("format") };
	Out.RelatedCapabilities = { TEXT("create_asset_render_target"), TEXT("manage_asset_render_target") };
}

FCapabilityResult FGetAssetRenderTargetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
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
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("加载 RenderTarget 失败: %s"), *AssetPath));
			return;
		}

		// 格式字符串
		const UEnum* FormatEnum = StaticEnum<ETextureRenderTargetFormat>();
		const FString FormatStr = FormatEnum
			? FormatEnum->GetNameStringByValue((int64)RT->RenderTargetFormat)
			: FString::FromInt((int32)RT->RenderTargetFormat);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),         RT->GetName());
		Entry->SetStringField(TEXT("path"),         RT->GetPathName());
		Entry->SetNumberField(TEXT("sizeX"),        RT->SizeX);
		Entry->SetNumberField(TEXT("sizeY"),        RT->SizeY);
		Entry->SetStringField(TEXT("format"),       FormatStr);
		Entry->SetNumberField(TEXT("formatValue"),  (double)(int32)RT->RenderTargetFormat);
		Entry->SetBoolField(TEXT("bHDR"),           RT->bHDR_DEPRECATED || (int32)RT->RenderTargetFormat >= (int32)ETextureRenderTargetFormat::RTF_R16f);

		// ClearColor
		TSharedPtr<FJsonObject> ClearColorObj = MakeShared<FJsonObject>();
		ClearColorObj->SetNumberField(TEXT("r"), RT->ClearColor.R);
		ClearColorObj->SetNumberField(TEXT("g"), RT->ClearColor.G);
		ClearColorObj->SetNumberField(TEXT("b"), RT->ClearColor.B);
		ClearColorObj->SetNumberField(TEXT("a"), RT->ClearColor.A);
		Entry->SetObjectField(TEXT("clearColor"), ClearColorObj);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetRenderTargetCapability)
