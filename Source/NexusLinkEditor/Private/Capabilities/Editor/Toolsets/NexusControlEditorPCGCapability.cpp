// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusControlEditorPCGCapability.h"
#if WITH_NEXUS_TOOLSETS
#include "Utils/NexusEditorToolSchema.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"

void FNexusControlEditorPCGCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ControlEditorPCG");
	Out.Description = TEXT("Spawn a PCG graph instance in the editor or execute an existing instance asynchronously. Generated content may change the level.");
	auto Vector = FNexusSchema::Object().Required(TEXT("X"), FNexusSchema::Num(TEXT("X"))).Required(TEXT("Y"), FNexusSchema::Num(TEXT("Y"))).Required(TEXT("Z"), FNexusSchema::Num(TEXT("Z"))).Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("PCG control"), {TEXT("Spawn"), TEXT("Execute")}))
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Graph asset object path for Spawn")))
		.Prop(TEXT("ActorName"), FNexusSchema::Str(TEXT("New instance name for Spawn")))
		.Prop(TEXT("Location"), Vector.ToSharedRef())
		.Prop(TEXT("ActorPath"), FNexusSchema::Str(TEXT("PCGVolume object refPath from Spawn or GetEditorPCG Instances")))
		.Required({TEXT("Action")}).Build();
	FNexusEditorToolSchema::ConfigureNative(Out, true, TEXT("PCGToolset"));
	Out.SearchAssetTypes = {TEXT("PCGGraph")};
	Out.RelatedCapabilities = {TEXT("GetEditorPCG"), TEXT("GetEditorOperation")};
}
FCapabilityResult FNexusControlEditorPCGCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	const FNexusArgs A(Arguments); const bool bSpawn = A.Str(TEXT("Action")) == TEXT("Spawn");
	auto Input = MakeShared<FJsonObject>();
	if (bSpawn)
	{
		if (A.Str(TEXT("AssetPath")).IsEmpty() || A.Str(TEXT("ActorName")).IsEmpty()) return FCapabilityResult::MakeArgInvalid(TEXT("Spawn requires AssetPath and ActorName"));
		Input->SetObjectField(TEXT("Graph"), FNexusNativeEditorTools::Reference(A.Str(TEXT("AssetPath")))); Input->SetStringField(TEXT("Name"), A.Str(TEXT("ActorName"))); Input->SetStringField(TEXT("JsonParams"), TEXT(""));
		auto Location = MakeShared<FJsonObject>(), Rotation = MakeShared<FJsonObject>(), Scale = MakeShared<FJsonObject>(), Transform = MakeShared<FJsonObject>();
		const TSharedPtr<FJsonObject>* Position = nullptr; Arguments->TryGetObjectField(TEXT("Location"), Position); const FNexusArgs L(Position ? *Position : nullptr);
		for (const FString& Axis : {TEXT("X"), TEXT("Y"), TEXT("Z")}) { Location->SetNumberField(Axis.ToLower(), L.Num(*Axis)); Scale->SetNumberField(Axis.ToLower(), Axis == TEXT("Z") ? 10 : 25); }
		for (const FString& Axis : {TEXT("pitch"), TEXT("yaw"), TEXT("roll")}) Rotation->SetNumberField(Axis, 0);
		Transform->SetObjectField(TEXT("location"), Location); Transform->SetObjectField(TEXT("rotation"), Rotation); Transform->SetObjectField(TEXT("scale"), Scale); Input->SetObjectField(TEXT("Transform"), Transform);
	}
	else
	{
		if (A.Str(TEXT("ActorPath")).IsEmpty()) return FCapabilityResult::MakeArgInvalid(TEXT("Execute requires ActorPath"));
		Input->SetObjectField(TEXT("PCGVolume"), FNexusNativeEditorTools::Reference(A.Str(TEXT("ActorPath"))));
	}
	auto Operation = FNexusEditorOperations::Create(TEXT("OfficialToolset"));
	FNexusNativeEditorTools::Call(TEXT("PCGToolset"), bSpawn ? TEXT("SpawnGraphInstance") : TEXT("ExecuteGraphInstance"), Input, Operation);
	return FNexusEditorToolSchema::Result(Operation);
}
REGISTER_MCP_CAPABILITY(FNexusControlEditorPCGCapability)
#endif
