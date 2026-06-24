// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/NexusControlPieCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "LevelEditor.h"
#include "IAssetViewport.h"
#include "NexusMcpTool.h"
#endif

void FControlPieCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("control_pie");
	Out.Description = TEXT("启动/停止/查询 PIE。action=start|stop|status。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("PIE 操作"), { TEXT("start"), TEXT("stop"), TEXT("status") }))
		.Prop(TEXT("mode"),   FNexusSchema::Enum(TEXT("播放模式（仅 start）"), { TEXT("viewport"), TEXT("simulate") }, TEXT("viewport")))
		.Required({ TEXT("action") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("play"), TEXT("pause"), TEXT("simulate"), TEXT("game"), TEXT("preview") };
	Out.RelatedCapabilities = { TEXT("exec_command") };
}

FCapabilityResult FControlPieCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		FString Action;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
		{ OutError = TEXT("缺少必填参数 action"); return; }

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

	#if WITH_EDITOR
		if (!GEditor)
		{
			OutError = TEXT("GEditor 不可用");
			return;
		}

		if (Action.Equals(TEXT("status"), ESearchCase::IgnoreCase))
		{
			const bool bRunning    = GEditor->IsPlayingSessionInEditor();
			const bool bSimulating = GEditor->IsSimulatingInEditor();
			OutEntry->SetBoolField(TEXT("isPIERunning"), bRunning);
			if (bSimulating) OutEntry->SetBoolField(TEXT("isPIESimulating"), true);
			OutEntry->SetStringField(TEXT("state"), bRunning
				? (bSimulating ? TEXT("simulating") : TEXT("playing")) : TEXT("stopped"));
			OutEntry->SetBoolField(TEXT("success"), true);
		}
		else if (Action.Equals(TEXT("start"), ESearchCase::IgnoreCase))
		{
			if (GEditor->IsPlayingSessionInEditor())
			{
				OutEntry->SetBoolField(TEXT("success"), false);
				OutEntry->SetStringField(TEXT("error"), TEXT("PIE 已在运行；请先 stop"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}

			FString Mode = TEXT("viewport");
			Arguments->TryGetStringField(TEXT("mode"), Mode);

			ULevelEditorPlaySettings* PlaySettings = GetMutableDefault<ULevelEditorPlaySettings>();
			const bool bSimulate = Mode.Equals(TEXT("simulate"), ESearchCase::IgnoreCase);
			PlaySettings->LastExecutedPlayModeType = bSimulate ? PlayMode_Simulate : PlayMode_InViewPort;
			PlaySettings->SetPlayNetMode(PIE_Standalone);
			PlaySettings->PostEditChange();
			PlaySettings->SaveConfig();

			FRequestPlaySessionParams Params;
			if (bSimulate)
			{
				Params.WorldType = EPlaySessionWorldType::SimulateInEditor;
			}
			else
			{
				if (FLevelEditorModule* LEModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor"))
				{
					TSharedPtr<IAssetViewport> ActiveViewport = LEModule->GetFirstActiveViewport();
					if (ActiveViewport.IsValid())
						Params.DestinationSlateViewport = ActiveViewport;
				}
			}
			GEditor->RequestPlaySession(Params);

			OutEntry->SetBoolField(TEXT("success"), true);
			OutEntry->SetStringField(TEXT("action"), TEXT("start"));
			OutEntry->SetStringField(TEXT("mode"), Mode.ToLower());
			OutEntry->SetStringField(TEXT("note"), TEXT("已请求启动 PIE；将在下一帧实际开始。"));
		}
		else if (Action.Equals(TEXT("stop"), ESearchCase::IgnoreCase))
		{
			if (!GEditor->IsPlayingSessionInEditor())
			{
				OutEntry->SetBoolField(TEXT("success"), false);
				OutEntry->SetStringField(TEXT("error"), TEXT("无运行中的 PIE 会话"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			GEditor->RequestEndPlayMap();
			OutEntry->SetBoolField(TEXT("success"), true);
			OutEntry->SetStringField(TEXT("action"), TEXT("stop"));
			OutEntry->SetStringField(TEXT("note"), TEXT("已请求停止 PIE。"));
		}
		else
		{
			OutEntry->SetBoolField(TEXT("success"), false);
			OutEntry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Unknown action: %s; supported: start / stop / status"), *Action));
		}
	#else
		OutEntry->SetBoolField(TEXT("success"), false);
		OutEntry->SetStringField(TEXT("error"), TEXT("control_pie 仅在编辑器模式可用"));
	#endif

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FControlPieCapability)

#endif // WITH_EDITOR
