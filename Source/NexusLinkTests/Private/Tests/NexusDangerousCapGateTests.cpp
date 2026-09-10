// Copyright byteyang. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "Dom/JsonObject.h"
#include "NexusLinkSettings.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpTool.h"
#include "Utils/NexusDangerousCapGate.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusDangerousCapTagTest,
	"NexusLink.DangerousCap.Tags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusDangerousCapTagTest::RunTest(const FString& Parameters)
{
	const TCHAR* Names[] = {
		TEXT("exec_command"),
		TEXT("eval_runtime_lua"),
		TEXT("dofile_runtime_lua"),
		TEXT("exec_python"),
	};
	int32 Found = 0;
	for (const TCHAR* Name : Names)
	{
		const FCapRecord* Rec = FNexusCapabilityRegistry::Get().FindRecordByName(Name);
		if (!Rec)
		{
			continue;
		}
		++Found;
		TestTrue(FString::Printf(TEXT("%s has dangerous tag"), Name),
			Rec->Def.HasTag(FNexusMcpTags::Dangerous));
		TestTrue(FString::Printf(TEXT("%s has write tag"), Name),
			Rec->Def.HasTag(FNexusMcpTags::Write));
		TestTrue(FString::Printf(TEXT("IsDangerous(%s)"), Name),
			FNexusDangerousCapGate::IsDangerous(Name));
		const TSharedPtr<FJsonObject>* Props = nullptr;
		TestTrue(FString::Printf(TEXT("%s schema has reason"), Name),
			Rec->Def.InputSchema.IsValid()
			&& Rec->Def.InputSchema->TryGetObjectField(TEXT("properties"), Props)
			&& Props && (*Props)->HasField(TEXT("reason")));
	}
	TestTrue(TEXT("at least exec_command is registered"), Found >= 1);
	TestFalse(TEXT("search_asset is not dangerous"),
		FNexusDangerousCapGate::IsDangerous(TEXT("search_asset")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusDangerousCapReasonTest,
	"NexusLink.DangerousCap.ValidateReason",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusDangerousCapReasonTest::RunTest(const FString& Parameters)
{
	FString Err;
	TestFalse(TEXT("empty reason fails"),
		FNexusDangerousCapGate::ValidateReason(TEXT(""), TEXT("command: stat fps"), Err));
	TestFalse(TEXT("short reason fails"),
		FNexusDangerousCapGate::ValidateReason(TEXT("too short"), TEXT("command: stat fps"), Err));
	TestFalse(TEXT("payload echo fails"),
		FNexusDangerousCapGate::ValidateReason(TEXT("command: stat fps"), TEXT("command: stat fps"), Err));
	TestTrue(TEXT("good reason passes"),
		FNexusDangerousCapGate::ValidateReason(
			TEXT("Need engine FPS overlay because no dedicated capability exists for toggling stat hud."),
			TEXT("command: stat fps"), Err));

	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("command"), TEXT("stat fps"));
	Args->SetStringField(TEXT("mode"), TEXT("exec"));
	const FString Payload = FNexusDangerousCapGate::ExtractPayload(Args);
	TestTrue(TEXT("payload contains command"), Payload.Contains(TEXT("stat fps")));
	TestTrue(TEXT("payload contains mode"), Payload.Contains(TEXT("exec")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusDangerousCapAccessMatrixTest,
	"NexusLink.DangerousCap.AccessMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusDangerousCapAccessMatrixTest::RunTest(const FString& Parameters)
{
	UNexusLinkSettings* S = UNexusLinkSettings::Get();
	if (!S)
	{
		AddError(TEXT("settings missing"));
		return false;
	}
	const ENexusDangerousCapAccess SavedAccess = S->DangerousCapAccess;
	const TSet<FString> SavedDisabled = S->DisabledCapabilities;
	const TSet<FString> SavedSession = S->SessionEnabledCapabilities;

	const FString Cap = TEXT("exec_command");
	S->SessionEnabledCapabilities.Empty();
	S->DisabledCapabilities.Add(Cap);

	S->DangerousCapAccess = ENexusDangerousCapAccess::Disabled;
	S->DisabledCapabilities.Remove(Cap);
	TestFalse(TEXT("Disabled mode does not enable dangerous cap"), S->IsCapabilityEnabled(Cap));
	TestTrue(TEXT("Disabled mode still shows stored check"), S->IsCapabilityCheckedInTree(Cap));
	TestFalse(TEXT("Disabled does not need confirm"), FNexusDangerousCapGate::NeedsConfirm(Cap));

	S->DangerousCapAccess = ENexusDangerousCapAccess::Confirm;
	S->DisabledCapabilities.Add(Cap);
	TestFalse(TEXT("Confirm respects uncheck"), S->IsCapabilityEnabled(Cap));
	TestFalse(TEXT("unchecked Confirm does not need confirm"), FNexusDangerousCapGate::NeedsConfirm(Cap));
	S->DisabledCapabilities.Remove(Cap);
	TestTrue(TEXT("Confirm checked is enabled"), S->IsCapabilityEnabled(Cap));
	TestTrue(TEXT("Confirm needs confirm"), FNexusDangerousCapGate::NeedsConfirm(Cap));

	S->DangerousCapAccess = ENexusDangerousCapAccess::Custom;
	S->DisabledCapabilities.Add(Cap);
	TestFalse(TEXT("Custom respects DisabledCapabilities"), S->IsCapabilityEnabled(Cap));
	S->DisabledCapabilities.Remove(Cap);
	TestTrue(TEXT("Custom enabled when not disabled"), S->IsCapabilityEnabled(Cap));
	TestFalse(TEXT("Custom does not need confirm"), FNexusDangerousCapGate::NeedsConfirm(Cap));

	S->DangerousCapAccess = ENexusDangerousCapAccess::Disabled;
	S->SessionEnabledCapabilities.Add(Cap);
	TestTrue(TEXT("session override wins"), S->IsCapabilityEnabled(Cap));
	TestFalse(TEXT("session override skips confirm"), FNexusDangerousCapGate::NeedsConfirm(Cap));

	S->DangerousCapAccess = SavedAccess;
	S->DisabledCapabilities = SavedDisabled;
	S->SessionEnabledCapabilities = SavedSession;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusDangerousCapUpgradeTest,
	"NexusLink.DangerousCap.UpgradeResolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusDangerousCapUpgradeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("all disabled → Disabled"),
		static_cast<uint8>(UNexusLinkSettings::ResolveAccessAfterUpgrade(false)),
		static_cast<uint8>(ENexusDangerousCapAccess::Disabled));
	TestEqual(TEXT("any enabled → Custom"),
		static_cast<uint8>(UNexusLinkSettings::ResolveAccessAfterUpgrade(true)),
		static_cast<uint8>(ENexusDangerousCapAccess::Custom));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusDangerousCapConfirmOrDenyTest,
	"NexusLink.DangerousCap.ConfirmOrDeny",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusDangerousCapConfirmOrDenyTest::RunTest(const FString& Parameters)
{
	UNexusLinkSettings* S = UNexusLinkSettings::Get();
	if (!S)
	{
		AddError(TEXT("settings missing"));
		return false;
	}
	const ENexusDangerousCapAccess SavedAccess = S->DangerousCapAccess;
	const TSet<FString> SavedDisabled = S->DisabledCapabilities;
	const TSet<FString> SavedSession = S->SessionEnabledCapabilities;

	S->SessionEnabledCapabilities.Empty();
	S->DangerousCapAccess = ENexusDangerousCapAccess::Confirm;
	S->DisabledCapabilities.Remove(TEXT("exec_command"));
	S->DisabledCapabilities.Remove(TEXT("eval_runtime_lua"));

	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("command"), TEXT("stat fps"));
	Args->SetStringField(TEXT("reason"), TEXT("short"));
	const FCapabilityResult Weak = FNexusDangerousCapGate::ConfirmOrDeny(TEXT("exec_command"), Args);
	TestTrue(TEXT("weak reason is arg_invalid"), Weak.bIsArgInvalid);
	TestTrue(TEXT("weak reason skips feedback"), Weak.bSkipFeedback);
	TestFalse(TEXT("weak reason is not user_denied"), Weak.bIsUserDenied);

	Args->SetStringField(TEXT("reason"),
		TEXT("Need engine FPS overlay because no dedicated capability exists for toggling stat hud."));

	FNexusDangerousCapGate::FBatchScope Batch;
	if (FApp::IsUnattended() || IsRunningCommandlet())
	{
		const FCapabilityResult First = FNexusDangerousCapGate::ConfirmOrDeny(TEXT("exec_command"), Args);
		TestTrue(TEXT("unattended Confirm → user_denied"), First.bIsUserDenied);
		const FCapabilityResult Second = FNexusDangerousCapGate::ConfirmOrDeny(TEXT("eval_runtime_lua"), Args);
		TestTrue(TEXT("batch skip after first deny"), Second.bIsUserDenied);
		TestTrue(TEXT("batch skip mentions previous"), Second.FatalError.Contains(TEXT("batch")));
	}
	else
	{
		AddInfo(TEXT("skip UI confirm / batch-deny (attended editor; would pop a window)"));
	}

	S->DangerousCapAccess = SavedAccess;
	S->DisabledCapabilities = SavedDisabled;
	S->SessionEnabledCapabilities = SavedSession;
	return true;
}
