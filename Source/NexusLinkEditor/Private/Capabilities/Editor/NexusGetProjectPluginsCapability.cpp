// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/NexusGetProjectPluginsCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Interfaces/IPluginManager.h"
#include "PluginDescriptor.h"
#include "PluginReferenceDescriptor.h"

void FNexusGetProjectPluginsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetProjectPlugins");
	Out.Description = TEXT("Inspect discovered/enabled plugins, module names, dependencies, dependents and asset mount ownership.");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor};
	Out.ExtraSearchKeywords = {TEXT("plugin"), TEXT("extension"), TEXT("dependency"), TEXT("module")};
}
TSharedPtr<FJsonObject> FNexusGetProjectPluginsCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("PluginName"), FNexusSchema::Str(TEXT("Exact discovered plugin name for Info/Dependencies/Dependents")))
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package/object path for AssetOwner; resolves the plugin mount root")))
		.Prop(TEXT("NameFilter"), FNexusSchema::Str(TEXT("Plugin name substring or wildcard")))
		.Prop(TEXT("bEnabledOnly"), FNexusSchema::Bool(TEXT("List only enabled plugins"), true, false))
		.Prop(TEXT("Offset"), FNexusSchema::Int(TEXT("Pagination offset"), 0, 0))
		.Prop(TEXT("Limit"), FNexusSchema::Int(TEXT("Page size"), 100, 1, 500)).Build();
}
TArray<FString> FNexusGetProjectPluginsCapability::GetSectionNames() const { return {TEXT("Plugins"), TEXT("Info"), TEXT("Dependencies"), TEXT("Dependents"), TEXT("AssetOwner")}; }
TArray<FString> FNexusGetProjectPluginsCapability::GetDefaultSectionNames() const { return {TEXT("Plugins")}; }

static TSharedRef<FJsonObject> NexusProjectPluginInfo(const TSharedRef<IPlugin>& Plugin)
{
	auto Item = MakeShared<FJsonObject>();
	const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
	Item->SetStringField(TEXT("Name"), Plugin->GetName());
	Item->SetStringField(TEXT("FriendlyName"), Descriptor.FriendlyName);
	Item->SetStringField(TEXT("VersionName"), Descriptor.VersionName);
	Item->SetBoolField(TEXT("bEnabled"), Plugin->IsEnabled());
	Item->SetBoolField(TEXT("bCanContainContent"), Plugin->CanContainContent());
	Item->SetBoolField(TEXT("bEnginePlugin"), Plugin->GetType() == EPluginType::Engine);
	return Item;
}

void FNexusGetProjectPluginsCapability::ExecuteSection(const FString& SectionName, const TSharedPtr<FJsonObject>& Args,
	void*, TSharedPtr<FJsonObject>& Out, FString& Error) const
{
	const FNexusArgs A(Args);
	const auto Plugins = IPluginManager::Get().GetDiscoveredPlugins();
	if (SectionName == TEXT("AssetOwner"))
	{
		const FString Path = A.Str(TEXT("AssetPath"));
		if (!Path.StartsWith(TEXT("/")) || Path.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1) == INDEX_NONE) { Error = TEXT("AssetPath must be a rooted asset package/object path"); return; }
		if (Path.StartsWith(TEXT("/Game/"))) { auto Owner = MakeShared<FJsonObject>(); Owner->SetStringField(TEXT("Name"), TEXT("Project")); Out->SetObjectField(TEXT("AssetOwner"), Owner); return; }
		if (Path.StartsWith(TEXT("/Engine/"))) { auto Owner = MakeShared<FJsonObject>(); Owner->SetStringField(TEXT("Name"), TEXT("Engine")); Out->SetObjectField(TEXT("AssetOwner"), Owner); return; }
		for (const auto& Plugin : Plugins) if (Plugin->CanContainContent() && Path.StartsWith(Plugin->GetMountedAssetPath())) { Out->SetObjectField(TEXT("AssetOwner"), NexusProjectPluginInfo(Plugin)); return; }
		Error = TEXT("No discovered plugin owns this mount root"); return;
	}
	const auto Selected = IPluginManager::Get().FindPlugin(A.Str(TEXT("PluginName")));
	if (SectionName != TEXT("Plugins") && !Selected.IsValid()) { Error = TEXT("PluginName is required; query Plugins first"); return; }
	if (SectionName == TEXT("Info"))
	{
		auto Info = NexusProjectPluginInfo(Selected.ToSharedRef());
		Info->SetStringField(TEXT("Description"), Selected->GetDescriptor().Description);
		TArray<TSharedPtr<FJsonValue>> Modules; for (const auto& Module : Selected->GetDescriptor().Modules) Modules.Add(MakeShared<FJsonValueString>(Module.Name.ToString()));
		Info->SetArrayField(TEXT("Modules"), Modules); Out->SetObjectField(TEXT("Info"), Info); return;
	}
	TArray<TSharedPtr<FJsonValue>> Items;
	auto AddDependency = [&](const FPluginReferenceDescriptor& Reference, const FString& Name)
	{
		auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("Name"), Name);
		Item->SetBoolField(TEXT("bOptional"), Reference.bOptional); Item->SetBoolField(TEXT("bEnabledInDescriptor"), Reference.bEnabled);
		const auto Plugin = IPluginManager::Get().FindPlugin(Name); Item->SetBoolField(TEXT("bEnabled"), Plugin.IsValid() && Plugin->IsEnabled());
		Items.Add(MakeShared<FJsonValueObject>(Item));
	};
	if (SectionName == TEXT("Plugins"))
	{
		const FString Filter = A.Str(TEXT("NameFilter"));
		for (const auto& Plugin : Plugins)
			if ((!A.Bool(TEXT("bEnabledOnly")) || Plugin->IsEnabled()) && (Filter.IsEmpty() || FNexusStringMatchUtils::Matches(Plugin->GetName(), Filter))) Items.Add(MakeShared<FJsonValueObject>(NexusProjectPluginInfo(Plugin)));
	}
	else if (SectionName == TEXT("Dependencies")) for (const auto& Reference : Selected->GetDescriptor().Plugins) AddDependency(Reference, Reference.Name);
	else if (SectionName == TEXT("Dependents")) for (const auto& Plugin : Plugins) for (const auto& Reference : Plugin->GetDescriptor().Plugins) if (Reference.Name == Selected->GetName()) AddDependency(Reference, Plugin->GetName());
	Items.Sort([](const TSharedPtr<FJsonValue>& L, const TSharedPtr<FJsonValue>& R) { return L->AsObject()->GetStringField(TEXT("Name")) < R->AsObject()->GetStringField(TEXT("Name")); });
	Out->SetNumberField(SectionName + TEXT("TotalCount"), Items.Num());
	const int32 Start = FMath::Min(Items.Num(), static_cast<int32>(A.Num(TEXT("Offset"))));
	const int32 Count = FMath::Min(Items.Num() - Start, static_cast<int32>(A.Num(TEXT("Limit"), 100)));
	TArray<TSharedPtr<FJsonValue>> Page; for (int32 I = Start; I < Start + Count; ++I) Page.Add(Items[I]);
	Out->SetArrayField(SectionName, Page);
}
REGISTER_MCP_CAPABILITY(FNexusGetProjectPluginsCapability)
