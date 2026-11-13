<a id="umg材质项目配置与-sequencer-扩展"></a>
# UMG, materials, project configuration, and Sequencer extensions
[中文](editor-remaining-toolsets.md) · English

[Documentation](README.en.md) · [The first four groups of editor compatibility capabilities](editor-compatibility.en.md) · [Independent backends](toolset-backends.en.md)

All operations are implemented directly by NexusLink using public engine APIs. The plugin itself does not directly link or call official Toolset, Registry, or official Python toolset packages. Standard modules retain [engine-required transitive dependencies](toolset-backends.en.md). Availability depends on underlying APIs, domain plugins, and NexusLink permissions. Plugins are not automatically enabled for one request.

## UMG

ManageAssetUserWidget adds Rename, SetVariable, BindEvent, AddUIComponent, RemoveUIComponent, and MoveUIComponent. These actions retain Operations[] and existing compilation/save options.

- Rename uses WidgetName and a unique NewWidgetName. The compatibility path updates WidgetTree, Blueprint variable references, delegate bindings, animation bindings, and existing UIComponent target names. Arbitrary object paths are not accepted as targets.
- SetVariable takes an explicit bIsVariable. BindEvent uses EventName (such as OnClicked); the target must be an exposed, compiled widget variable in the current Blueprint, and the event must be BlueprintAssignable. First run SetVariable separately with top-level bCompile=true, then bind the event. Repeated bindings reuse existing nodes.
- UIComponent operations use ComponentClass; moving also requires an existing RelativeComponentClass on the same widget and bMoveAfter. The framework is available in UE 5.6+; earlier versions explicitly return unsupported. GetAssetUserWidget(Sections=["UIComponents"]) returns widgets, component types, and component paths with existing pagination.

Implementation uses WidgetBlueprint, Kismet editor tools, and UIComponentWidgetBlueprintExtension directly and does not open a Widget editor window for an operation.

<a id="材质与材质函数"></a>
## Materials and material functions

ManageAssetMaterial adds Layout, DeleteUnused, RenameParameterGroup, and UngroupParameters for both Material and MaterialFunction. GetAssetMaterial(Sections=["ParameterGroups"]) returns group names and parameter counts.

Rename groups using GroupName/NewGroupName; the destination must not be empty. None denotes ungrouped parameters. UngroupParameters clears grouping without deleting parameters. Cleanup follows references from material or function outputs; the compatibility implementation preserves CustomOutput expressions. Missing groups return explicit errors.

Layout, material cleanup, and parameter grouping use engine MaterialEditingLibrary and graph traversal directly. MaterialFunction graph creation and node/link/property editing keep existing interfaces; function cleanup uses compatible graph traversal. The compatibility implementation introduces no new graph format. Saving continues to use the existing asset save options.

<a id="项目设置"></a>
## Project settings

Use GetProjectSettings to discover Container/Category/Section and Properties before using ManageProjectSettings. Value queries retain the original Value text and add JsonValue for reading/writing the original type.

Operations[] supports:

- SetProperties: the Properties object takes exact editable Config property names and correctly typed JSON values, at most 64 fields. All fields are validated on a copy before application and saving.
- Save: calls the registered settings section's save interface.
- Reset: calls ResetDefaults only on sections declaring reset support.

Success returns bSavedToDisk. bRestartRequired follows property restart metadata; Reset conservatively requires restart. Missing restart metadata does not prove arbitrary settings take effect immediately. A save failure may leave changed values in memory, as explicitly explained in the error; writes are not automatically retried.

All versions directly use ISettingsModule/ISettingsSection and the editor property-change lifecycle. SetProperties persists values itself and is not a temporary preview. This is a dedicated configuration write without a Dangerous tag; only arbitrary code-execution entry points are marked dangerous.

<a id="项目插件"></a>
## Project plugins

ManageProjectPlugin targets PluginName. Operations[] supports Create, SetEnabled, UpdateDescriptor, AddDependency, and RemoveDependency, reporting bSavedToDisk/bRestartRequired for each.

Create makes a unique content-only plugin under the current project's Plugins directory without C++ modules. It uses FPluginDescriptor and project plugin-management APIs. Names cannot contain paths; existing directories/plugins are not overwritten. Descriptor edits are restricted to plugins inside the current project. Enablement is written through project configuration and takes effect on the next launch.

UpdateDescriptor allows the string Properties fields FriendlyName, Description, Category, VersionName, CreatedBy, CreatedByURL, DocsURL, MarketplaceURL, and SupportURL. Other descriptor content is retained. Dependencies must be distinct discovered plugins; enabled dependency cycles are rejected. Changes synchronize cache and disk through the plugin's UpdateDescriptor, without PluginToolset.

<a id="sequencer-与-animmixer"></a>
## Sequencer and AnimMixer

Get/ControlEditorAnimation continues to accept fixed Action and Parameters values, never arbitrary tool class names. Domain actions cover Conditions, Outliner, bindings, FBX/animation links, and Mixer. Discover names, parameters, and availability with GetProjectToolsets(Sections=["Actions"], Capability=...).

Compatibility implementations include reading/writing/clearing Section/Track conditions and row conditions on existing TrackRowMetadata. If the engine lacks the condition container, a version restriction is returned. Standard Sequence folders use SequenceFolders/AddSequenceFolder/RemoveSequenceFolder, with sequence references and folder_name for writes in Parameters. Only empty root folders can be removed.

ExportFbx/ImportFbx use world, sequence, bindings, fbx_file_path, and optional native settings references. The compatibility path calls the actual SequencerTools interface in that version. Export refuses to overwrite an existing .fbx; import requires an existing file. Operations are not automatically retried.

Outliner view-model operations and custom binding conversion depend on editor scripting APIs available in that engine, usually with the relevant Sequence already open. Older versions explicitly return unsupported when no equivalent mapping exists. Sequence folders and ordinary binding operations remain available.

The AnimMixer framework in 5.6/5.7 is not the 5.8 Layer/Transition object model. Operations use engine Layer/Transition/Decoration APIs; engines lacking that model do not receive fabricated Layer objects. Generic Section/key reads and writes on older Mixer tracks retain the Sequencer compatibility interfaces. Discover specific Mixer operations through action discovery; SequencerAnimMixerToolset is not required.
