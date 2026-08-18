// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MoviePipeline/NexusManageAssetMoviePipelineConfigCapability.h"
#if WITH_MOVIE_RENDER_PIPELINE
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusVersionCompat.h"
#if NX_UE_HAS_MOVIE_PIPELINE_PRIMARY_CONFIG
#include "MoviePipelinePrimaryConfig.h"
using FNexusMoviePipelineConfig = UMoviePipelinePrimaryConfig;
#else
#include "MoviePipelineMasterConfig.h"
using FNexusMoviePipelineConfig = UMoviePipelineMasterConfig;
#endif
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineAntiAliasingSetting.h"
#include "MoviePipelineHighResSetting.h"
#include "MoviePipelineCameraSetting.h"
#include "MoviePipelineGameOverrideSetting.h"
#include "MoviePipelineColorSetting.h"
#include "MoviePipelineDebugSettings.h"
#include "MoviePipelineSetting.h"
#include "NexusMcpTool.h"

namespace
{
	UClass* ResolveMoviePipelineSettingClass(const FString& InName, FString& OutError)
	{
		FString Name = InName;
		Name.ReplaceInline(TEXT("UMoviePipeline"), TEXT(""));
		Name.ReplaceInline(TEXT("MoviePipeline"), TEXT(""));
		if (Name.EndsWith(TEXT("Setting")))
		{
			Name = Name.LeftChop(7);
		}
		if (Name.EndsWith(TEXT("Settings")))
		{
			Name = Name.LeftChop(8);
		}
		if (Name.Equals(TEXT("Output"), ESearchCase::IgnoreCase))
			return UMoviePipelineOutputSetting::StaticClass();
		if (Name.Equals(TEXT("AntiAliasing"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("AA"), ESearchCase::IgnoreCase))
			return UMoviePipelineAntiAliasingSetting::StaticClass();
		if (Name.Equals(TEXT("HighRes"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("HighResolution"), ESearchCase::IgnoreCase))
			return UMoviePipelineHighResSetting::StaticClass();
		if (Name.Equals(TEXT("Camera"), ESearchCase::IgnoreCase))
			return UMoviePipelineCameraSetting::StaticClass();
		if (Name.Equals(TEXT("GameOverride"), ESearchCase::IgnoreCase))
			return UMoviePipelineGameOverrideSetting::StaticClass();
		if (Name.Equals(TEXT("Color"), ESearchCase::IgnoreCase))
			return UMoviePipelineColorSetting::StaticClass();
		if (Name.Equals(TEXT("Debug"), ESearchCase::IgnoreCase))
			return UMoviePipelineDebugSettings::StaticClass();

		OutError = FString::Printf(
			TEXT("Unknown settingClass: %s (Output/AntiAliasing/HighRes/Camera/GameOverride/Color/Debug)"),
			*InName);
		return nullptr;
	}
}

void FManageAssetMoviePipelineConfigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_movie_pipeline_config");
	Out.SearchAssetTypes = {TEXT("MoviePipelinePrimaryConfig"), TEXT("MoviePipelineMasterConfig")};
	Out.Description = TEXT("Batch edit MoviePipeline settings. Does not trigger render.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"), {
			TEXT("set_output"), TEXT("set_anti_aliasing"),
			TEXT("add_setting"), TEXT("remove_setting"),
			TEXT("set_setting_property"), TEXT("set_setting_enabled")
		}))
		.Prop(TEXT("directory"), FNexusSchema::Str(TEXT("Output directory (set_output)")))
		.Prop(TEXT("width"), FNexusSchema::Int(TEXT("Output width")))
		.Prop(TEXT("height"), FNexusSchema::Int(TEXT("Output height")))
		.Prop(TEXT("fileNameFormat"), FNexusSchema::Str(TEXT("Output filename format (set_output)")))
		.Prop(TEXT("spatialSampleCount"), FNexusSchema::Int(TEXT("Spatial samples (set_anti_aliasing)")))
		.Prop(TEXT("temporalSampleCount"), FNexusSchema::Int(TEXT("Temporal samples (set_anti_aliasing)")))
		.Prop(TEXT("settingClass"), FNexusSchema::Enum(TEXT("Setting class short name"), {
			TEXT("Output"), TEXT("AntiAliasing"), TEXT("HighRes"), TEXT("Camera"),
			TEXT("GameOverride"), TEXT("Color"), TEXT("Debug")
		}))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("Setting object property path (set_setting_property)")))
		.Prop(TEXT("value"), FNexusSchema::Str(TEXT("New property value (set_setting_property)")))
		.Prop(TEXT("enabled"), FNexusSchema::Bool(TEXT("Setting enabled flag (set_setting_enabled)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("MoviePipeline config asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mrq"), TEXT("output"), TEXT("resolution"), TEXT("aa"), TEXT("setting") };
	Out.RelatedCapabilities = {
		TEXT("get_asset_movie_pipeline_config"), TEXT("create_asset_movie_pipeline_config")
	};
	Out.WhenToUse = TEXT("Edit MRQ output/AA/settings stack; no render");
}

FCapabilityResult FManageAssetMoviePipelineConfigCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;
		FNexusMoviePipelineConfig* Cfg = FNexusAssetUtils::LoadAssetWithFallback<FNexusMoviePipelineConfig>(AssetPath);
		if (!Cfg)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("Failed to load MoviePipeline config: %s"), *AssetPath));
			return;
		}
		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("Missing or empty operations"));
			return;
		}

		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			const TSharedPtr<FJsonObject>* OpPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpPtr) || !OpPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpPtr;
			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), AssetPath);
			Entry->SetStringField(TEXT("action"), Action);

			if (Action.Equals(TEXT("set_output"), ESearchCase::IgnoreCase))
			{
				UMoviePipelineOutputSetting* OutSet = Cfg->FindSetting<UMoviePipelineOutputSetting>();
				if (!OutSet)
				{
					OutSet = Cast<UMoviePipelineOutputSetting>(
						Cfg->FindOrAddSettingByClass(UMoviePipelineOutputSetting::StaticClass()));
				}
				if (!OutSet)
				{
					Entry->SetStringField(TEXT("error"), TEXT("no OutputSetting"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				FString Dir, Fmt;
				if (Op->TryGetStringField(TEXT("directory"), Dir) && !Dir.IsEmpty())
				{
					OutSet->OutputDirectory.Path = Dir;
				}
				if (Op->TryGetStringField(TEXT("fileNameFormat"), Fmt) && !Fmt.IsEmpty())
				{
					OutSet->FileNameFormat = Fmt;
				}
				if (Op->HasField(TEXT("width")) || Op->HasField(TEXT("height")))
				{
					FIntPoint Res = OutSet->OutputResolution;
					if (Op->HasField(TEXT("width"))) Res.X = static_cast<int32>(Op->GetNumberField(TEXT("width")));
					if (Op->HasField(TEXT("height"))) Res.Y = static_cast<int32>(Op->GetNumberField(TEXT("height")));
					OutSet->OutputResolution = Res;
				}
				bDirty = true;
				Entry->SetStringField(TEXT("directory"), OutSet->OutputDirectory.Path);
				Entry->SetNumberField(TEXT("width"), OutSet->OutputResolution.X);
				Entry->SetNumberField(TEXT("height"), OutSet->OutputResolution.Y);
				Entry->SetStringField(TEXT("fileNameFormat"), OutSet->FileNameFormat);
			}
			else if (Action.Equals(TEXT("set_anti_aliasing"), ESearchCase::IgnoreCase))
			{
				UMoviePipelineAntiAliasingSetting* AA = Cast<UMoviePipelineAntiAliasingSetting>(
					Cfg->FindOrAddSettingByClass(UMoviePipelineAntiAliasingSetting::StaticClass()));
				if (!AA)
				{
					Entry->SetStringField(TEXT("error"), TEXT("Unable to add AntiAliasingSetting"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				if (Op->HasField(TEXT("spatialSampleCount")))
					AA->SpatialSampleCount = FMath::Max(1, static_cast<int32>(Op->GetNumberField(TEXT("spatialSampleCount"))));
				if (Op->HasField(TEXT("temporalSampleCount")))
					AA->TemporalSampleCount = FMath::Max(1, static_cast<int32>(Op->GetNumberField(TEXT("temporalSampleCount"))));
				if (Op->HasField(TEXT("enabled")))
				{
					bool bEnabled = true;
					Op->TryGetBoolField(TEXT("enabled"), bEnabled);
					AA->SetIsEnabled(bEnabled);
				}
				bDirty = true;
				Entry->SetNumberField(TEXT("spatialSampleCount"), AA->SpatialSampleCount);
				Entry->SetNumberField(TEXT("temporalSampleCount"), AA->TemporalSampleCount);
				Entry->SetBoolField(TEXT("enabled"), AA->IsEnabled());
			}
			else if (Action.Equals(TEXT("add_setting"), ESearchCase::IgnoreCase)
				|| Action.Equals(TEXT("remove_setting"), ESearchCase::IgnoreCase)
				|| Action.Equals(TEXT("set_setting_enabled"), ESearchCase::IgnoreCase)
				|| Action.Equals(TEXT("set_setting_property"), ESearchCase::IgnoreCase))
			{
				FString SettingClassName;
				Op->TryGetStringField(TEXT("settingClass"), SettingClassName);
				if (SettingClassName.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("settingClass required"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				FString ResolveErr;
				UClass* SettingClass = ResolveMoviePipelineSettingClass(SettingClassName, ResolveErr);
				if (!SettingClass)
				{
					Entry->SetStringField(TEXT("error"), ResolveErr);
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}

				if (Action.Equals(TEXT("add_setting"), ESearchCase::IgnoreCase))
				{
					UMoviePipelineSetting* Added = Cfg->FindOrAddSettingByClass(SettingClass);
					if (!Added)
					{
						Entry->SetStringField(TEXT("error"), TEXT("FindOrAddSettingByClass failed"));
						OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
						continue;
					}
					bDirty = true;
					Entry->SetStringField(TEXT("settingClass"), Added->GetClass()->GetName());
				}
				else
				{
					UMoviePipelineSetting* Found = Cfg->FindSettingByClass(SettingClass);
					if (!Found)
					{
						Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Setting not found: %s"), *SettingClassName));
						OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
						continue;
					}
					if (Action.Equals(TEXT("remove_setting"), ESearchCase::IgnoreCase))
					{
						Cfg->RemoveSetting(Found);
						bDirty = true;
						Entry->SetStringField(TEXT("removed"), SettingClassName);
					}
					else if (Action.Equals(TEXT("set_setting_enabled"), ESearchCase::IgnoreCase))
					{
						bool bEnabled = true;
						if (!Op->HasField(TEXT("enabled")))
						{
							Entry->SetStringField(TEXT("error"), TEXT("set_setting_enabled requires enabled"));
							OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
							continue;
						}
						Op->TryGetBoolField(TEXT("enabled"), bEnabled);
						Found->SetIsEnabled(bEnabled);
						bDirty = true;
						Entry->SetBoolField(TEXT("enabled"), Found->IsEnabled());
						Entry->SetStringField(TEXT("settingClass"), Found->GetClass()->GetName());
					}
					else // set_setting_property
					{
						FString PropPath, Value;
						Op->TryGetStringField(TEXT("propertyPath"), PropPath);
						Op->TryGetStringField(TEXT("value"), Value);
						if (PropPath.IsEmpty() || Value.IsEmpty())
						{
							Entry->SetStringField(TEXT("error"), TEXT("set_setting_property requires propertyPath and value"));
							OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
							continue;
						}
						FString OldVal, ActualVal, Err;
						if (!FNexusPropertyUtils::WritePropertyAndEcho(Found, { PropPath }, 0, Value, OldVal, ActualVal, Err))
						{
							Entry->SetStringField(TEXT("error"), Err);
							OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
							continue;
						}
						bDirty = true;
						Entry->SetStringField(TEXT("settingClass"), Found->GetClass()->GetName());
						Entry->SetStringField(TEXT("propertyPath"), PropPath);
						if (!OldVal.IsEmpty()) Entry->SetStringField(TEXT("oldValue"), OldVal);
						if (!ActualVal.IsEmpty()) Entry->SetStringField(TEXT("newValue"), ActualVal);
					}
				}
			}
			else
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported operation: '%s'"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		if (bDirty) Cfg->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetMoviePipelineConfigCapability)
#endif
