// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

/// <summary>
/// NexusLinkEditor（Editor 模块）：192 个 EditorOnly cap、编辑器 Utils、Slate 设置定制 / 状态栏 /
/// UpdateChecker。只在完整 Editor 宿主加载（Type=Editor，Game/DS 目标不编译本模块）。
/// 依赖 NexusLink（Runtime）取 Public API（Capability 基类、Registry、Editor 服务钩子表等）。
/// </summary>
public class NexusLinkEditor : ModuleRules
{
	public NexusLinkEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);

		PublicDependencyModuleNames.AddRange(new[] { "Core", "DeveloperSettings" });
		AddEngineThirdPartyPrivateStaticDependencies(Target, "FBX");
		if (Target.bCompileRecast) PrivateDependencyModuleNames.Add("Navmesh");
		PrivateDependencyModuleNames.Add(NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target,5,0) ? "ToolWidgets" : "EditorWidgets");
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"NexusLink",
			"CoreUObject", "Engine", "Projects", "Json", "JsonUtilities",
			"AssetRegistry", "UMG", "GameplayTags", "AIModule", "GameplayTasks", "NavigationSystem",
			"ImageWrapper", "AnimGraphRuntime", "RenderCore", "PhysicsCore",
			"LevelSequence", "MovieScene", "MovieSceneTracks", "Foliage", "MediaAssets",
			"Slate", "SlateCore", "ApplicationCore", "InputCore",
			"HTTP",
			// EditorStyle：UE 4.26–5.0 的 FEditorStyle::GetBrush/GetFontStyle 符号（NX_UE_HAS_APP_STYLE 判定为 false
			// 时走这条路径）；5.0 本身部分引擎 Slate 头仍内联引用该符号，即便本插件代码已切到 FAppStyle 也需要链接
			"Settings", "UnrealEd", "LevelEditor", "EditorStyle", "EditorSubsystem",
			"KismetCompiler", "Kismet", "BlueprintGraph", "AnimGraph", "UMGEditor", "AssetTools",
			"MaterialEditor", "PropertyEditor", "ContentBrowser", "ContentBrowserData",
			"SourceControl", "AudioEditor",
		});

		// UE4 的 PhysicsAssetUtils 由 UnrealEd 提供，UE5 起拆入 PhysicsUtilities。
		if (NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target, 5, 0))
			PrivateDependencyModuleNames.Add("PhysicsUtilities");
		PrivateDependencyModuleNames.Add(NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target, 5, 0) ? "StaticMeshEditor" : "EditorScriptingUtilities");
		// 低版本通过官方动画库按骨骼名采样，避免把 Skeleton 索引误当作 Track 索引。
		// 4.x 的接口在 AnimationModifiers；5.0/5.1 拆到 AnimationBlueprintLibrary。
		if (!NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target, 5, 2))
		{
			PrivateDependencyModuleNames.Add("AnimationModifiers");
			if (NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target, 5, 0))
				PrivateDependencyModuleNames.Add("AnimationBlueprintLibrary");
		}

		// NexusLink（Runtime）里 NexusMcpAuth / NexusLanHost 仍是 Private 头（无 UnrealEd 依赖，
		// 无需为区区两个设置面板要用的静态工具类整体提升为 Public API），设置定制面板需要直接
		// #include 它们；加一条 PrivateIncludePaths 而不是搬到 Public，缩小编辑器面暴露的表面积。
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "..", "NexusLink", "Private"));

		// LiveCoding 仅 Windows 平台存在
		if (Target.Platform == UnrealTargetPlatform.Win64)
			PrivateDependencyModuleNames.Add("LiveCoding");

		// 只读 .utrace。TraceServices 是引擎 Developer 模块，只链进 Editor，不进 Runtime/Game。
		if (NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target, 4, 26))
		{
			PrivateDependencyModuleNames.Add("TraceServices");
			PrivateDependencyModuleNames.Add("TraceAnalysis");
		}
		if (NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target, 5, 7)) PrivateDependencyModuleNames.Add("CSVUtils");
		if (NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target, 5, 3))
			PrivateDependencyModuleNames.AddRange(new[] { "BehaviorTreeEditor", "AIGraph" });

		string ProjectRoot = NexusLinkOptionalPlugins.FindProjectRoot(ModuleDirectory);
		var SearchDirs = NexusLinkOptionalPlugins.CollectPluginSearchDirs(ProjectRoot, this);
		if (NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target, 5, 9) && System.Environment.GetEnvironmentVariable("WITH_COMMON_UI") != "0")
		{
			var CommonUIDiagnostics = NexusLinkOptionalPlugins.Opt(new[] { "CommonUI.uplugin" }, "WITH_NEXUS_COMMON_UI_DEBUGGING", "NEXUS_COMMON_UI_DEBUGGING", 5, 9,
				rt: new[] { "CommonUIDebugging" }, uproj: "CommonUI");
			NexusLinkOptionalPlugins.Apply(this, Target, CommonUIDiagnostics, SearchDirs, ProjectRoot, false);
		}
		else PublicDefinitions.Add("WITH_NEXUS_COMMON_UI_DEBUGGING=0");

		PrivateDependencyModuleNames.AddRange(new[] { "AutomationController", "SessionServices" });
		// 语义检索直接链接领域服务，不加载官方工具注册表。
        var SemanticSearch = NexusLinkOptionalPlugins.Opt(new[] { "SemanticSearch.uplugin" }, "WITH_NEXUS_SEMANTIC_SEARCH", "NEXUS_SEMANTIC_SEARCH", 5, 8,
            rt: new[] { "SemanticSearch" }, uproj: "SemanticSearch", requireProjectEnable: true);
        if (NexusLinkOptionalPlugins.Apply(this, Target, SemanticSearch, SearchDirs, ProjectRoot, false))
            PrivateDependencyModuleNames.Add("DerivedDataCache");

		// Editor 模块覆盖全量可选插件表：190 个 EditorOnly cap 里用到的资产编辑/图编辑功能
		// 需要各插件的 Runtime 模块（资产数据类型）与 EditorModules（编译器/图编辑器）两部分。
		// WITH_GAS/WITH_NIAGARA/WITH_ENHANCED_INPUT 的宏已由 NexusLink（Runtime）PublicDefinitions 传递到本模块，
		// 这里只补链 EditorModules，不重复 Add 宏（避免 C4005 重定义）。
		foreach (var C in NexusLinkOptionalPlugins.BuildFullTable())
		{
			bool bSharedWithRuntime = C.Define == "WITH_GAS" || C.Define == "WITH_NIAGARA" || C.Define == "WITH_ENHANCED_INPUT";
			NexusLinkOptionalPlugins.Apply(this, Target, C, SearchDirs, ProjectRoot,
				bAllowEditorModules: true, bDefineMacro: !bSharedWithRuntime);
		}
        // Before UE 5.5 property bags lived in the StructUtils plugin module.
        if (PrivateDependencyModuleNames.Contains("PCGEditor") && !NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target, 5, 5))
            PrivateDependencyModuleNames.Add("StructUtils");
		if (PrivateDependencyModuleNames.Contains("PCGEditor") && NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target, 5, 7))
			PrivateDependencyModuleNames.Add("AssetDefinition");

	}
}
