// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Runtime/AI/NexusGetNavigationPointCapability.h"
#include "Utils/NexusNavigationUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "NavigationSystem.h"

void FNexusGetNavigationPointCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetNavigationPoint");
	Out.Description = TEXT("Project coordinates onto existing navigation. Reports world/agent/filter availability; no movement or rebuild.");
	const TSharedPtr<FJsonObject> Point = FNexusSchema::Object()
		.Required(TEXT("X"), FNexusSchema::Num(TEXT("World X")))
		.Required(TEXT("Y"), FNexusSchema::Num(TEXT("World Y")))
		.Required(TEXT("Z"), FNexusSchema::Num(TEXT("World Z"))).Build();
	Point->SetStringField(TEXT("description"), TEXT("Coordinates {X:number,Y:number,Z:number}; all axes are required"));
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("Point"), Point.ToSharedRef())
		.Prop(TEXT("ProjectionExtent"), Point.ToSharedRef())
		.Prop(TEXT("World"), FNexusSchema::Enum(TEXT("World selection"), {TEXT("Auto"), TEXT("Editor"), TEXT("Play")}, TEXT("Auto")))
		.Prop(TEXT("WorldName"), FNexusSchema::Str(TEXT("Optional exact world name for multiple PIE worlds")))
		.Prop(TEXT("ActorName"), FNexusSchema::Str(TEXT("Optional exact actor name or unambiguous label for agent context")))
		.Prop(TEXT("AgentRadius"), FNexusSchema::Num(TEXT("Optional positive agent radius override")))
		.Prop(TEXT("AgentHeight"), FNexusSchema::Num(TEXT("Optional positive agent height override")))
		.Prop(TEXT("FilterClass"), FNexusSchema::Str(TEXT("Optional NavigationQueryFilter class name/path"))).Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime};
	Out.RelatedCapabilities = {TEXT("GetNavigationPath"), TEXT("ListRuntimeActors"), TEXT("InteractRuntimeActorAI")};
	Out.ExtraSearchKeywords = {TEXT("navigation"), TEXT("project"), TEXT("projection"), TEXT("navmesh"), TEXT("agent")};
}

FCapabilityResult FNexusGetNavigationPointCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FVector Point;
	if (!FNexusNavigationUtils::ReadPoint(Arguments, TEXT("Point"), Point)) return FCapabilityResult::MakeArgInvalid(TEXT("Point requires finite X/Y/Z coordinates"));
	return FNexusCapabilityResultBuilder::Build([&](auto& Entries, auto& Top, auto& Error)
	{
		FNexusNavigationContext Context;
		if (!FNexusNavigationUtils::Resolve(Arguments, Point, Context, Error)) return;
		auto Entry = MakeShared<FJsonObject>();
		FNexusNavigationUtils::Describe(Context, Entry);
		FNavLocation Location;
		const bool bProjected = Context.System && Context.Data && Context.System->ProjectPointToNavigation(Point, Location, Context.Extent, Context.Data, Context.Filter);
		Entry->SetBoolField(TEXT("bProjected"), bProjected);
		Entry->SetObjectField(TEXT("Point"), FNexusNavigationUtils::PointJson(Point));
		if (bProjected) Entry->SetObjectField(TEXT("ProjectedPoint"), FNexusNavigationUtils::PointJson(Location.Location));
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}
REGISTER_MCP_CAPABILITY(FNexusGetNavigationPointCapability)
