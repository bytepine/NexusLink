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

		AddStandardDependencies(this, Target);
		// NexusLink（Runtime）里 NexusMcpAuth / NexusLanHost 仍是 Private 头（无 UnrealEd 依赖，
		// 无需为区区两个设置面板要用的静态工具类整体提升为 Public API），设置定制面板需要直接
		// #include 它们；加一条 PrivateIncludePaths 而不是搬到 Public，缩小编辑器面暴露的表面积。
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "..", "NexusLink", "Private"));

	}

	public static void AddStandardDependencies(ModuleRules Module, ReadOnlyTargetRules Target)
	{
		Module.PublicDependencyModuleNames.AddRange(new[] { "Core", "DeveloperSettings" });
		Module.AddEngineThirdPartyPrivateStaticDependencies(Target, "FBX");
		if (Target.bCompileRecast) Module.PrivateDependencyModuleNames.Add("Navmesh");
		Module.PrivateDependencyModuleNames.Add(NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target,5,0) ? "ToolWidgets" : "EditorWidgets");
		Module.PrivateDependencyModuleNames.AddRange(new[]
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

		// UE4 的 PhysicsAssetUtils 由 UnrealEd 提供，静态网格编辑通过可选的 EditorScriptingUtilities 反射调用。
		if (NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target, 5, 0))
			Module.PrivateDependencyModuleNames.AddRange(new[] { "PhysicsUtilities", "StaticMeshEditor" });
		// 低版本通过官方动画库按骨骼名采样，避免把 Skeleton 索引误当作 Track 索引。
		// 4.x 的接口在 AnimationModifiers；5.0/5.1 拆到 AnimationBlueprintLibrary。
		if (!NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target, 5, 2))
		{
			Module.PrivateDependencyModuleNames.Add("AnimationModifiers");
			if (NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target, 5, 0))
				Module.PrivateDependencyModuleNames.Add("AnimationBlueprintLibrary");
		}

		// LiveCoding 仅 Windows 平台存在
		if (Target.Platform == UnrealTargetPlatform.Win64)
			Module.PrivateDependencyModuleNames.Add("LiveCoding");

		// 只读 .utrace。TraceServices 是引擎 Developer 模块，只链进 Editor，不进 Runtime/Game。
		if (NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target, 4, 26))
		{
			Module.PrivateDependencyModuleNames.Add("TraceServices");
			Module.PrivateDependencyModuleNames.Add("TraceAnalysis");
		}
		if (NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target, 5, 7)) Module.PrivateDependencyModuleNames.Add("CSVUtils");
		if (NexusLinkOptionalPlugins.MeetsMinEngineVersion(Target, 5, 3))
			Module.PrivateDependencyModuleNames.AddRange(new[] { "BehaviorTreeEditor", "AIGraph" });

		Module.PrivateDependencyModuleNames.AddRange(new[] { "AutomationController", "SessionServices" });
	}
}
