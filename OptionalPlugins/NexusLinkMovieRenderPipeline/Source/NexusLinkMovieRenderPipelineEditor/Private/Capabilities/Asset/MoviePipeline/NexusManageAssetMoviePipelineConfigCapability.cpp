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
			TEXT("Unknown SettingClass: %s (Output/AntiAliasing/HighRes/Camera/GameOverride/Color/Debug)"),
			*InName);
	return nullptr;
}

void FNexusManageAssetMoviePipelineConfigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetMoviePipelineConfig");
	Out.SearchAssetTypes = {TEXT("MoviePipelinePrimaryConfig"), TEXT("MoviePipelineMasterConfig")};
	Out.Description = TEXT("Batch edit MoviePipeline settings. Does not trigger render.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"), {
			TEXT("SetOutput"), TEXT("SetAntiAliasing"),
			TEXT("AddSetting"), TEXT("RemoveSetting"),
			TEXT("SetSettingProperty"), TEXT("SetSettingEnabled")
		}))
		.Prop(TEXT("Directory"), FNexusSchema::Str(TEXT("Output directory (SetOutput)")))
		.Prop(TEXT("Width"), FNexusSchema::Int(TEXT("Output width")))
		.Prop(TEXT("Height"), FNexusSchema::Int(TEXT("Output height")))
		.Prop(TEXT("FileNameFormat"), FNexusSchema::Str(TEXT("Output filename format (SetOutput)")))
		.Prop(TEXT("SpatialSampleCount"), FNexusSchema::Int(TEXT("Spatial samples (SetAntiAliasing)")))
		.Prop(TEXT("TemporalSampleCount"), FNexusSchema::Int(TEXT("Temporal samples (SetAntiAliasing)")))
		.Prop(TEXT("SettingClass"), FNexusSchema::Enum(TEXT("Setting class short name"), {
			TEXT("Output"), TEXT("AntiAliasing"), TEXT("HighRes"), TEXT("Camera"),
			TEXT("GameOverride"), TEXT("Color"), TEXT("Debug")
		}))
		.Prop(TEXT("PropertyPath"), FNexusSchema::Str(TEXT("Setting object property path (SetSettingProperty)")))
		.Prop(TEXT("Value"), FNexusSchema::Str(TEXT("New property value (SetSettingProperty)")))
		.Prop(TEXT("bEnabled"), FNexusSchema::Bool(TEXT("Setting enabled flag (SetSettingEnabled)")))
		.Required({ TEXT("Action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("MoviePipeline config asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("mrq"), TEXT("Output"), TEXT("resolution"), TEXT("aa"), TEXT("setting") };
	Out.RelatedCapabilities = {
		TEXT("GetAssetMoviePipelineConfig"), TEXT("CreateAssetMoviePipelineConfig"),
		TEXT("ControlMoviePipeline")
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
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("no OutputSetting"));
		return;
	}
	const FNexusArgs A(Op);
	const FString Dir = A.Str(TEXT("Directory"));
	const FString Fmt = A.Str(TEXT("FileNameFormat"));
	if (!Dir.IsEmpty()) OutSet->OutputDirectory.Path = Dir;
	if (!Fmt.IsEmpty()) OutSet->FileNameFormat = Fmt;
	if (Op->HasField(TEXT("Width")) || Op->HasField(TEXT("Height")))
	{
		FIntPoint Res = OutSet->OutputResolution;
		if (Op->HasField(TEXT("Width"))) Res.X = static_cast<int32>(A.Num(TEXT("Width")));
		if (Op->HasField(TEXT("Height"))) Res.Y = static_cast<int32>(A.Num(TEXT("Height")));
		OutSet->OutputResolution = Res;
	}
	MarkCfgDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("Directory"), OutSet->OutputDirectory.Path);
	Ctx.Entry->SetNumberField(TEXT("Width"), OutSet->OutputResolution.X);
	Ctx.Entry->SetNumberField(TEXT("Height"), OutSet->OutputResolution.Y);
	Ctx.Entry->SetStringField(TEXT("FileNameFormat"), OutSet->FileNameFormat);
}

static void HandleMRQ_SetAntiAliasing(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FNexusMoviePipelineConfig* Cfg = CfgFrom(Ctx);
	UMoviePipelineAntiAliasingSetting* AA = Cast<UMoviePipelineAntiAliasingSetting>(
		Cfg->FindOrAddSettingByClass(UMoviePipelineAntiAliasingSetting::StaticClass()));
	if (!AA)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Unable to add AntiAliasingSetting"));
		return;
	}
	const FNexusArgs A(Op);
	if (Op->HasField(TEXT("SpatialSampleCount")))
		AA->SpatialSampleCount = FMath::Max(1, static_cast<int32>(A.Num(TEXT("SpatialSampleCount"))));
	if (Op->HasField(TEXT("TemporalSampleCount")))
		AA->TemporalSampleCount = FMath::Max(1, static_cast<int32>(A.Num(TEXT("TemporalSampleCount"))));
	if (Op->HasField(TEXT("bEnabled")))
	{
		AA->SetIsEnabled(A.Bool(TEXT("bEnabled"), true));
	}
	MarkCfgDirty(Ctx);
	Ctx.Entry->SetNumberField(TEXT("SpatialSampleCount"), AA->SpatialSampleCount);
	Ctx.Entry->SetNumberField(TEXT("TemporalSampleCount"), AA->TemporalSampleCount);
	Ctx.Entry->SetBoolField(TEXT("bEnabled"), AA->IsEnabled());
}

static UMoviePipelineSetting* ResolveSettingOrError(
	FNexusMoviePipelineConfig* Cfg, const TSharedPtr<FJsonObject>& Op, TSharedPtr<FJsonObject>& Entry, FString& OutClassName)
{
	OutClassName = FNexusArgs(Op).Str(TEXT("SettingClass"));
	if (OutClassName.IsEmpty())
	{
		Entry->SetStringField(TEXT("Error"), TEXT("SettingClass required"));
		return nullptr;
	}
	FString ResolveErr;
	UClass* SettingClass = ResolveMoviePipelineSettingClass(OutClassName, ResolveErr);
	if (!SettingClass)
	{
		Entry->SetStringField(TEXT("Error"), ResolveErr);
		return nullptr;
	}
	UMoviePipelineSetting* Found = Cfg->FindSettingByClass(SettingClass);
	if (!Found)
	{
		Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Setting not found: %s"), *OutClassName));
		return nullptr;
	}
	return Found;
}

static void HandleMRQ_AddSetting(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FNexusMoviePipelineConfig* Cfg = CfgFrom(Ctx);
	const FString SettingClassName = FNexusArgs(Op).Str(TEXT("SettingClass"));
	if (SettingClassName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SettingClass required"));
		return;
	}
	FString ResolveErr;
	UClass* SettingClass = ResolveMoviePipelineSettingClass(SettingClassName, ResolveErr);
	if (!SettingClass)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), ResolveErr);
		return;
	}
	UMoviePipelineSetting* Added = Cfg->FindOrAddSettingByClass(SettingClass);
	if (!Added)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("FindOrAddSettingByClass failed"));
		return;
	}
	MarkCfgDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("SettingClass"), Added->GetClass()->GetName());
}

static void HandleMRQ_RemoveSetting(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FString SettingClassName;
	UMoviePipelineSetting* Found = ResolveSettingOrError(CfgFrom(Ctx), Op, Ctx.Entry, SettingClassName);
	if (!Found) return;
	CfgFrom(Ctx)->RemoveSetting(Found);
	MarkCfgDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("bRemoved"), SettingClassName);
}

static void HandleMRQ_SetSettingEnabled(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FString SettingClassName;
	UMoviePipelineSetting* Found = ResolveSettingOrError(CfgFrom(Ctx), Op, Ctx.Entry, SettingClassName);
	if (!Found) return;
	if (!Op->HasField(TEXT("bEnabled")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetSettingEnabled requires enabled"));
		return;
	}
	Found->SetIsEnabled(FNexusArgs(Op).Bool(TEXT("bEnabled"), true));
	MarkCfgDirty(Ctx);
	Ctx.Entry->SetBoolField(TEXT("bEnabled"), Found->IsEnabled());
	Ctx.Entry->SetStringField(TEXT("SettingClass"), Found->GetClass()->GetName());
}

static void HandleMRQ_SetSettingProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	FString SettingClassName;
	UMoviePipelineSetting* Found = ResolveSettingOrError(CfgFrom(Ctx), Op, Ctx.Entry, SettingClassName);
	if (!Found) return;
	const FNexusArgs A(Op);
	const FString PropPath = A.Str(TEXT("PropertyPath"));
	const FString Value = A.Str(TEXT("Value"));
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetSettingProperty requires PropertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Found, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), Err);
		return;
	}
	MarkCfgDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("SettingClass"), Found->GetClass()->GetName());
	Ctx.Entry->SetStringField(TEXT("PropertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("OldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("NewValue"), ActualVal);
}

bool FNexusManageAssetMoviePipelineConfigCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	FNexusMoviePipelineConfig* Cfg = FNexusAssetUtils::LoadAssetWithFallback<FNexusMoviePipelineConfig>(AssetPath);
	if (!Cfg)
	{
		OutError = FString::Printf(TEXT("Failed to load MoviePipeline config: %s"), *AssetPath);
		return false;
	}
	OutTarget = Cfg;
	return true;
}

void FNexusManageAssetMoviePipelineConfigCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetOutput"),            &HandleMRQ_SetOutput);
	OutHandlers.Add(TEXT("SetAntiAliasing"),     &HandleMRQ_SetAntiAliasing);
	OutHandlers.Add(TEXT("AddSetting"),           &HandleMRQ_AddSetting);
	OutHandlers.Add(TEXT("RemoveSetting"),        &HandleMRQ_RemoveSetting);
	OutHandlers.Add(TEXT("SetSettingEnabled"),   &HandleMRQ_SetSettingEnabled);
	OutHandlers.Add(TEXT("SetSettingProperty"),  &HandleMRQ_SetSettingProperty);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetMoviePipelineConfigCapability)
#endif
