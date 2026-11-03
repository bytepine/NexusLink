// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Asset/NexusGetAssetEditPreviewCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusAssetEditUtils.h"
#include "Utils/NexusAssetStructureUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"

void FNexusGetAssetEditPreviewCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetEditPreview");
	Out.Description = TEXT("Plan scalar values and bounded BP/UMG/PCG structural edits in temporary storage. No source writes, compilation or save.");
	const TSharedRef<FJsonObject> Operation = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Matching operation"), {TEXT("SetDefaults"), TEXT("Set"), TEXT("SetProperty"), TEXT("Add"), TEXT("Remove"), TEXT("Move"), TEXT("AddNode"), TEXT("RemoveNode"), TEXT("AddEdge"), TEXT("RemoveEdge"), TEXT("Disconnect"), TEXT("DisconnectAll")}))
		.Prop(TEXT("ParentWidget"), FNexusSchema::Str(TEXT("Destination panel")))
		.Prop(TEXT("WidgetClass"), FNexusSchema::Str(TEXT("Concrete widget class")))
		.Prop(TEXT("ChildIndex"), FNexusSchema::Int(TEXT("Destination child index; -1 appends"), -1, -1))
		.Prop(TEXT("GraphName"), FNexusSchema::Str(TEXT("Blueprint graph object name")))
		.Prop(TEXT("NodeId"), FNexusSchema::Str(TEXT("Existing node id")))
		.Prop(TEXT("SettingsClass"), FNexusSchema::Str(TEXT("Concrete PCG settings class")))
		.Prop(TEXT("FromNodeId"), FNexusSchema::Str(TEXT("PCG source node id")))
		.Prop(TEXT("ToNodeId"), FNexusSchema::Str(TEXT("PCG target node id")))
		.Prop(TEXT("FromPin"), FNexusSchema::Str(TEXT("PCG output pin")))
		.Prop(TEXT("ToPin"), FNexusSchema::Str(TEXT("PCG input pin")))
		.Prop(TEXT("SourceNodeId"), FNexusSchema::Str(TEXT("Blueprint source node id")))
		.Prop(TEXT("TargetNodeId"), FNexusSchema::Str(TEXT("Blueprint target node id")))
		.Prop(TEXT("SourcePinName"), FNexusSchema::Str(TEXT("Blueprint source pin")))
		.Prop(TEXT("TargetPinName"), FNexusSchema::Str(TEXT("Blueprint target pin")))
		.Prop(TEXT("PropertyPath"), FNexusSchema::Str(TEXT("Blueprint/Widget scalar property path")))
		.Prop(TEXT("WidgetName"), FNexusSchema::Str(TEXT("Widget target for SetProperty")))
		.Prop(TEXT("RowName"), FNexusSchema::Str(TEXT("DataTable row for Set")))
		.Prop(TEXT("FieldName"), FNexusSchema::Str(TEXT("DataTable scalar field for Set")))
		.Prop(TEXT("Value"), FNexusSchema::Str(TEXT("Proposed value as ImportText text")))
		.Prop(TEXT("ExpectedValue"), FNexusSchema::Str(TEXT("Optional expected current value")))
		.Required({TEXT("Action")}).Build().ToSharedRef();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Single asset path")))
		.Prop(TEXT("Capability"), FNexusSchema::Enum(TEXT("Target write capability"), {TEXT("ManageAssetBlueprint"), TEXT("ManageAssetDataTable"), TEXT("ManageAssetUserWidget"), TEXT("ManageAssetPCGGraph")}))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Scalar edits to validate"), Operation))
		.Required({TEXT("AssetPath"), TEXT("Capability"), TEXT("Operations")}).Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor};
	Out.RelatedCapabilities = {TEXT("ManageAssetBlueprint"), TEXT("ManageAssetDataTable"), TEXT("ManageAssetUserWidget")};
	Out.ExtraSearchKeywords = {TEXT("preview"), TEXT("ExpectedValue"), TEXT("verify"), TEXT("dry run")};
}

FCapabilityResult FNexusGetAssetEditPreviewCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& Entries, auto& Top, auto& Error)
	{
		const FNexusArgs Args(Arguments);
		const FString Path = Args.Str(TEXT("AssetPath"));
		UObject* Asset = FNexusAssetUtils::LoadAssetWithFallback<UObject>(Path);
		if (!Asset) { Error = TEXT("Asset not found"); return; }
		const TArray<TSharedPtr<FJsonValue>> Operations = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Operations.Num() == 0) { Error = TEXT("Operations must not be empty"); return; }
		for (const auto& Operation : Operations) if (!Operation || Operation->Type != EJson::Object) { Error = TEXT("Every operation must be an object"); return; }
		const FString Capability = Args.Str(TEXT("Capability"));
		const bool bStructural = FNexusAssetStructureUtils::Supports(Capability, FNexusArgs(Operations[0]->AsObject()).Str(TEXT("Action")));
		if (bStructural && Operations.Num() > 128) { Error = TEXT("Structural preview is limited to 128 operations"); return; }
		UObject* Preview = bStructural ? FNexusAssetStructureUtils::Copy(Asset) : nullptr;
		if (bStructural && !Preview) { Error = TEXT("Could not create isolated asset copy"); return; }
		const auto Before = bStructural ? FNexusAssetStructureUtils::Snapshot(Asset) : TArray<FString>();
		if (bStructural) Top->SetStringField(TEXT("StructureHash"), FNexusAssetStructureUtils::Hash(Asset));
		for (const TSharedPtr<FJsonValue>& Value : Operations)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("Path"), Path);
			if (bStructural)
			{
				FString PlanError;
				const bool bValid = Value.IsValid() && Value->Type == EJson::Object && FNexusAssetStructureUtils::ApplyPreview(Preview, Capability, Value->AsObject(), Entry, PlanError);
				Entry->SetBoolField(TEXT("bValid"), bValid);
				if (!bValid) Entry->SetStringField(TEXT("Error"), PlanError.IsEmpty() ? TEXT("Invalid structural operation") : PlanError);
				Entries.Add(MakeShared<FJsonValueObject>(Entry));
				if (!bValid) break;
				continue;
			}
			FNexusAssetEditUtils::Preview(Asset, Capability, Value->AsObject(), Entry);
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		if (bStructural)
		{
			const auto After = FNexusAssetStructureUtils::Snapshot(Preview);
			TArray<TSharedPtr<FJsonValue>> Added, Removed;
			for (const auto& Row : After) if (!Before.Contains(Row)) Added.Add(MakeShared<FJsonValueString>(Row));
			for (const auto& Row : Before) if (!After.Contains(Row)) Removed.Add(MakeShared<FJsonValueString>(Row));
			Top->SetArrayField(TEXT("Added"), Added); Top->SetArrayField(TEXT("Removed"), Removed);
			Top->SetStringField(TEXT("Note"), TEXT("Sequential structural plan only; generated node ids are preview-local. Re-read actual ids after writes. Compilation and disk rollback are outside this plan."));
		}
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetEditPreviewCapability)
