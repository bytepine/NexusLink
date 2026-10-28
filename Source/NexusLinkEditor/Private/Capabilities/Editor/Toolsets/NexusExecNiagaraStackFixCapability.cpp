// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusExecNiagaraStackFixCapability.h"
#if WITH_NEXUS_TOOLSETS
#include "Utils/NexusEditorToolSchema.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"

void FNexusExecNiagaraStackFixCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ExecNiagaraStackFix");
	Out.Description = TEXT("Apply one exact fix returned by GetEditorNiagara StackIssues, then wait for native validation. Uses the supplied system and issue/fix IDs.");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("NiagaraSystem asset object path")))
		.Required(TEXT("IssueId"), FNexusSchema::Str(TEXT("IssueId from a fresh StackIssues query")))
		.Required(TEXT("FixId"), FNexusSchema::Str(TEXT("FixId belonging to that issue"))).Build();
	FNexusEditorToolSchema::ConfigureNative(Out, true, TEXT("NiagaraToolsets"));
	Out.SearchAssetTypes = {TEXT("NiagaraSystem")};
	Out.RelatedCapabilities = {TEXT("GetEditorNiagara"), TEXT("GetEditorOperation")};
}
FCapabilityResult FNexusExecNiagaraStackFixCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	const FNexusArgs A(Arguments);
	if (A.Str(TEXT("AssetPath")).IsEmpty() || A.Str(TEXT("IssueId")).IsEmpty() || A.Str(TEXT("FixId")).IsEmpty()) return FCapabilityResult::MakeArgInvalid(TEXT("AssetPath, IssueId and FixId must be nonempty"));
	auto Input = MakeShared<FJsonObject>(); Input->SetObjectField(TEXT("System"), FNexusNativeEditorTools::Reference(A.Str(TEXT("AssetPath"))));
	Input->SetStringField(TEXT("IssueId"), A.Str(TEXT("IssueId"))); Input->SetStringField(TEXT("FixId"), A.Str(TEXT("FixId")));
	auto Operation = FNexusEditorOperations::Create(TEXT("OfficialToolset"));
	FNexusNativeEditorTools::Call(TEXT("NiagaraToolsets"), TEXT("ApplyStackIssueFix"), Input, Operation);
	return FNexusEditorToolSchema::Result(Operation);
}
REGISTER_MCP_CAPABILITY(FNexusExecNiagaraStackFixCapability)
#endif
