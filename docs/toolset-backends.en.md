<a id="独立引擎后端与版本适配"></a>
# Independent engine backends and version adaptation
[中文](toolset-backends.md) · English

[Documentation](README.en.md) · [Editor capability scope](editor-compatibility.en.md)

NexusLink implements editor operations, action discovery, and parameter conversion itself, calling Unreal Engine and relevant domain plugin APIs directly. This plugin does not directly link or call official Toolset plugins, ToolsetCore, or ToolsetRegistry, and does not load official Python toolset packages. Separable official Toolset adapter plugins do not participate in NexusLink execution, and the official MCP service need not run.

When keeping official UE 5.9, "zero dependency" is checked at NexusLink's own boundary:

- Zero direct official Toolset / ToolsetCore / ToolsetRegistry dependency declarations in NexusLink.uplugin and build rules.
- Zero source references/forwarding that execute functionality through official Toolsets, Registry, or their Python packages.
- Zero regular or delayed direct imports of official Toolset DLLs in the Windows NexusLink and NexusLinkEditor DLLs.

The official editor itself has a hard UnrealEd → SourceControl → ToolsetCore dependency, and UMGEditor also depends on ToolsetCore. Standard PCG enables ToolsetRegistry in Editor targets; PCGEditor depends on ToolsetCore, ToolsetRegistry, and ToolsetRegistryEditor. Keeping these engine modules and domain features requires retaining their engine-mandated transitive dependencies. Separable official Toolset plugins can still be disabled. This boundary does not mean all physical transitive dependencies across the editor process or engine modules become zero.

The source gate is py -m pytest scripts/tests/test_no_toolset_dependencies.py. Check the two compiled DLLs in a VS Native Tools command prompt:

```powershell
py scripts/audit_binary_dependencies.py Binaries/Win64/UnrealEditor-NexusLink.dll Binaries/Win64/UnrealEditor-NexusLinkEditor.dll
```

If MSVC tools are not on PATH, explicitly pass --dumpbin <path-to-dumpbin.exe>. Checks include delayed imports. Missing tools/files or unparseable output fail rather than count as a pass. This does not recursively inspect official engine DLLs. Source CI without DLL artifacts can demonstrate only source-gate and parser-test success, not replace artifact inspection.

<a id="领域插件与自有实现"></a>
## Domain plugins and native implementations

Domain plugins supply the actual data types and engine features and still require enablement and recompilation as needed. Input, performance, and UI diagnostics directly use PlayerInput/EnhancedInput, RHI, CSVUtils, and optional CommonUIDebugging without official toolsets. Other examples include Niagara, PCG, ControlRig, GameFeatures, DataRegistry, ModelViewViewModel, and SemanticSearch. An optional domain plugin is not an official Toolset dependency.

- Settings and plugin discovery directly read ISettingsModule / IPluginManager; persistence uses their engine editor APIs.
- GameplayTags, PhysicsAsset, PCG, Niagara, materials, meshes, MVVM, UMG, Sequencer, and Control Rig operations use their own engine APIs while retaining NexusLink parameter validation, version limits, compilation, and saving.
- Slate uses NexusLink's session Ref cache and visible widget trees; Automation uses the local AutomationController; Live Coding uses a nonwaiting underlying entry point.
- GetProjectToolsets retains its historical name and section parameters but queries NexusLink Capability groups and action Schema, never an external Toolset registry.
- GetProjectSkills scans SKILL.md under the project, enabled plugins, and engine AgentTools directories. Details reads only exact file paths returned by List.
- SearchAssetsSemantic directly uses the SemanticSearch domain plugin's index and Embedding Provider, preserving real hybrid vector/BM25 and similar-asset search. Missing models or indices produce explicit errors.

<a id="返回值与长操作"></a>
## Results and long operations

Editor operations report Backend=Independent. Query GetEditorOperation with OperationId. Completed means execution ended; inspect test, compilation, and domain business results too.

Existing Result.CompatibilityResult / Result.OfficialResult keys may remain compatible data containers; their names no longer designate the execution backend. UI and compilation interfaces may also return structured data directly in Result. Follow each capability's output Schema.

Save, permission, transaction, and batch boundaries follow each Capability's contract. Disconnects or canceled waits must not automatically replay writes. Version adaptation follows actual underlying APIs without fabricating missing types or functionality.

<a id="从官方-toolset-后端迁移"></a>
## Migrating from an official Toolset backend

1. Keep domain plugins you actually use and their engine-required dependencies, then recompile NexusLink. Standard UE 5.9 PCG must retain Registry. NexusLink itself needs neither extra separable official Toolset plugins nor the official MCP service.
2. Use GetProjectToolsets(Sections=["Actions"]) to obtain action InputSchema, version requirements, and support status. Toolsets/Tools/Schema now return NexusLink groups and Capability Schema; old external Toolset names are no longer queryable.
3. GraphDSL / WriteGraphDSL now use native UnrealGraphText node text. Read Code returned by this tool first; old official S-expression documentation cannot be passed directly as Code. Structured WriteGraphPlan retains its node-plan contract.
4. Migrate project skills to SKILL.md files: List first, then read the returned SkillPath. Old AgentSkill class paths are no longer valid Details inputs.

For exact scope see [editor tools](editor-toolsets.en.md), [Blueprint and scene operations](editor-compatibility.en.md), and [animation and domain actions](animation-niagara-gamefeatures-toolsets.en.md).
