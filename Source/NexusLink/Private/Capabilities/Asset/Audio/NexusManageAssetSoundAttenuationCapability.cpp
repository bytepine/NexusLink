// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Audio/NexusManageAssetSoundAttenuationCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "Sound/SoundAttenuation.h"
#include "NexusMcpTool.h"

void FManageAssetSoundAttenuationCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_sound_attenuation");
	Out.SearchAssetTypes = {TEXT("SoundAttenuation")};
	Out.Description = TEXT("设置 SoundAttenuation：innerRadius/falloffDistance/shapeValue/bAttenuate/bSpatialize。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),          FNexusSchema::Enum(TEXT("操作"), { TEXT("set") }))
		.Prop(TEXT("innerRadius"),     FNexusSchema::Num(TEXT("内半径（球形 = Sphere Radius，cm）")))
		.Prop(TEXT("falloffDistance"), FNexusSchema::Num(TEXT("衰减距离（cm）")))
		.Prop(TEXT("shapeValue"),      FNexusSchema::Int(TEXT("形状枚举值：0=Sphere,1=Capsule,2=Box,3=Cone")))
		.Prop(TEXT("bAttenuate"),      FNexusSchema::Bool(TEXT("启用距离衰减")))
		.Prop(TEXT("bSpatialize"),     FNexusSchema::Bool(TEXT("启用空间化")))
		.Prop(TEXT("dBAtMax"),         FNexusSchema::Num(TEXT("最大衰减量（dB，Natural Sound 算法）")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("SoundAttenuation 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("attenuation"), TEXT("sound"), TEXT("radius"), TEXT("distance"), TEXT("shape") };
	Out.RelatedCapabilities = { TEXT("get_asset_sound_attenuation"), TEXT("create_asset_sound_attenuation") };
}

FCapabilityResult FManageAssetSoundAttenuationCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		USoundAttenuation* SA = LoadObject<USoundAttenuation>(nullptr, *AssetPath);
		if (!SA)
		{
			OutError = FString::Printf(TEXT("加载 SoundAttenuation 失败: %s"), *AssetPath);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("缺少 operations 或为空");
			return;
		}

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject>* OpPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpPtr) || !OpPtr)
			{
				Entry->SetStringField(TEXT("error"), TEXT("无效的 operation 项"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			const TSharedPtr<FJsonObject>& Op = *OpPtr;

			const FString Action = Op->HasField(TEXT("action")) ? Op->GetStringField(TEXT("action")).ToLower() : TEXT("");
			Entry->SetStringField(TEXT("action"), Action);
			if (Action != TEXT("set"))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的操作: '%s'（仅 set）"), *Action));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			if (Op->HasField(TEXT("innerRadius")))
				SA->Attenuation.AttenuationShapeExtents.X = (float)Op->GetNumberField(TEXT("innerRadius"));
			if (Op->HasField(TEXT("falloffDistance")))
				SA->Attenuation.FalloffDistance = (float)Op->GetNumberField(TEXT("falloffDistance"));
			if (Op->HasField(TEXT("shapeValue")))
				SA->Attenuation.AttenuationShape = EAttenuationShape::Type((int32)Op->GetNumberField(TEXT("shapeValue")));
			if (Op->HasField(TEXT("bAttenuate")))
				SA->Attenuation.bAttenuate = Op->GetBoolField(TEXT("bAttenuate")) ? 1 : 0;
			if (Op->HasField(TEXT("bSpatialize")))
				SA->Attenuation.bSpatialize = Op->GetBoolField(TEXT("bSpatialize")) ? 1 : 0;
			if (Op->HasField(TEXT("dBAtMax")))
				SA->Attenuation.dBAttenuationAtMax = (float)Op->GetNumberField(TEXT("dBAtMax"));

			SA->MarkPackageDirty();

			Entry->SetStringField(TEXT("name"),            SA->GetName());
			Entry->SetNumberField(TEXT("innerRadius"),     SA->Attenuation.AttenuationShapeExtents.X);
			Entry->SetNumberField(TEXT("falloffDistance"), SA->Attenuation.FalloffDistance);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetSoundAttenuationCapability)
