// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Editor/NexusControlPieCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusRuntimeUtils.h"
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

void FNexusControlPieCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ControlPie");
	Out.Description = TEXT("Start/stop/pause/step PIE. Action=start|stop|status|pause|resume|step.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("PIE operation"), { TEXT("Start"), TEXT("Stop"), TEXT("Status"), TEXT("Pause"), TEXT("Resume"), TEXT("Step") }))
		.Prop(TEXT("Mode"),   FNexusSchema::Enum(TEXT("Play mode (start only)"), { TEXT("Viewport"), TEXT("Simulate") }, TEXT("Viewport")))
		.Required({ TEXT("Action") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("play"), TEXT("Pause"), TEXT("Simulate"), TEXT("game"), TEXT("preview") };
	Out.RelatedCapabilities = { TEXT("ExecCommand") };
}

FCapabilityResult FNexusControlPieCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString Action = A.Str(TEXT("Action"));

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

	#if WITH_EDITOR
		if (!GEditor)
		{
			OutError = TEXT("GEditor unavailable");
			return;
		}

		if (Action.Equals(TEXT("Status"), ESearchCase::IgnoreCase))
		{
			const bool bRunning      = GEditor->IsPlayingSessionInEditor();
			const bool bSimulating   = GEditor->IsSimulatingInEditor();
			const bool bHasPlayWorld = FNexusRuntimeUtils::HasPlayWorld();
			OutEntry->SetBoolField(TEXT("bIsPIERunning"), bRunning);
			OutEntry->SetBoolField(TEXT("bHasPlayWorld"), bHasPlayWorld);
			if (bSimulating) OutEntry->SetBoolField(TEXT("bIsPIESimulating"), true);
			FString State = TEXT("bStopped");
			if (bRunning)
			{
				if (!bHasPlayWorld)
				{
					State = TEXT("starting");
				}
				else if (bSimulating)
				{
					State = TEXT("simulating");
				}
				else
				{
					State = TEXT("bPlaying");
				}
			}
			OutEntry->SetStringField(TEXT("State"), State);
			if (bRunning && !bHasPlayWorld)
			{
				OutEntry->SetStringField(TEXT("Hint"),
					TEXT("PIE session started but play World is not ready; wait for bHasPlayWorld before list_runtime_*"));
			}
		}
		else if (Action.Equals(TEXT("Start"), ESearchCase::IgnoreCase))
		{
			if (GEditor->IsPlayingSessionInEditor())
			{
				OutEntry->SetBoolField(TEXT("bSuccess"), false);
				OutEntry->SetStringField(TEXT("Error"), TEXT("PIE already running; stop first"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}

			FString Mode = TEXT("Viewport");
			Mode = FNexusArgs(Arguments).Str(TEXT("Mode"), Mode);

			ULevelEditorPlaySettings* PlaySettings = GetMutableDefault<ULevelEditorPlaySettings>();
			const bool bSimulate = Mode.Equals(TEXT("Simulate"), ESearchCase::IgnoreCase);
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

			OutEntry->SetStringField(TEXT("Action"), TEXT("Start"));
			OutEntry->SetStringField(TEXT("Mode"), Mode.ToLower());
			OutEntry->SetStringField(TEXT("Note"), TEXT("PIE start requested; begins next frame."));
		}
		else if (Action.Equals(TEXT("Stop"), ESearchCase::IgnoreCase))
		{
			if (!GEditor->IsPlayingSessionInEditor())
			{
				OutEntry->SetBoolField(TEXT("bSuccess"), false);
				OutEntry->SetStringField(TEXT("Error"), TEXT("No running PIE session"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			GEditor->RequestEndPlayMap();
			OutEntry->SetStringField(TEXT("Action"), TEXT("Stop"));
			OutEntry->SetStringField(TEXT("Note"), TEXT("PIE stop requested."));
		}
		else if (Action.Equals(TEXT("Pause"), ESearchCase::IgnoreCase)
			|| Action.Equals(TEXT("Resume"), ESearchCase::IgnoreCase)
			|| Action.Equals(TEXT("Step"), ESearchCase::IgnoreCase))
		{
			if (!GEditor->IsPlayingSessionInEditor())
			{
				OutEntry->SetBoolField(TEXT("bSuccess"), false);
				OutEntry->SetStringField(TEXT("Error"), TEXT("No running PIE session"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				return;
			}
			if (Action.Equals(TEXT("Pause"), ESearchCase::IgnoreCase))
			{
#if NX_UE_HAS_SET_PIE_WORLDS_PAUSED
				GEditor->SetPIEWorldsPaused(true);
#else
				if (UWorld* PlayWorld = GEditor->PlayWorld)
				{
					PlayWorld->bDebugPauseExecution = true;
				}
#endif
				OutEntry->SetStringField(TEXT("State"), TEXT("paused"));
			}
			else if (Action.Equals(TEXT("Resume"), ESearchCase::IgnoreCase))
			{
#if NX_UE_HAS_SET_PIE_WORLDS_PAUSED
				GEditor->SetPIEWorldsPaused(false);
#else
				if (UWorld* PlayWorld = GEditor->PlayWorld)
				{
					PlayWorld->bDebugPauseExecution = false;
				}
#endif
				OutEntry->SetStringField(TEXT("State"), TEXT("bPlaying"));
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
				OutEntry->SetStringField(TEXT("State"), TEXT("Step"));
				OutEntry->SetStringField(TEXT("Note"), TEXT("Step requested (pause next frame)"));
			}
			OutEntry->SetStringField(TEXT("Action"), Action.ToLower());
		}
		else
		{
			OutEntry->SetBoolField(TEXT("bSuccess"), false);
			OutEntry->SetStringField(TEXT("Error"), FString::Printf(
				TEXT("Unknown action: %s; supported: start / stop / status / pause / resume / step"), *Action));
		}
	#else
		OutEntry->SetBoolField(TEXT("bSuccess"), false);
		OutEntry->SetStringField(TEXT("Error"), TEXT("ControlPie only available in editor mode"));
	#endif

		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusControlPieCapability)

#endif // WITH_EDITOR
