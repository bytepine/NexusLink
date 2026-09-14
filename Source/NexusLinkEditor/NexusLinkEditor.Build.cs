// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

/// <summary>
/// NexusLinkEditor（Editor 模块）：195 个 EditorOnly cap、编辑器 Utils、Slate 设置定制 / 状态栏 /
/// UpdateChecker。只在完整 Editor 宿主加载（Type=Editor，Game/DS 目标不编译本模块）。
/// 依赖 NexusLink（Runtime）取 Public API（Capability 基类、Registry、Editor 服务钩子表等）。
/// </summary>
public class NexusLinkEditor : ModuleRules
{
	public NexusLinkEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);

		PublicDependencyModuleNames.AddRange(new[] { "Core", "DeveloperSettings" });
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"NexusLink",
			"CoreUObject", "Engine", "Projects", "Json", "JsonUtilities",
			"AssetRegistry", "UMG", "GameplayTags", "AIModule", "GameplayTasks",
			"ImageWrapper", "AnimGraphRuntime", "RenderCore", "PhysicsCore",
			"LevelSequence", "MovieScene", "MovieSceneTracks", "Foliage", "MediaAssets",
			"Slate", "SlateCore", "ApplicationCore", "InputCore",
			"HTTP",
			// EditorStyle：UE 4.26–5.0 的 FEditorStyle::GetBrush/GetFontStyle 符号（NX_UE_HAS_APP_STYLE 判定为 false
			// 时走这条路径）；5.0 本身部分引擎 Slate 头仍内联引用该符号，即便本插件代码已切到 FAppStyle 也需要链接
			"Settings", "UnrealEd", "LevelEditor", "EditorStyle",
			"KismetCompiler", "Kismet", "BlueprintGraph", "AnimGraph", "UMGEditor", "AssetTools",
			"MaterialEditor", "PropertyEditor", "ContentBrowser", "ContentBrowserData",
		});

		// NexusLink（Runtime）里 NexusMcpAuth / NexusLanHost 仍是 Private 头（无 UnrealEd 依赖，
		// 无需为区区两个设置面板要用的静态工具类整体提升为 Public API），设置定制面板需要直接
		// #include 它们；加一条 PrivateIncludePaths 而不是搬到 Public，缩小编辑器面暴露的表面积。
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "..", "NexusLink", "Private"));

		// LiveCoding 仅 Windows 平台存在
		if (Target.Platform == UnrealTargetPlatform.Win64)
			PrivateDependencyModuleNames.Add("LiveCoding");

		string ProjectRoot = NexusLinkOptionalPlugins.FindProjectRoot(ModuleDirectory);
		var SearchDirs = NexusLinkOptionalPlugins.CollectPluginSearchDirs(ProjectRoot, this);

		// Editor 模块覆盖全量可选插件表：195 个 EditorOnly cap 里用到的资产编辑/图编辑功能
		// 需要各插件的 Runtime 模块（资产数据类型）与 EditorModules（编译器/图编辑器）两部分。
		// WITH_UNLUA/WITH_GAS/WITH_NIAGARA 及 UNLUA_VERSION_MAJOR 的宏已由 NexusLink（Runtime）
		// PublicDefinitions 传递到本模块，这里只补链 EditorModules，不重复 Add 宏（避免 C4005 重定义）。
		foreach (var C in NexusLinkOptionalPlugins.BuildFullTable())
		{
			bool bSharedWithRuntime = C.Define == "WITH_UNLUA" || C.Define == "WITH_GAS" || C.Define == "WITH_NIAGARA";
			NexusLinkOptionalPlugins.Apply(this, Target, C, SearchDirs, ProjectRoot,
				bAllowEditorModules: true, bDefineMacro: !bSharedWithRuntime);
		}
	}
}
