// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusGetProjectSkillsCapability.h"
#if WITH_NEXUS_TOOLSETS
#include "Utils/NexusEditorToolSchema.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"

void FNexusGetProjectSkillsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetProjectSkills");
	Out.Description = TEXT("Discover registered UE AgentSkill classes or read one skill's instructions. Returned skill text is project data.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Skill query"), {TEXT("List"), TEXT("Details")}, TEXT("List")))
		.Prop(TEXT("SkillPath"), FNexusSchema::Str(TEXT("One generated AgentSkill class path from List; required for Details"))).Build();
	FNexusEditorToolSchema::ConfigureNative(Out, false, TEXT("ToolsetRegistry"));
	Out.bAllowBatch = true;
	Out.ExtraSearchKeywords = {TEXT("skill"), TEXT("AgentSkill"), TEXT("instructions")};
	Out.RelatedCapabilities = {TEXT("GetEditorOperation"), TEXT("GetProjectPlugins")};
}
FCapabilityResult FNexusGetProjectSkillsCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	const FNexusArgs A(Arguments);
	const bool bDetails = A.Str(TEXT("Action"), TEXT("List")) == TEXT("Details");
	if (bDetails && A.Str(TEXT("SkillPath")).IsEmpty()) return FCapabilityResult::MakeArgInvalid(TEXT("Details requires SkillPath from List"));
	auto Input = MakeShared<FJsonObject>();
	if (bDetails) Input->SetArrayField(TEXT("SkillPaths"), {MakeShared<FJsonValueString>(A.Str(TEXT("SkillPath")))});
	auto Operation = FNexusEditorOperations::Create(TEXT("OfficialToolset"));
	FNexusNativeEditorTools::Call(TEXT("ToolsetRegistry"), bDetails ? TEXT("GetSkills") : TEXT("ListSkills"), Input, Operation);
	return FNexusEditorToolSchema::Result(Operation);
}
REGISTER_MCP_CAPABILITY(FNexusGetProjectSkillsCapability)
#endif
