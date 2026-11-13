<a id="nexuslink-capability-元数据规范"></a>
# NexusLink Capability metadata specification

[中文](CapabilitySpec.md) · English

Engine and engine-bundled plugin capabilities, including official UE Toolsets, belong in NexusLink / NexusLinkEditor. Only third-party plugin capabilities belong in NexusLinkExt / NexusLinkExtEditor. Optional enablement does not make a plugin third-party.

> Follow this entire specification when adding/changing Capabilities. Registration provides automatic fallback validation, but run the checklist before submission.

**Independent implementation:** NexusLink itself must not directly link/call official Toolset / ToolsetCore / ToolsetRegistry modules, import their Python packages, or execute through official registries. Use engine/domain APIs directly; this plugin owns action routing, parameter contracts, version gates. Standard modules/domain plugins may retain transitive dependencies; see [independent backend boundaries](../docs/toolset-backends.en.md). scripts/tests/test_no_toolset_dependencies.py checks direct source dependencies/calls. For Windows artifacts, scripts/audit_binary_dependencies.py additionally checks regular/delayed direct DLL imports. Passing source gates does not substitute for binary verification.

---

<a id="1-描述格式四段式"></a>
## 1. Description format (four-part contract)

> **Authoritative language:** Out.Description uses the **English four-part contract**, matching implementation and MCP/AI-visible text. This specification is maintained in separate Chinese and English files.

```
[VERB] [TARGET]. [DIFFERENTIATOR]. [CONSTRAINT?]
```

| Part | Constraint | Description |
|---|---|---|
| VERB + TARGET | Required, preferably ≤30 characters | Action and target; **do not repeat name tokens**, validated during registration |
| DIFFERENTIATOR | Required, preferably ≤50 characters | Key difference from same-prefix capabilities: batch/single, editor/runtime, CDO/instance |
| CONSTRAINT | As needed, preferably ≤30 characters | Danger notice/prerequisite: requires PIE, editor graph not synced, validated ImportText |
| **Total** | **Preferably ≤100 characters** | Longer descriptions consume tools/list/SearchMode tokens; **registration no longer uses ensure for length** |

<a id="11-verb-标准词表"></a>
### 1.1 Standard VERB vocabulary

| Verb | Meaning | Example capability |
|---|---|---|
| Search | Filter a list, returning references/summaries rather than full data | SearchAsset |
| List | Enumerate existing runtime instances | ListRuntimeActors |
| Get | Read full data/properties | GetAssetBlueprint, GetRuntimeActorProperty |
| Set | Write properties, singly/in batches | SetRuntimeActorProperty |
| Manage | Structural addition/removal/editing of nodes, fields, rows, sections | ManageAssetBlueprint, ManageAssetDataTable |
| Create | Create an asset file | CreateAssetBlueprint |
| Delete | Delete an asset file | DeleteAsset |
| Rename | Move/rename an asset | RenameAsset |
| Duplicate | Copy an asset to a new path | DuplicateAsset |
| Import | First create an asset from an external file | ImportAsset |
| Save | Persist dirty assets | SaveAsset |
| Unload | Unload a loaded asset package from memory | UnloadAsset |
| Spawn | Instantiate at runtime | SpawnRuntimeActor, SpawnRuntimeWidget |
| Destroy | Remove a runtime instance | DestroyRuntimeActor |
| Diff | Compare two entities' properties | DiffRuntimeActors |
| Interact | Trigger UI actions: click/check/set | InteractRuntimeWidget |
| Control | Control editor state | ControlPie |
| Exec | Execute commands/scripts | ExecCommand, ExecPython |
| Capture | Take screenshots | CaptureViewport for runtime viewports/windows; CaptureEditorPanel for LevelEditor panels |
| Query | System-level read-only querying | GetAssetRefs, GetGameplayTags |

<a id="12-反例--正例对照"></a>
### 1.2 Bad → good examples

| Verb | Old example | New example | Problem |
|---|---|---|---|
| Get | "Read AnimMontage asset: slots/segments and sections." | "Inspect montage timeline. Slots/segments/sections snapshot; read-only, no playback." | Name overlap with Montage/Asset; no differentiation |
| List | "List Actors in the current World with filters." | "Enumerate live Actors. class/tag/name filters; PIE/Game only, returns refs not props." | Not differentiated from GetRuntimeActorProperty |
| Set | "Write properties to runtime Actors (per-update entry)." | "Batch-write Actor properties. One entry per target actor; PIE/Game only, immediate apply." | Ambiguous per-update entry |
| Manage | "Add/remove segments and sections in an AnimMontage asset." | "Edit montage structure. Add/remove slots, segments, sections; save separately." | Insufficient difference from GetAssetAnimMontage |
| Search | "Search project assets by type/path/query/name." | Retain; routing table in the description permits an exceptional longer length | — |
| Get | "Get the widget tree of a WidgetBlueprint." | "Read WidgetBlueprint tree. Widget hierarchy, bindings, slot props; editor only." | Insufficient difference from manage |
| Control | "Start/stop/status the PIE session." | Current length 36 satisfies the contract; retain | — |

---

<a id="2-元数据字段速查"></a>
## 2. Metadata field reference

<a id="21-唯一钩子所有元数据集中填写"></a>
### 2.1 Single hook for all metadata

First **choose a module** with §2.1.0, then **choose its base class** with §2.1.1, then override the required hooks. Module selection narrows valid bases. Execute is **final** in FNexusActionCapability / FNexusMultiSectionCapability; subclasses **must not** redeclare/implement Execute.

<a id="210-模块选择runtime--editor从上到下命中即停"></a>
#### 2.1.0 Module selection: Runtime / Editor, first match wins

NexusLink has NexusLink (Type: Runtime, eligible for cooked Game/DS) and NexusLinkEditor (Type: Editor, loaded only in Editor). **Base class determines module:** existing capabilities do not manually override GetHostScope(); inheritance determines host scope. Wrong bases break Game-target compilation.

| Condition | Module | Directory |
|------|------|------|
| UnLua: Lua VM / Blueprint UnLuaInterface binding | NexusLinkExt / NexusLinkExtEditor | Plugins/NexusLinkExt/Source/NexusLinkExt/Private/Capabilities/Lua/Runtime/ and .../NexusLinkExtEditor/Private/Capabilities/Lua/Asset/ |
| **Live objects in PIE/Game Worlds**, such as Actors, Widgets, ASCs, Niagara components, using only runtime APIs/reflection without disk saving | NexusLink (Runtime) | Source/NexusLink/Private/Capabilities/Runtime/<domain>/ |
| **Disk assets**, or UnrealEd / Kismet* / AssetTools / GEditor / editor transactions / asset compilation/persistence | NexusLinkEditor (Editor) | Source/NexusLinkEditor/Private/Capabilities/<domain>/ |
| Both | Split into two capabilities; if inseparable, use Editor and state standalone DS is unsupported | Place each according to the split above |

Chosen modules restrict bases to the corresponding Runtime/other subset below. Do not cross them. Plugins/NexusLink/scripts/audit_capability_params.py verifies base/path module consistency and FAILs mismatches.

<a id="211-基类选择从上到下命中即停"></a>
#### 2.1.1 Base-class selection: first match wins

| Condition | Inherit | Subclass implements |
|------|------|------|
| Manage* with Operations[] in Schema | FNexusActionCapability | BuildDefinition + RegisterActions + PrepareTarget; optional FinalizeTarget / AfterPrepareTarget |
| Read-only with Sections[], including All | FNexusMultiSectionCapability (Editor) or FNexusRuntimeMultiSectionCapability (PIE) | BuildDefinition with Out.InputSchema = BuildSchemaWithSections(), plus BuildCapabilitySchema / GetSectionNames / ExecuteSection |
| PIE/Game runtime, e.g. *Runtime*, ListRuntime*, GetOutputLog, SetLogCaptureFilter, ExecCommand, CaptureViewport, **without** Operations[] | FNexusRuntimeCapability | BuildDefinition + Execute |
| Other: Create*, save/delete/Rename/duplicate/unload, Get* without sections, ControlPie, ControlMoviePipeline, Set*Property | FNexusCapability | BuildDefinition + Execute |

- Top-level Action in Interact* / ControlPie / ControlMoviePipeline is a **command**, not batch Operations[]: **do not** use FNexusActionCapability.
- Set*Property uses Updates[]: choose FNexusCapability or FNexusRuntimeCapability.
- Default GetHostScope()=EditorOnly; runtime bases idempotently add the Runtime category tag.
- Plugin-gated capabilities wrap the class and REGISTER_MCP_CAPABILITY in #if WITH_*.
- **Loading scope:** NexusLink Type: Runtime enters cooked Game/DS; NexusLinkEditor Type: Editor loads only Editor binaries. REGISTER_MCP_CAPABILITY / REGISTER_MCP_TOOL are gated by NEXUSLINK_WITH_SERVER (!UE_BUILD_SHIPPING). MCP runs in Editor/PIE/non-Shipping standalone Game/DS. Platform gates need both PlatformAllowList (UE5) and WhitelistPlatforms (UE4.2x).

<a id="212-扩展插件第三方插件-capability"></a>
#### 2.1.2 Extension plugins for third-party capabilities

Third-party-dependent capabilities belong in extension plugins, not NexusLink/NexusLinkEditor. The current extension is [NexusLinkExt](https://github.com/bytepine/NexusLinkExt). The main plugin does not scan extension directories; it recognizes the two registration mechanisms below.

| Topic | Rule |
|------|------|
| Modules | Use existing NexusLinkExt (Runtime), NexusLinkExtEditor (Editor). Put new domains under Capabilities/<domain>/ rather than creating modules |
| Base classes | Follow §2.1.1. End .cpp with REGISTER_MCP_CAPABILITY, sharing the main registry |
| Gate macros | Only the module **first** linking an optional plugin adds WITH_* to PublicDefinitions. Another linking module must not redefine it, avoiding C4005 |
| Dangerous | Only arbitrary code-execution entry points, including arbitrary console commands/scripts (§2.3), add FNexusMcpTags::Dangerous to Out.Tags together with Write. The main plugin recognizes tags for default disablement, prompts, and -NexusEnableDangerousCaps; do not hardcode names into NexusLink |
| Handshake instructions | StartupModule reads its own Resources/InitializeInstructions.SearchMode.md and InitializeInstructions.MultiTool.md, calling FNexusInstructionRegistry::Get().Register(plugin-name, …); ShutdownModule calls Unregister. Fragments mention only that plugin's capabilities. The main plugin appends registered fragments by name |
| Tests | L1 lives in example-project Plugins/NexusLinkExtTestSuite with prefix NexusLinkExt., not the extension repository |

Set these fields in BuildDefinition as appropriate:

| Field | Assignment | Constraint |
|---|---|---|
| Out.Name | Required | PascalCase, globally unique, following §6 {Verb}{Scope}{Target}{Aspect?} |
| Out.Description | Required | Four-part format, preferably ≤100 characters; not hard-blocked during registration |
| Out.InputSchema | Required | FNexusSchema::Object().Prop(...).Required({...}).Build() |
| Out.Tags | Required | At least one access tag (readonly/Write) and one category |
| Out.ExtraSearchKeywords | Optional | Colloquialisms/synonyms; no duplicate name tokens, stripped at registration |
| Out.RelatedCapabilities | Optional | Capability names; fill for strongly related siblings such as get/manage pairs |
| Out.SearchAssetTypes | Optional | **Required for asset get/manage**: match SearchAsset AssetType, e.g. {"Blueprint"}. Registration builds the RecommendedGet/RecommendedManage routing index |
| Out.Prerequisites | Optional | Enum values below; fill when a specific runtime environment is required |
| Out.WhenToUse | Optional | ≤40 characters; useful with ≥3 same-prefix capabilities difficult to distinguish by description |

**Multi-section capabilities:**
FNexusMultiSectionCapability subclasses set Out.InputSchema = BuildSchemaWithSections() rather than constructing it directly; the base injects the sections enum. BuildCapabilitySchema() declares all other inputs.

<a id="22-旧字段映射废弃仅供历史参考"></a>
### 2.2 Legacy field mapping: deprecated, historical reference only

> These separate virtual functions were merged into BuildDefinition. **New capabilities must not use them.**

| Removed C++ hook | New field |
|---|---|
| `GetName()` | `Out.Name` |
| `GetDescription()` | `Out.Description` |
| `BuildSchema()` | `Out.InputSchema` |
| `BuildTags(OutTags)` | `Out.Tags` |
| `GetExtraSearchKeywords()` | `Out.ExtraSearchKeywords` |
| `GetRelatedCapabilities()` | `Out.RelatedCapabilities` |
| `GetPrerequisites()` | `Out.Prerequisites` |
| `GetWhenToUse()` | `Out.WhenToUse` |

<a id="23-tags-枚举"></a>
### 2.3 Tags enumeration

| Tag | Type | Meaning |
|---|---|---|
| readonly | Access | Read-only marker, **not a sandbox**; disabling write capabilities does not prevent other capabilities modifying the project |
| Write | Access | Changes data/state |
| Dangerous | Access | **Only arbitrary code execution**, including arbitrary console commands/scripts. **Excluded from category trees/search keywords**. Must also include Write. All disabled by default; see settings Confirm/Custom access modes |
| Editor | Category | General/editor-only |
| blueprint | Category | Blueprint assets |
| Material | Category | Material / MaterialInstance assets |
| struct | Category | UserDefinedStruct assets |
| Data | Category | DataTable / DataAsset |
| Widget | Category | UMG / WidgetBlueprint |
| Runtime | Category | PIE/Game operations; added automatically by runtime bases or explicitly |
| gas | Category | GameplayAbility System, registered with WITH_GAS=1 |

**Dangerous classification, shared by main/extension plugins:** only capabilities executing arbitrary code may use Dangerous, e.g. ExecCommand, ExecPython, EvalRuntimeLua, DofileRuntimeLua. Dedicated capabilities must not be marked dangerous merely for writes, irreversibility, configuration changes, UI interaction, tests, or compilation. Shared write helpers must not add Dangerous automatically.

> **Host filtering ignores Tags:** GetHostScope() inherited from bases determines Game/DS visibility; do not gate through hand-written Runtime/Editor tags.

<a id="24-prerequisites-枚举值"></a>
### 2.4 Prerequisites enum values

| Value | Meaning |
|---|---|
| PIE | Requires a running PIE/Game session |
| python | Requires enabled Python Editor Script Plugin with an initialized interpreter |
| EditorOnly | Available only in Editor mode, outside PIE |
| ds_mode | Meaningful only in Dedicated Server mode |

---

<a id="3-extrasearchkeywords-词典"></a>
## 3. ExtraSearchKeywords dictionary

<a id="31-全局禁用词注册期自动剥离写了也无效"></a>
### 3.1 Globally excluded terms, stripped during registration

These terms come from Tags or match nearly every capability and do not distinguish them:

`Asset`, `Runtime`, `Editor`, `blueprint`, `Widget`, `Material`, `struct`, `Data`, `Actor`

All name tokens are automatically stripped too.

<a id="32-高优先口语词建议在对应-cap-的-keywords-里加"></a>
### 3.2 High-priority colloquial keywords to consider

| User expression | Suggested capability |
|---|---|
| `flag`, `variable`, `var`, `field`, `Property` | `ManageAssetBlueprint`, `GetAssetBlueprint` |
| `Node`, `pin`, `wire`, `graph`, `connection` | `ManageAssetBlueprint`, `ManageAssetMaterial` |
| `Tag`, `gameplay tag`, `gtag` | `GetGameplayTags` |
| `Log`, `Output`, `console` | `GetOutputLog`, `ExecCommand` |
| screenshot, snap, capture, photo | CaptureViewport; CaptureEditorPanel for editor panel tabs |
| `spawn`, `create actor`, `instantiate` | `SpawnRuntimeActor` |
| `kill`, `remove actor`, `delete actor` | `DestroyRuntimeActor` |
| `Montage`, `play`, `anim slot` | `GetAssetAnimMontage`, `GetRuntimeActorAnimation`, `InteractRuntimeActorAnimation` |
| Texture, 贴图, image, tga, png | GetAssetTexture |
| `static mesh`, `sm`, `mesh asset` | `GetAssetStaticMesh` |
| `anim sequence`, `clip`, `frame` | `GetAssetAnimSequence` |
| `skeletal mesh`, `skmesh` | `GetAssetSkeletalMesh`, `GetAssetSkeleton` |
| `Sound`, `audio`, `sfx` | `GetAssetSoundWave`, `GetAssetSoundCue` |
| `niagara`, `vfx`, `fx` | `GetAssetNiagaraSystem` |
| Level, map, umap, World | GetAssetLevel for a disk level with known AssetPath |
| current map, open level, loaded map | GetEditorContext section CurrentMap; current open editor level, not PIE World |
| `state machine`, `Transition`, `anim graph` | `ManageAssetAnimBlueprint`, `GetAssetAnimBlueprint` |
| `Blackboard`, `bb key` | `GetAssetBlackboard`, `GetRuntimeActorBehaviorTree` |
| `bt node`, `task`, `Service`, `Decorator` | `GetAssetBehaviorTree`, `ManageAssetBehaviorTree` |
| `PIE`, `play in editor`, `Simulate` | `ControlPie` |
| `diff`, `compare`, `delta` | `DiffRuntimeActors` |
| `ref`, `dependency`, `reference`, `referencer`, `inheritance`, `subclass`, `parent class` | `GetAssetRefs` |
| `slider`, `button`, `text`, `image`, `checkbox` | `InteractRuntimeWidget`, `GetRuntimeWidgetProperty` |
| `struct field`, `struct type` | `ManageAssetStructField`, `GetAssetStruct` |
| `Row`, `datatable row` | `ManageAssetDataTable`, `GetAssetDataTable` |
| `cdo`, `default Value`, `class default` | `GetAssetBlueprint` (Section=defaults), `ManageAssetDataAsset` |

Native C++ UENUM/header reflection is outside scope; use GetAssetEnum for UserDefinedEnum assets.

---

<a id="4-提交前自检清单6-条"></a>
## 4. Pre-submission checklist

Before committing or PR review, verify every added/changed capability:

- [ ] **Format:** Description follows four-part format, preferably ≤100 characters, with at least one period.
- [ ] **No overlap:** Description excludes major name tokens; registration overlap ≤30%.
- [ ] **Differentiation:** same-prefix descriptions have clear DIFFERENTIATOR wording.
- [ ] **Keywords:** ExtraSearchKeywords contains colloquialisms/synonyms without name tokens.
- [ ] **Relations:** RelatedCapabilities lists strongly related siblings, usually get/manage pairs.
- [ ] **SearchAssetTypes:** asset GetAsset*/ManageAsset* declare matching SearchAsset AssetType; nonasset capabilities leave it empty.
- [ ] **Naming:** Out.Name follows §6 decision rules; nonpattern names appear in InitializeInstructions exceptions.
- [ ] Runtime PIE/Game/DS capabilities inherit FNexusRuntimeCapability / FNexusRuntimeMultiSectionCapability; asset/editor capabilities keep default bases.

> Registration FNexusCapabilityRegistry::Register() hard-validates only naming verbs (§6). Description length and §4 format/overlap/keyword counts rely on checklist and scripts/audit_capability_naming.py CI, avoiding startup ensure crashes in Dev builds.

---

<a id="5-新增-capability-完整流程"></a>
## 5. Complete Capability addition workflow

```
1. Ensure a globally unique PascalCase name following §6 {Verb}{Scope}{Target}{Aspect?}.
2. Implement BuildDefinition(Out):
   a. Write four-part Out.Description; compare DIFFERENTIATOR with same-prefix capabilities.
   b. Out.InputSchema = FNexusSchema::Object().Prop(...).Required({...}).Build()
      Use BuildSchemaWithSections() when inheriting FNexusMultiSectionCapability.
   c. Out.Tags needs at least readonly/Write plus a category tag.
   d. Fill Out.ExtraSearchKeywords / Out.RelatedCapabilities / Out.WhenToUse / Out.Prerequisites as needed.
   e. For asset get/manage, fill Out.SearchAssetTypes, e.g. {"Blueprint"}, for SearchAsset RecommendedGet/Manage.
3. Implement Execute(Arguments).
4. End .cpp with REGISTER_MCP_CAPABILITY(YourCapClass).
5. Compile and inspect Output Log for no ensureMsgf warnings.
6. Pass the pre-submission checklist (§4).
7. Update CHANGELOG.md [Unreleased] in sync.
8. Update [InitializeInstructions.SearchMode.md](InitializeInstructions.SearchMode.md) / [InitializeInstructions.MultiTool.md](InitializeInstructions.MultiTool.md), see §6.6.
```

---

<a id="6-capability-命名规范"></a>
## 6. Capability naming

> Authoritative for AI routing/registration checks; the full exception table is in [InitializeInstructions.SearchMode.md](InitializeInstructions.SearchMode.md).

<a id="61-全局格式"></a>
### 6.1 Global format

- PascalCase, globally unique, without underscores between words.
- Boolean fields start with b followed by an uppercase letter: bSaveToDisk, bCompile, bEnabled.
- Private C++ Capability classes use FNexus{PascalCase}Capability, in Nexus{PascalCase}Capability.{h,cpp}; removing F gives the filename.
- Order: {Verb}{Scope}{Target}{Aspect?}. Asset/Runtime scope must appear in the name; Editor/general exceptions are in §6.4.
- JSON Schema keywords (type, properties, required, enum) and standard MCP/JSON-RPC fields retain protocol spelling rather than this PascalCase convention: capabilities, instructions, listChanged, inputSchema, annotations, readOnlyHint, code, message, jsonrpc, method, params, isError, structuredContent.
- Discovery/proxy base protocols also retain spelling: GET /status server/wsPort/projectName/engineVersion/netRole/hostKind/bHasPlayWorld/authRequired; registration-file pid/mcpPort/wsPort/projectName/engineVersion/authToken; WS auth params.token/result.ok; method/field names for status, nexus/instructions, nexus/proxy_config, nexus/proxy_feedback. These are not Capability business parameters; renaming breaks discovery/connections in released proxies.

<a id="62-asset-盘编辑器资产"></a>
### 6.2 Asset scope: editor assets

- **Default:** {Get|Manage|Create}Asset{Type}, such as Blueprint / Material / UserWidget / AnimBlueprint.
- **Manage** means structural disk-asset changes to nodes/rows/fields/sections, **never** runtime commands such as PIE playback or ability activation.
- **General exceptions, full names:** SearchAsset, SaveAsset, RenameAsset, DuplicateAsset, DeleteAsset, UnloadAsset, GetAssetRefs, ManageAssetStructField.

### 6.3 Runtime（PIE / Game）

- **Default:** {Verb}Runtime{Target}{Aspect?}.
- **Verb choice:**
  - **Get:** read-only snapshots / Sections.
  - **Set:** only *Property reflection writes with PropertyPaths.
  - **Interact:** multi-Action commands, e.g. InteractRuntimeWidget; montage playback is InteractRuntimeActorAnimation.
  - **List / Spawn / Destroy / Diff:** enumeration, creation, destruction, comparison.
- **Pairs:** command writes use Get* + Interact*; property writes Get* + Set*. Do not name montage playback SetRuntimeActorAnimation just for symmetry.
- **Plurality:** ListRuntimeActors / DiffRuntimeActors have plural targets; SpawnRuntimeActor / DestroyRuntimeActor singular.

<a id="64-例外登记表非-assetruntime-pattern"></a>
### 6.4 Exception register outside Asset/Runtime patterns

The Instructions exceptions section must match this table's **all-version rows 1:1**. Gated GAS/StateTree/MVVM/etc. capabilities appear only in Instructions plugin gates, not all-version type/exception lists:

| Category | Capability |
|------|------------|
| Editor (5) | `CaptureEditorPanel`, `CompileBlueprint`, `ControlPie`, `GetEditorInfo`, `GetGameplayTags` |
| General assets (7) | See §6.2 exception row |
| Nonstandard runtime | DiffRuntimeActors, GetRuntimeSlateWidget, GetOutputLog, SetLogCaptureFilter, ExecCommand, CaptureViewport |
| GAS (`WITH_GAS=1`, 14) | `create/get/ManageAssetGameplayAbility`, `create/get/ManageAssetGameplayEffect`, `create/get/ManageAssetAttributeSet`, `create/get/ManageAssetGameplayCueNotify`, `GetRuntimeActorAbilitySystem`, `InteractRuntimeActorAbilitySystem` |
| StateTree (`WITH_STATETREE=1`, UE 5.5+, 3) | `GetAssetStateTree`, `ManageAssetStateTree`, `CreateAssetStateTree` |
| MVVM (`WITH_MVVM=1`, UE 5.5+, 2) | `GetAssetViewModel`, `ManageAssetViewModel` |
| Paper2D (`WITH_PAPER2D=1`, 9) | `create/get/ManageAssetPaperSprite`, `create/get/ManageAssetPaperFlipbook`, `create/get/ManageAssetPaperTileMap` |
| GeometryCollection (`WITH_GEOMETRY_COLLECTION=1`, UE5+, 3) | `create/get/ManageAssetGeometryCollection` |
| CommonUI (`WITH_COMMON_UI=1`, UE5+, 6) | `create/get/ManageAssetCommonButtonStyle`, `create/get/ManageAssetCommonTextStyle` |
| MoviePipeline (`WITH_MOVIE_RENDER_PIPELINE=1`, UE5+, 4) | `create/get/ManageAssetMoviePipelineConfig`, `ControlMoviePipeline` |
| PoseSearch (`WITH_POSE_SEARCH=1`, UE 5.4+, 3) | `create/get/ManageAssetPoseSearch` |
| Python (`WITH_NEXUS_PYTHON=1`, 2) | `ExecPython`, `GetPythonApi` |

<a id="65-禁止复活与计划缺口"></a>
### 6.5 Removed names and planned gaps

| Forbidden name / gap | Explanation |
|-------------|------|
| ManageAnimation | Removed in v1.9; use InteractRuntimeActorAnimation |
| SetRuntimeActorAnimation | Misuses Set |
| Scopeless old names such as GetAssetGeneric | Deprecated |
| ManageAssetabl_ability | Able plugin capability; public NexusLink has no Able dependency and implements no get/manage |

<a id="66-relatedcapabilities-与文档同步"></a>
### 6.6 RelatedCapabilities and document synchronization

- Siblings: get↔manage for assets, get↔Set for properties, get↔interact for commands.
- **Never** reference unregistered names. Source audit_capability_naming.py checks before CI/run_e2e against all source, including #if WITH_* files.
- **Runtime:** FilterVisibleRelated removes names unregistered/invisible/disabled in the host from SearchCapabilities related, AttachMetaHints, and MultiTool [see:].
- Name/routing changes update §6 register, **SearchMode + MultiTool**, scripts/build_tool_reference.py regenerating both tool-reference languages, and CHANGELOG.

---

*This specification is authoritative for Capability metadata; §6 is the naming reference, §8 the Utils reference.*

---

<a id="8-utils-模块规范"></a>
## 8. Utils specification

> Follow this section when adding/changing any helper class under Utils/.

<a id="81-分层模型"></a>
### 8.1 Layers

Utils has seven dependency layers below. Dependencies go only from higher to lower layers, **never backward**:

| Layer | Meaning | Example files |
|---|---|---|
| **Common** | Engine-independent JSON/string/macro foundations | NexusJsonUtils, NexusStringMatchUtils, NexusResponseCompactorUtils, NexusVersionCompat (macros only) |
| **Result** | Capability result-envelope construction | NexusCapabilityResultBuilder |
| **Reflection** | UObject reflection/property reading/writing | NexusPropertyUtils, NexusPropertyReportUtils |
| **Asset** | Asset loading/Blueprints/graph structure | NexusAssetUtils, NexusBlueprintGraphUtils |
| **Runtime** | Runtime World/Actor/Widget | NexusRuntimeUtils, NexusCaptureUtils for Game viewport/Slate screenshots |
| **Domain** | Narrowly referenced domain logic | NexusMaterialUtils, NexusAnimGraphUtils, NexusWidgetAnimationUtils, NexusBehaviorTreeInspectUtils, NexusPinTypeUtils |
| **Editor** | WITH_EDITOR only | NexusPortUtils, NexusEditorContextUtils, NexusEditorCaptureUtils, NexusCapabilityIndexUtils |

**Dependency rules:** Common depends on no other layer. Result/Reflection depend only on Common. Asset and Runtime may depend on Reflection/Common. Domain/Editor may depend on Common; Editor may additionally depend on Asset.

<a id="82-文件命名"></a>
### 8.2 Filenames

- **All** utility classes use FNexus<Domain>Utils, in Nexus<Domain>Utils.h/.cpp.
- **Only exception:** macro-only files such as NexusVersionCompat.h, with no class, retain their names.
- No single-responsibility or short-name exemption. New files **must** have the Utils suffix.

<a id="83-类与-api-风格"></a>
### 8.3 Class and API style

```cpp
// Copyright byteyang. All Rights Reserved.

/** Chinese class-level JavaDoc */
class NEXUSLINK_API FNexus<Domain>Utils final
{
public:
    FNexus<Domain>Utils() = delete;

    /** Chinese method JavaDoc */
    static ReturnType MethodName(Params...);
};
```

Constraints:
- final class, deleted constructor, all public static, no state members.
- Public classes **must** export NEXUSLINK_API.
- Public methods **must** have Chinese JavaDoc comments (/** ... */).
- Private helpers are file-level static functions/variables in .cpp. Named/anonymous namespaces are forbidden; named helper groups use struct FXxx final with static members.
- File-scope enum/struct/class cannot be static. Nest them inside struct FXxx final or use a unique FNexus* name; generic EStatus/FScored names are forbidden.
- Follow Epic type prefixes U/A/F/E/I/T. Filenames equal class names without type prefixes.
- Variable names express their role. Avoid single letters/unreadable abbreviations; name loop indices after iterated items, e.g. SocketIndex, rather than always Index. Template T and engine members such as FVector::X are exceptions.
- Booleans start with b followed by uppercase; do not prefix constants with k.
- Protocol tool IDs, inputs, outputs, action names, tags use PascalCase, with b for booleans. See §6.1.
- scripts/audit_cpp_naming.py reports these mechanical rules without failure by default. Role-expressive variable naming is not scripted.

<a id="84-准入门槛"></a>
### 8.4 Admission criteria

| Situation | Treatment |
|---|---|
| Helper referenced by ≥2 Capabilities/Tools | **Must** move to the corresponding Utils layer |
| One reference, but >30 function lines **or** ≥3 combined helpers | Domain Utils allowed under §8.4 exemption; add comment // §8.4 exemption: single reference but complex logic |
| Other single reference | Keep a file-level Static helper in the Capability file |

<a id="85-跨版本兼容权威详版"></a>
### 8.5 Cross-version compatibility: authoritative detail

> audit_capability_naming.py rejects NX_UE_AT_LEAST( in **all NexusLink modules** except NexusVersionCompat.h.

**Three-way responsibility**

| Layer | Location | Rule |
|---|---|---|
| Semantic macros | NexusVersionCompat.h | Only here may use NX_UE_AT_LEAST(M,N). Expose NX_UE_HAS_<meaning> with comment // oldAPI → newAPI |
| Implementation | FNexus*Utils | All multiline #if NX_UE_HAS_* branches; do not copy the same difference across Capabilities |
| Capability | Capabilities/** | No NX_UE_AT_LEAST or ≥3-branch version #elif ladders; only Utils calls or single-line NX_UE_HAS_* |
| Editor/Server/etc. | Private/Editor/etc. | No bare NX_UE_AT_LEAST; use, e.g., NX_UE_HAS_APP_STYLE for FEditorStyle/FAppStyle |

**Adding API forks: four steps**

1. Look for reusable NexusVersionCompat.h macros, including aliases such as NX_UE_HAS_SKELETAL_MESH_ACCESSORS.
2. If absent, add NX_UE_HAS_* with a Chinese comment, without raw version checks in Capabilities.
3. Move logic into Utils for ≥2 callers or ≥3 #if lines at one caller; prefer FNexusAssetUtils for read-only asset fields.
4. build_test must fully pass compatibility floor UE_4.26; daily smoke/example host defaults UE_5.7. Add UE_5.0 and UE_5.6 when changing engine APIs. audit_capability_naming.py must PASS.

**Optional plugins:** WITH_GAS / WITH_NIAGARA / WITH_STATETREE / WITH_MVVM etc. guard whole .cpp files with Build.cs detection. WITH_UNLUA belongs in [NexusLinkExt](https://github.com/bytepine/NexusLinkExt). Detection searches project Plugins, {Project}/../Engine/Plugins, and Plugins under UBT EngineDirectory. Explicit Enabled: false in the actual .uproject disables them. Host build_test compiles all Nexus.uproject: NexusEditor detects optional capabilities from **that engine's** disk as the host does. Nexus Game has WITH_EDITOR=0; Editor-type NexusLinkEditor stays out of cooked Game, Runtime-type NexusLink enters it. Bare NX_UE_AT_LEAST remains forbidden.

**Registered macros, excerpt; NexusVersionCompat.h is authoritative**

| Macro | Meaning |
|---|---|
| NX_UE_HAS_TEXTURE_* | Texture PlatformData / GetSurface* |
| NX_UE_HAS_STATIC_MESH_ACCESSORS | StaticMesh material-slot / BodySetup getters |
| NX_UE_HAS_SKELETAL_MESH_* | SkeletalMesh material / Physics / Skeleton getters |
| NX_UE_HAS_ANIM_SEQUENCE_DATA_MODEL | AnimSequence frame count/rate through DataModel, 5.6+ |
| `NX_UE_HAS_ANIM_SEQUENCE_LOOP_FIELD` | AnimSequence::bLoop（5.1+） |
| `NX_UE_HAS_SOUND_NODE_WAVE_ACCESSOR` | SoundNodeWavePlayer::GetSoundWave |
| NX_UE_HAS_NIAGARA_* | Niagara emitter enumeration / ExposedParameters API |
| NX_UE_HAS_APP_STYLE | Editor Slate FEditorStyle → FAppStyle |
| `NX_UE_HAS_EQS` | Environment Query System（EQS）；UE5.0+ |
| NX_UE_HAS_UMG_SLOT_GETTERS | UMG PanelSlot layout getters, Canvas/Box/Grid, 5.1+ |
| NX_UE_HAS_SLATE_BOX_PANEL_SLOT_GETTERS | Slate BoxPanel child Slot export, GetSlot + getters, 5.0+ |
| NX_UE_HAS_CONTENT_BROWSER_ITEM_PATH | Content Browser GetCurrentPath() → FContentBrowserItemPath; UE4 uses no-argument FString |
| NX_UE_HAS_ASSET_SOFT_OBJECT_PATH | FAssetData::GetSoftObjectPath(); UE4 ObjectPath |
| NX_UE_HAS_TAG_SEARCHABLE_REFERENCERS | AssetRegistry GetReferencers(SearchableName) finds packages by GameplayTag |
| NX_UE_HAS_WIDGET_BLUEPRINT_OPERATIONS | Public UE 5.8 Widget Blueprint structure API; older engines limit new actions |
| NX_UE_HAS_K2_PIN_REAL | Blueprint PC_Real pin; UE4 only PC_Float |

<a id="86-if-守卫规则"></a>
### 8.6 #if guard rules

- WITH_EDITOR / NX_UE_HAS_* wrap only **necessary branches**. Do not guard entire class bodies in headers.
- Editor-only Utils filenames must show that scope, e.g. NexusEditor*Utils / NexusPort*Utils.

<a id="87-依赖单向性硬限制"></a>
### 8.7 One-way dependencies: mandatory

- Utils must not #include headers under Capabilities/ or Tools/.
- Common must not depend on any other listed layer.

<a id="88-通用接口约定禁止重复"></a>
### 8.8 Shared interfaces: no duplication

New Capabilities **must** use Utils for these responsibilities rather than copy logic:

| Responsibility | Shared interface |
|---|---|
| offset/limit pagination | FNexusJsonUtils::ComputeSlice(Total, Offset, Limit, OutStart, OutEnd) |
| NameFilter / PropertyPaths / sections parsing | FNexusJsonUtils::Parse* |
| Reflected field reports for DataTable/DataAsset/Struct | FNexusPropertyReportUtils::BuildFieldReport(...) |
| Capability envelopes / entry errors | FNexusCapabilityResultBuilder::AddEntry(...) / AddEntryError(...). Adapter AssembleStructuredContent promotes Entries.Num()==1 and uses results[] for Num()>1. Response identity is Path; get_/manage_ may StripRedundantPathEcho |
| Runtime World lookup/error handling | FNexusRuntimeUtils::RequirePlayWorld(OutError) |
| New asset finalization: MarkDirty + AssetCreated + Save | FNexusAssetUtils::NotifyAndSaveCreated(...) / NotifyCompileAndSave(...) |
| Manage finalization: optional compile/bSaveToDisk | FNexusCapability::Run / Finalize centrally invoke FNexusEditorServices::ApplyManageFinalize(...); do not reimplement per manage Execute |

<a id="89-capability-实现样板约束"></a>
### 8.9 Capability implementation constraints

FNexusActionCapability / FNexusMultiSectionCapability subclasses **must not** declare/implement Execute. Other Execute(), Action handlers, and MultiSection ExecuteSection follow:

- No raw FCapabilityResult _R + lambda + _R.Entries boilerplate. Use FNexusCapabilityResultBuilder::AddEntry(...). Action handlers only write Ctx.Entry; bases perform OutEntries.Add.
- Entry errors: ordinary capabilities use AddEntryError(OutEntries, ErrCode, Msg). **Action handlers** write Ctx.Entry Error then return; no OutEntries.Add / AddEntryError.
- No hand-written null-check chains for World lookup; use FNexusRuntimeUtils::RequirePlayWorld(OutError).
- No hand-written MarkPackageDirty + AssetCreated + Save finalization sequence for new assets; use the corresponding FNexusAssetUtils interface.
- New Manage* with Operations[] **must** inherit FNexusActionCapability with RegisterActions static dispatch. No custom ExtractOperations loop shells; parse through FNexusArgs.
- **Check returned pointers before dereferencing.** If XxxFrom(Ctx), LoadObject, etc. can return nullptr, callers must check before ->. Action handlers set Ctx.Entry Error and return; pointer helpers return nullptr. Wrapping subsequent -> inside if (T* P = Foo()) also satisfies this. Returning nullptr only in a helper while its caller directly dereferences still triggers static checks.

<a id="810-提交前自检6-条"></a>
### 8.10 Pre-submission checks: six items

1. Class/filenames follow §8.2 FNexus<Domain>Utils unless exempt.
2. APIs are all Static, public classes export NEXUSLINK_API, public methods have Chinese JavaDoc.
3. Includes respect §8.7 dependency direction.
4. NX_* macros are not bypassed (§8.5).
5. No duplicated helpers; use shared §8.8 interfaces.
6. Capability implementations contain none of the forbidden §8.9 patterns.

<a id="811-manage--create--execute-契约breaking-权威"></a>
### 8.11 Manage / Create / Execute contract: authoritative breaking changes

> **Breaking:** no transition compatibility. Old keys/multiple-target arrays/ops/top-level Action synthesis reject as ArgInvalid, or read as empty and fail business checks. Authoritative mappings are in §8.12.

| Topic | Contract |
|---|---|
| Single target | **Capabilities have one target only:** Schema/Execute must not expose/consume assetPaths/actorNames/widgetNames. Cross-target batches use only meta-tool CallCapability.Calls[] |
| Collections within one target | **Retain** Sections / PropertyPaths / Operations / Updates and domain arrays below |
| Manage commands | Schema exposes only Operations: [{Action, ...}], never ops or bare top-level Action as a batch list |
| Execute input | FNexusActionCapability Execute invokes ExtractOperations for Manage* with Operations[]; subclasses must not add loop shells. Read only Operations[], without ops fallback or synthesis from top-level Action/other fields. Nonobject items create their entries[].error "Invalid operation (expected object)", never silent skipping |
| Strict Schema | FNexusSchema::Object() defaults additionalProperties:false; AnyObject() explicitly true for dynamic fields; AnyScalar() type:[string,number,boolean,null]. Before Execute, FNexusCapability::Run recursively validates InputSchema: unknown keys, type as string/array, required/missing empty strings, enum, array items, nested objects. All failures use FCapabilityResult::MakeArgInvalid |
| Result envelopes | No double wrapping of one Entry containing results[]. Each operation has one independent OutEntries.Add(...), promoted/wrapped centrally by AssembleStructuredContent |
| bSuccess | Do not write bSuccess on success. Errors or explicit compile/save failures aggregate through HasErrors into MCP isError. Constant-true success flags or always-true conditional bSuccess are forbidden |
| Create input | Expose/consume only AssetPath, removing separate PackagePath + assetName |
| Create response | Successful entries must contain Path |
| Manage finalization | Definition flags inject bSaveToDisk via bInjectSaveToDisk (Action base sets it for ManageAsset*) and bCompile via bInjectCompile (only BP/ABP/WBP manage BuildDefinition). Single Run auto-finalizes by default. Calls[] with ≥2 uses Run(args,false), delaying finalization until all business operations pass, then Finalize compilation. After all compiles pass, commit memory transaction and save. UDS autocompile, Material Recompile, explicit SaveAsset/CompileBlueprint are not delayed |
| Failure aggregation | FCapabilityResult::HasErrors() checks FatalError, top-level/Entries Error/CompileError/SaveError/FinalizeError and nonempty SectionErrors, without recursively scanning business data. SearchMode/MultiTool return isError=true and preserve details. Partially failed single manage retains successful operations |
| Batch rollback | Business/autocompile failures increase FailureCount, attempt recorded memory rollback, and skip automatic saving; bRolledBack/UndoNote describe actual boundaries. Save failure also returns isError=true without claiming committed memory/disk fully rolled back. Package operations/runtime side effects lack editor-transaction protection |
| Execute hygiene | Non-MultiSection capabilities prefer FNexusCapabilityResultBuilder::Build. Asset lookup consistently uses RequireString + EmitError or corresponding Fatal/MakeArgInvalid. Bare SetStringField("Error") cannot be the sole failure path |
| Intentionally retained | Single-target Sections/PropertyPaths/Operations/Updates; domain arrays inside Operations rather than top-level Fields/Rows/Keys/Widgets operation containers; top-level command Action for runtime Interact*, ControlPie, ControlMoviePipeline; meta-tool Calls[] |

<a id="812-参数权威表--breaking-迁移"></a>
### 8.12 Authoritative parameters / breaking migration

Old Arguments keys → strict Run validation or undeclared Schema → **ArgInvalid** via MakeArgInvalid. Silent alias mapping is forbidden.

| Old key / shape | Authoritative key / shape | Explanation |
|---|---|---|
| assetPaths / actorNames / widgetNames | Removed → CallCapability.Calls[] | Capabilities are single-target; only meta-tools batch across targets |
| newPath | DestAssetPath | Copy/migration destination |
| FilePath | ScriptPath | Script disk/project-relative path |
| ownerWidget | OwnerClass | Widget-owning class |
| spawn blueprintPath | AssetPath | Consistent spawn Blueprint asset path |
| classPath | ClassName | Class name/path field standardized to ClassName |
| Top-level Fields/Rows/Keys/Widgets as operations | Operations[] | Only Operations is the list. Retained domain arrays belong inside an operation, never replacing Operations at top level |
| ops | Removed | No fallback; Execute reads only Operations |
| Top-level Action synthesized into one operation | Removed | Manage batches cannot fall back to top-level Action |
| PackagePath + assetName | AssetPath | Create/etc. accept full AssetPath only |
| Singular get PropertyPath | PropertyPaths | Gets retain plural arrays only; singular → ArgInvalid |
| Undeclared Schema key | — | Object() defaults additionalProperties:false; unknown keys → ArgInvalid |


<a id="编辑器长操作契约"></a>
## Editor long-operation contract

Engine Slate/Automation/Live Coding belongs in NexusLinkEditor; third-party plugin capabilities belong in NexusLinkExtEditor.

FNexusCapabilityDefinition may declare OutputSchema, bAutoTransaction, bAllowBatch, bCacheable. Defaults preserve compatibility; nontransactional editor actions disable the last three. Multi-call batches check bAllowBatch **before** execution, not after side effects.

FNexusEditorOperations stores bounded state with Owner from request context. Access is GameThread-only; result snapshots are independently copied. Asynchronous callbacks hold responses/identities, not dispatch-stack references. Waits are at most 30 seconds; stopping waits differs from stopping underlying work. New tools return operation envelopes with output Schema. Errors never masquerade as successful completion.

Optional discovery ExecutionInfo callbacks return only session-fixed backend/reason as Execution for SearchCapabilities, without introducing specific engine plugin types into the core module.
