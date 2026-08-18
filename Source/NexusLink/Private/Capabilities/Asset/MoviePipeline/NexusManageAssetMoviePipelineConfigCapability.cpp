// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MoviePipeline/NexusManageAssetMoviePipelineConfigCapability.h"
#if WITH_MOVIE_RENDER_PIPELINE
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
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

static UClass* ResolveMoviePipelineSettingClass(const FString& InName, FString& OutError)
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

static FNexusMoviePipelineConfig* CfgFrom(FNexusActionContext& Ctx)
{
	return static_cast<FNexusMoviePipelineConfig*>(Ctx.Target);
}

static void MarkCfgDirty(FNexusActionContext& Ctx)
{
	if (FNexusMoviePipelineConfig* Cfg = CfgFrom(Ctx))
	{
		Cfg->MarkPackageDirty();
	}
}

static void HandleMRQ_SetOutput(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FNexusMoviePipelineConfig* Cfg = CfgFrom(Ctx);
	UMoviePipelineOutputSetting* OutSet = Cfg->FindSetting<UMoviePipelineOutputSetting>();
	if (!OutSet)
	{
		OutSet = Cast<UMoviePipelineOutputSetting>(
			Cfg->FindOrAddSettingByClass(UMoviePipelineOutputSetting::StaticClass()));
	}
	if (!OutSet)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("no OutputSetting"));
		return;
	}
	const FNexusArgs A(Op);
	const FString Dir = A.Str(TEXT("directory"));
	const FString Fmt = A.Str(TEXT("fileNameFormat"));
	if (!Dir.IsEmpty()) OutSet->OutputDirectory.Path = Dir;
	if (!Fmt.IsEmpty()) OutSet->FileNameFormat = Fmt;
	if (Op->HasField(TEXT("width")) || Op->HasField(TEXT("height")))
	{
		FIntPoint Res = OutSet->OutputResolution;
		if (Op->HasField(TEXT("width"))) Res.X = static_cast<int32>(A.Num(TEXT("width")));
		if (Op->HasField(TEXT("height"))) Res.Y = static_cast<int32>(A.Num(TEXT("height")));
		OutSet->OutputResolution = Res;
	}
	MarkCfgDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("directory"), OutSet->OutputDirectory.Path);
	Ctx.Entry->SetNumberField(TEXT("width"), OutSet->OutputResolution.X);
	Ctx.Entry->SetNumberField(TEXT("height"), OutSet->OutputResolution.Y);
	Ctx.Entry->SetStringField(TEXT("fileNameFormat"), OutSet->FileNameFormat);
}

static void HandleMRQ_SetAntiAliasing(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FNexusMoviePipelineConfig* Cfg = CfgFrom(Ctx);
	UMoviePipelineAntiAliasingSetting* AA = Cast<UMoviePipelineAntiAliasingSetting>(
		Cfg->FindOrAddSettingByClass(UMoviePipelineAntiAliasingSetting::StaticClass()));
	if (!AA)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Unable to add AntiAliasingSetting"));
		return;
	}
	const FNexusArgs A(Op);
	if (Op->HasField(TEXT("spatialSampleCount")))
		AA->SpatialSampleCount = FMath::Max(1, static_cast<int32>(A.Num(TEXT("spatialSampleCount"))));
	if (Op->HasField(TEXT("temporalSampleCount")))
		AA->TemporalSampleCount = FMath::Max(1, static_cast<int32>(A.Num(TEXT("temporalSampleCount"))));
	if (Op->HasField(TEXT("enabled")))
	{
		AA->SetIsEnabled(A.Bool(TEXT("enabled"), true));
	}
	MarkCfgDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("spatialSampleCount"), AA->SpatialSampleCount);
	Ctx.Entry->SetNumberField(TEXT("temporalSampleCount"), AA->TemporalSampleCount);
	Ctx.Entry->SetBoolField(TEXT("enabled"), AA->IsEnabled());
}

static UMoviePipelineSetting* ResolveSettingOrError(
	FNexusMoviePipelineConfig* Cfg, const TSharedPtr<FJsonObject>& Op, TSharedPtr<FJsonObject>& Entry, FString& OutClassName)
{
	OutClassName = FNexusArgs(Op).Str(TEXT("settingClass"));
	if (OutClassName.IsEmpty())
	{
		Entry->SetStringField(TEXT("error"), TEXT("settingClass required"));
		return nullptr;
	}
	FString ResolveErr;
	UClass* SettingClass = ResolveMoviePipelineSettingClass(OutClassName, ResolveErr);
	if (!SettingClass)
	{
		Entry->SetStringField(TEXT("error"), ResolveErr);
		return nullptr;
	}
	UMoviePipelineSetting* Found = Cfg->FindSettingByClass(SettingClass);
	if (!Found)
	{
		Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Setting not found: %s"), *OutClassName));
		return nullptr;
	}
	return Found;
}

static void HandleMRQ_AddSetting(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FNexusMoviePipelineConfig* Cfg = CfgFrom(Ctx);
	const FString SettingClassName = FNexusArgs(Op).Str(TEXT("settingClass"));
	if (SettingClassName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("settingClass required"));
		return;
	}
	FString ResolveErr;
	UClass* SettingClass = ResolveMoviePipelineSettingClass(SettingClassName, ResolveErr);
	if (!SettingClass)
	{
		Ctx.Entry->SetStringField(TEXT("error"), ResolveErr);
		return;
	}
	UMoviePipelineSetting* Added = Cfg->FindOrAddSettingByClass(SettingClass);
	if (!Added)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("FindOrAddSettingByClass failed"));
		return;
	}
	MarkCfgDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("settingClass"), Added->GetClass()->GetName());
}

static void HandleMRQ_RemoveSetting(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FString SettingClassName;
	UMoviePipelineSetting* Found = ResolveSettingOrError(CfgFrom(Ctx), Op, Ctx.Entry, SettingClassName);
	if (!Found) return;
	CfgFrom(Ctx)->RemoveSetting(Found);
	MarkCfgDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("removed"), SettingClassName);
}

static void HandleMRQ_SetSettingEnabled(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FString SettingClassName;
	UMoviePipelineSetting* Found = ResolveSettingOrError(CfgFrom(Ctx), Op, Ctx.Entry, SettingClassName);
	if (!Found) return;
	if (!Op->HasField(TEXT("enabled")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_setting_enabled requires enabled"));
		return;
	}
	Found->SetIsEnabled(FNexusArgs(Op).Bool(TEXT("enabled"), true));
	MarkCfgDirty(Ctx);
	Ctx.Entry->SetBoolField(TEXT("enabled"), Found->IsEnabled());
	Ctx.Entry->SetStringField(TEXT("settingClass"), Found->GetClass()->GetName());
}

static void HandleMRQ_SetSettingProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FString SettingClassName;
	UMoviePipelineSetting* Found = ResolveSettingOrError(CfgFrom(Ctx), Op, Ctx.Entry, SettingClassName);
	if (!Found) return;
	const FNexusArgs A(Op);
	const FString PropPath = A.Str(TEXT("propertyPath"));
	const FString Value = A.Str(TEXT("value"));
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_setting_property requires propertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Found, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("error"), Err);
		return;
	}
	MarkCfgDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("settingClass"), Found->GetClass()->GetName());
	Ctx.Entry->SetStringField(TEXT("propertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("oldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("newValue"), ActualVal);
}

bool FManageAssetMoviePipelineConfigCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	FNexusMoviePipelineConfig* Cfg = FNexusAssetUtils::LoadAssetWithFallback<FNexusMoviePipelineConfig>(AssetPath);
	if (!Cfg)
	{
		OutError = FString::Printf(TEXT("Failed to load MoviePipeline config: %s"), *AssetPath);
		return false;
	}
	OutTarget = Cfg;
	return true;
}

void FManageAssetMoviePipelineConfigCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_output"),            &HandleMRQ_SetOutput);
	OutHandlers.Add(TEXT("set_anti_aliasing"),     &HandleMRQ_SetAntiAliasing);
	OutHandlers.Add(TEXT("add_setting"),           &HandleMRQ_AddSetting);
	OutHandlers.Add(TEXT("remove_setting"),        &HandleMRQ_RemoveSetting);
	OutHandlers.Add(TEXT("set_setting_enabled"),   &HandleMRQ_SetSettingEnabled);
	OutHandlers.Add(TEXT("set_setting_property"),  &HandleMRQ_SetSettingProperty);
}

REGISTER_MCP_CAPABILITY(FManageAssetMoviePipelineConfigCapability)
#endif
