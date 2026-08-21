// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/NexusControlPieCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusVersionCompat.h"

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
	Out.Description = TEXT("Start/stop/pause/step PIE. action=start|stop|status|pause|resume|step.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("PIE operation"), { TEXT("start"), TEXT("stop"), TEXT("status"), TEXT("pause"), TEXT("resume"), TEXT("step") }))
		.Prop(TEXT("mode"),   FNexusSchema::Enum(TEXT("Play mode (start only)"), { TEXT("viewport"), TEXT("simulate") }, TEXT("viewport")))
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
		const FNexusArgs A(Arguments);

		const FString Action = A.Str(TEXT("action"));

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

	#if WITH_EDITOR
		if (!GEditor)
		{
			OutError = TEXT("GEditor unavailable");
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
		}
		else if (Action.Equals(TEXT("start"), ESearchCase::IgnoreCase))
		{
			if (GEditor->IsPlayingSessionInEditor())
			{
				OutEntry->SetBoolField(TEXT("success"), false);
				OutEntry->SetStringField(TEXT("error"), TEXT("PIE already running; stop first"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}

			FString Mode = TEXT("viewport");
			Mode = FNexusArgs(Arguments).Str(TEXT("mode"), Mode);

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

			OutEntry->SetStringField(TEXT("action"), TEXT("start"));
			OutEntry->SetStringField(TEXT("mode"), Mode.ToLower());
			OutEntry->SetStringField(TEXT("note"), TEXT("PIE start requested; begins next frame."));
		}
		else if (Action.Equals(TEXT("stop"), ESearchCase::IgnoreCase))
		{
			if (!GEditor->IsPlayingSessionInEditor())
			{
				OutEntry->SetBoolField(TEXT("success"), false);
				OutEntry->SetStringField(TEXT("error"), TEXT("No running PIE session"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			GEditor->RequestEndPlayMap();
			OutEntry->SetStringField(TEXT("action"), TEXT("stop"));
			OutEntry->SetStringField(TEXT("note"), TEXT("PIE stop requested."));
		}
		else if (Action.Equals(TEXT("pause"), ESearchCase::IgnoreCase)
			|| Action.Equals(TEXT("resume"), ESearchCase::IgnoreCase)
			|| Action.Equals(TEXT("step"), ESearchCase::IgnoreCase))
		{
			if (!GEditor->IsPlayingSessionInEditor())
			{
				OutEntry->SetBoolField(TEXT("success"), false);
				OutEntry->SetStringField(TEXT("error"), TEXT("No running PIE session"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			if (Action.Equals(TEXT("pause"), ESearchCase::IgnoreCase))
			{
#if NX_UE_HAS_SET_PIE_WORLDS_PAUSED
				GEditor->SetPIEWorldsPaused(true);
#else
				if (UWorld* PlayWorld = GEditor->PlayWorld)
				{
					PlayWorld->bDebugPauseExecution = true;
				}
#endif
				OutEntry->SetStringField(TEXT("state"), TEXT("paused"));
			}
			else if (Action.Equals(TEXT("resume"), ESearchCase::IgnoreCase))
			{
#if NX_UE_HAS_SET_PIE_WORLDS_PAUSED
				GEditor->SetPIEWorldsPaused(false);
#else
				if (UWorld* PlayWorld = GEditor->PlayWorld)
				{
					PlayWorld->bDebugPauseExecution = false;
				}
#endif
				OutEntry->SetStringField(TEXT("state"), TEXT("playing"));
			}
			else
			{
#if NX_UE_HAS_SET_PIE_WORLDS_PAUSED
				GEditor->SetPIEWorldsPaused(false);
#endif
				if (UWorld* PlayWorld = GEditor->PlayWorld)
				{
					PlayWorld->bDebugPauseExecution = true;
				}
				OutEntry->SetStringField(TEXT("state"), TEXT("step"));
				OutEntry->SetStringField(TEXT("note"), TEXT("Step requested (pause next frame)"));
			}
			OutEntry->SetStringField(TEXT("action"), Action.ToLower());
		}
		else
		{
			OutEntry->SetBoolField(TEXT("success"), false);
			OutEntry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Unknown action: %s; supported: start / stop / status / pause / resume / step"), *Action));
		}
	#else
		OutEntry->SetBoolField(TEXT("success"), false);
		OutEntry->SetStringField(TEXT("error"), TEXT("control_pie only available in editor mode"));
	#endif

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FControlPieCapability)

#endif // WITH_EDITOR
