// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Texture/NexusGetAssetTextureCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Engine/Texture2D.h"
#include "NexusMcpTool.h"

void FGetAssetTextureCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_texture");
	Out.Description = TEXT("检查 Texture2D 快照。尺寸/压缩/sRGB/LOD。写用 manage_asset_texture。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("Texture2D 资产路径")))
		.Prop(TEXT("assetPaths"), FNexusSchema::StrArr(TEXT("多个 Texture2D 路径（批量）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("texture"), TEXT("image"), TEXT("png"), TEXT("size"), TEXT("compress") };
	Out.RelatedCapabilities = { TEXT("manage_asset_texture"), TEXT("search_asset"), TEXT("get_asset_refs"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("读贴图元数据；写用 manage_asset_texture");
}

static void CollectTexturePaths(const TSharedPtr<FJsonObject>& Args, TArray<FString>& OutPaths)
{
	OutPaths.Reset();
	if (!Args.IsValid()) return;

	FString Single;
	if (Args->TryGetStringField(TEXT("assetPath"), Single) && !Single.IsEmpty())
	{
		OutPaths.Add(Single);
	}

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Args->TryGetArrayField(TEXT("assetPaths"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FString P;
			if (V.IsValid() && V->TryGetString(P) && !P.IsEmpty())
			{
				OutPaths.AddUnique(P);
			}
		}
	}
}

FCapabilityResult FGetAssetTextureCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TArray<FString> Paths;
		CollectTexturePaths(Arguments, Paths);
		if (Paths.Num() == 0)
		{
			OutError = TEXT("需要 assetPath 或 assetPaths");
			return;
		}

		for (const FString& Path : Paths)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("assetPath"), Path);

			UTexture2D* Tex = FNexusAssetUtils::LoadAssetWithFallback<UTexture2D>(Path);
			if (!Tex)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Texture2D 未找到: %s"), *Path));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			Entry->SetStringField(TEXT("name"), Tex->GetName());
			Entry->SetStringField(TEXT("assetType"), TEXT("Texture2D"));

			int32 TexW = 0;
			int32 TexH = 0;
			FNexusAssetUtils::GetTexture2DSurfaceSize(Tex, TexW, TexH);
			Entry->SetNumberField(TEXT("width"),  TexW);
			Entry->SetNumberField(TEXT("height"), TexH);
			Entry->SetNumberField(TEXT("pixelFormat"), static_cast<double>(static_cast<int32>(Tex->GetPixelFormat())));
			Entry->SetNumberField(TEXT("compressionSettings"), static_cast<double>(static_cast<int32>(Tex->CompressionSettings)));
			Entry->SetNumberField(TEXT("lodGroup"), static_cast<double>(static_cast<int32>(Tex->LODGroup)));
			Entry->SetBoolField(TEXT("sRGB"), Tex->SRGB);
			Entry->SetNumberField(TEXT("mipCount"), Tex->GetNumMips());

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetTextureCapability)
