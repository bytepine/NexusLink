// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusGetEditorNiagaraCapability.h"
#if WITH_NIAGARA
#include "Utils/NexusEditorToolSchema.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"

void FNexusGetEditorNiagaraCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetEditorNiagara");
	Out.Description = TEXT("Deep Niagara diagnostics: compile state, stack issues, dependencies, system/emitter/user-variable data, module/input/renderer/data-interface schemas and dynamic input chains.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Niagara inspection"), {TEXT("CompileState"), TEXT("StackIssues"), TEXT("Dependencies"), TEXT("SystemData"), TEXT("ModuleTopology"), TEXT("ModuleSchema"), TEXT("ModuleInputValues"), TEXT("RendererSchema"), TEXT("RendererData"), TEXT("InputSchema"), TEXT("InputData"), TEXT("DynamicInputChain"), TEXT("AvailableDynamicInputs"), TEXT("SystemSchema"), TEXT("EmitterSchema"), TEXT("EmitterData"), TEXT("UserVariables"), TEXT("DataInterfaceSchema"), TEXT("SystemSummary"), TEXT("EmitterTopology"), TEXT("ScriptTopology")}, TEXT("CompileState")))
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("NiagaraSystem asset object path, except parameterless SystemSchema/EmitterSchema and class-only schema queries")))
		.Prop(TEXT("EmitterName"), FNexusSchema::Str(TEXT("Emitter name from SystemSummary; omit for system script modules")))
		.Prop(TEXT("ScriptName"), FNexusSchema::Str(TEXT("Script name from SystemSummary/EmitterTopology for module/input inspection")))
		.Prop(TEXT("ModuleName"), FNexusSchema::Str(TEXT("Module name from ScriptTopology")))
		.Prop(TEXT("RendererIndex"), FNexusSchema::Int(TEXT("Emitter renderer index for RendererData"), -1, -1))
		.Prop(TEXT("InputNameStack"), FNexusSchema::StrArr(TEXT("1-32 input names, outermost to innermost, for InputSchema/InputData/DynamicInputChain")))
		.Prop(TEXT("ClassPath"), FNexusSchema::Str(TEXT("Renderer class for RendererSchema, data interface class for DataInterfaceSchema, or Niagara type struct/class/enum for AvailableDynamicInputs"))).Build();
	FNexusEditorToolSchema::ConfigureCompatible(Out, false, TEXT("NiagaraToolsets"));
	Out.SearchAssetTypes = {TEXT("NiagaraSystem")};
	Out.ExtraSearchKeywords = {TEXT("niagara"), TEXT("vfx"), TEXT("stack"), TEXT("renderer"), TEXT("dynamic input")};
	Out.RelatedCapabilities = {TEXT("GetAssetNiagaraSystem"), TEXT("ExecNiagaraStackFix"), TEXT("ControlEditorNiagara"), TEXT("GetProjectToolsets"), TEXT("GetEditorOperation")};
}
FCapabilityResult FNexusGetEditorNiagaraCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	const FNexusArgs A(Arguments); const FString Action = A.Str(TEXT("Action"), TEXT("CompileState"));
	auto Input = MakeShared<FJsonObject>(); FString Tool;
	if (Action == TEXT("SystemSchema") || Action == TEXT("EmitterSchema")) Tool = TEXT("Get") + Action;
	else if (Action == TEXT("RendererSchema") || Action == TEXT("AvailableDynamicInputs") || Action == TEXT("DataInterfaceSchema"))
	{
		if (A.Str(TEXT("ClassPath")).IsEmpty()) return FCapabilityResult::MakeArgInvalid(TEXT("Class-only query requires ClassPath"));
		Tool = TEXT("Get") + Action;
		if (Action == TEXT("RendererSchema")) Input->SetObjectField(TEXT("RendererClass"), FNexusNativeEditorTools::Reference(A.Str(TEXT("ClassPath"))));
		else if (Action == TEXT("DataInterfaceSchema")) Input->SetObjectField(TEXT("DataInterfaceClass"), FNexusNativeEditorTools::Reference(A.Str(TEXT("ClassPath"))));
		else { auto Type = MakeShared<FJsonObject>(); Type->SetObjectField(TEXT("classStructOrEnum"), FNexusNativeEditorTools::Reference(A.Str(TEXT("ClassPath")))); Input->SetObjectField(TEXT("Type"), Type); }
	}
	else
	{
		if (A.Str(TEXT("AssetPath")).IsEmpty()) return FCapabilityResult::MakeArgInvalid(TEXT("Niagara inspection requires AssetPath"));
		if (Action == TEXT("CompileState") || Action == TEXT("StackIssues") || Action == TEXT("Dependencies") || Action == TEXT("SystemData") || Action == TEXT("UserVariables") || Action == TEXT("SystemSummary"))
		{
			Tool = Action == TEXT("CompileState") ? TEXT("GetSystemCompileState") : Action == TEXT("Dependencies") ? TEXT("GetSystemDependencies") : TEXT("Get") + Action;
			Input->SetObjectField(TEXT("System"), FNexusNativeEditorTools::Reference(A.Str(TEXT("AssetPath"))));
		}
		else if (Action == TEXT("EmitterData") || Action == TEXT("EmitterTopology") || Action == TEXT("ScriptTopology"))
		{
			if (Action != TEXT("ScriptTopology") && A.Str(TEXT("EmitterName")).IsEmpty()) return FCapabilityResult::MakeArgInvalid(TEXT("Emitter query requires EmitterName"));
			if (Action == TEXT("ScriptTopology") && A.Str(TEXT("ScriptName")).IsEmpty()) return FCapabilityResult::MakeArgInvalid(TEXT("ScriptTopology requires ScriptName from EmitterTopology"));
			Tool = Action == TEXT("ScriptTopology") ? TEXT("GetScriptStackTopology") : TEXT("Get") + Action; auto Ref = MakeShared<FJsonObject>(); Ref->SetObjectField(TEXT("system"), FNexusNativeEditorTools::Reference(A.Str(TEXT("AssetPath")))); Ref->SetStringField(TEXT("emitterName"), A.Str(TEXT("EmitterName"))); Ref->SetStringField(TEXT("scriptName"), A.Str(TEXT("ScriptName"))); Ref->SetStringField(TEXT("moduleName"), TEXT("")); Ref->SetNumberField(TEXT("rendererIndex"), -1); Ref->SetArrayField(TEXT("inputNameStack"), {}); Input->SetObjectField(Action == TEXT("ScriptTopology") ? TEXT("ScriptRef") : TEXT("EmitterRef"), Ref);
		}
		else
		{
			const bool bRenderer = Action == TEXT("RendererData");
			const bool bInput = Action == TEXT("InputSchema") || Action == TEXT("InputData") || Action == TEXT("DynamicInputChain");
			const auto Names = A.StrArr(TEXT("InputNameStack"));
			if (bRenderer ? (A.Str(TEXT("EmitterName")).IsEmpty() || A.Num(TEXT("RendererIndex"), -1) < 0) : (A.Str(TEXT("ScriptName")).IsEmpty() || A.Str(TEXT("ModuleName")).IsEmpty())) return FCapabilityResult::MakeArgInvalid(TEXT("RendererData requires EmitterName/RendererIndex; module and input queries require ScriptName/ModuleName"));
			if (bInput && (Names.Num() == 0 || Names.Num() > 32)) return FCapabilityResult::MakeArgInvalid(TEXT("Input queries require 1-32 InputNameStack entries"));
			auto Ref = MakeShared<FJsonObject>(); Ref->SetObjectField(TEXT("system"), FNexusNativeEditorTools::Reference(A.Str(TEXT("AssetPath"))));
			Ref->SetStringField(TEXT("emitterName"), A.Str(TEXT("EmitterName"))); Ref->SetStringField(TEXT("scriptName"), A.Str(TEXT("ScriptName"))); Ref->SetStringField(TEXT("moduleName"), A.Str(TEXT("ModuleName"))); Ref->SetNumberField(TEXT("rendererIndex"), A.Num(TEXT("RendererIndex"), -1));
			TArray<TSharedPtr<FJsonValue>> Stack; for (const auto& Name : Names) Stack.Add(MakeShared<FJsonValueString>(Name)); Ref->SetArrayField(TEXT("inputNameStack"), Stack);
			Tool = bInput && Action != TEXT("DynamicInputChain") ? TEXT("GetStack") + Action : TEXT("Get") + Action;
			const FString Key = bRenderer ? TEXT("RendererRef") : Action == TEXT("ModuleSchema") ? TEXT("ModuleReference") : Action == TEXT("InputSchema") ? TEXT("InputReference") : bInput ? TEXT("StackInputRef") : TEXT("ModuleRef");
			Input->SetObjectField(Key, Ref);
		}
	}
	auto Operation = FNexusEditorOperations::Create(TEXT("OfficialToolset"));
	FNexusNativeEditorTools::Call(TEXT("NiagaraToolsets"), Tool, Input, Operation);
	return FNexusEditorToolSchema::Result(Operation);
}
REGISTER_MCP_CAPABILITY(FNexusGetEditorNiagaraCapability)
#endif
