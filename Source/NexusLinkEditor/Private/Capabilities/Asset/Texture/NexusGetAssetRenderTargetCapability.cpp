// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Texture/NexusGetAssetRenderTargetCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Engine/TextureRenderTarget2D.h"
#include "NexusMcpTool.h"

void FNexusGetAssetRenderTargetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetRenderTarget");
	Out.SearchAssetTypes = {TEXT("TextureRenderTarget2D")};
	Out.Description = TEXT("Read TextureRenderTarget2D: size/format/clear color/generate Mips.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("RenderTarget asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("render"), TEXT("Target"), TEXT("Texture"), TEXT("rt"), TEXT("size"), TEXT("Format") };
	Out.RelatedCapabilities = { TEXT("CreateAssetRenderTarget"), TEXT("ManageAssetRenderTarget") };
}

FCapabilityResult FNexusGetAssetRenderTargetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("AssetPath"));
		UTextureRenderTarget2D* RT = LoadObject<UTextureRenderTarget2D>(nullptr, *AssetPath);
		if (!RT)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Failed to load RenderTarget: %s"), *AssetPath));
			return;
		}

		// 格式字符串
		const UEnum* FormatEnum = StaticEnum<ETextureRenderTargetFormat>();
		const FString FormatStr = FormatEnum
			? FormatEnum->GetNameStringByValue((int64)RT->RenderTargetFormat)
			: FString::FromInt((int32)RT->RenderTargetFormat);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),         RT->GetName());
		Entry->SetStringField(TEXT("Path"),         RT->GetPathName());
		Entry->SetNumberField(TEXT("SizeX"),        RT->SizeX);
		Entry->SetNumberField(TEXT("SizeY"),        RT->SizeY);
		Entry->SetStringField(TEXT("Format"),       FormatStr);
		Entry->SetNumberField(TEXT("FormatValue"),  (double)(int32)RT->RenderTargetFormat);
		Entry->SetBoolField(TEXT("bHDR"),           RT->bHDR_DEPRECATED || (int32)RT->RenderTargetFormat >= (int32)ETextureRenderTargetFormat::RTF_R16f);

		// ClearColor
		TSharedPtr<FJsonObject> ClearColorObj = MakeShared<FJsonObject>();
		ClearColorObj->SetNumberField(TEXT("r"), RT->ClearColor.R);
		ClearColorObj->SetNumberField(TEXT("g"), RT->ClearColor.G);
		ClearColorObj->SetNumberField(TEXT("b"), RT->ClearColor.B);
		ClearColorObj->SetNumberField(TEXT("a"), RT->ClearColor.A);
		Entry->SetObjectField(TEXT("ClearColor"), ClearColorObj);

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetRenderTargetCapability)
