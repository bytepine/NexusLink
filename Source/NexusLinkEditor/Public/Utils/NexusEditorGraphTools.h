// Copyright byteyang. All Rights Reserved.
#pragma once
#include "NexusCapability.h"

class IAssetEditorInstance;
class SGraphEditor;
class UEdGraph;

struct NEXUSLINKEDITOR_API FNexusEditorGraphTools final
{
	using FFindPCGView = TFunction<TOptional<TSharedPtr<SGraphEditor>>(UObject*, IAssetEditorInstance*, UEdGraph*)>;
	using FOpenPCGView = TFunction<bool(UObject*, IAssetEditorInstance*, UEdGraph*, TSharedPtr<SGraphEditor>&, FString&)>;
	static FFindPCGView FindPCGView;
	static FOpenPCGView OpenPCGView;
	static void Define(FNexusCapabilityDefinition& Out, bool bWrite);
	static FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments, bool bWrite);
};
