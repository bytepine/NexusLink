// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class NexusLink : ModuleRules
{
	/// <summary>可选插件探测配置：UpluginFiles、MinEngine、模块、Define、EnvVar。</summary>
	private struct OptionalPluginConfig
	{
		public string[] UpluginFiles;
		public string UprojectPluginName; // 非空时检查 .uproject Enabled
		public int MinEngineMajor, MinEngineMinor;
		public string[] PublicRuntimeModules, RuntimeModules, EditorModules;
		public string Define, EnvVar;
		public bool EnvVarSupportsDisable; // 默认 true；UnLua 仅 =1
	}

	public NexusLink(ReadOnlyTargetRules Target) : base(Target)
	{
		ApplyCustomEngineCompatDefines(this);

		PublicDependencyModuleNames.AddRange(new[] { "Core", "DeveloperSettings" });
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"CoreUObject", "Engine", "Projects", "Sockets", "Networking", "Json", "JsonUtilities",
			"HTTP",
			// UE 5.0+ 将模块 HttpServer 重命名为 HTTPServer
			Target.GetType().GetProperty("Version") != null ? "HTTPServer" : "HttpServer",
			"WebSocketNetworking", "AssetRegistry", "UMG", "GameplayTags", "AIModule", "GameplayTasks",
			"ImageWrapper", "AnimGraphRuntime", "RenderCore", "PhysicsCore",
			"LevelSequence", "MovieScene", "MovieSceneTracks", "Foliage", "MediaAssets",
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new[]
			{
				"Settings", "Slate", "SlateCore", "EditorStyle", "UnrealEd", "LevelEditor",
				"KismetCompiler", "Kismet", "BlueprintGraph", "AnimGraph", "UMGEditor", "AssetTools",
				"MaterialEditor", "PropertyEditor", "ContentBrowser", "ContentBrowserData",
			});
			// LiveCoding 仅 Windows 平台存在
			if (Target.Platform == UnrealTargetPlatform.Win64)
				PrivateDependencyModuleNames.Add("LiveCoding");
		}

		string ProjectRoot = FindProjectRoot(ModuleDirectory);
		var SearchDirs = CollectPluginSearchDirs(ProjectRoot);

		// 可选插件表（顺序与旧版一致）
		bool bHasUnLua = false;
		foreach (var C in BuildOptionalPluginTable())
		{
			if (ApplyOptionalPlugin(Target, C, SearchDirs, ProjectRoot) && C.Define == "WITH_UNLUA")
				bHasUnLua = true;
		}
		ApplyUnLuaVersionDefines(bHasUnLua, SearchDirs);
	}

	private static OptionalPluginConfig[] BuildOptionalPluginTable()
	{
		return new[]
		{
			Opt(new[] { "UnLua.uplugin" }, "WITH_UNLUA", "WITH_UNLUA", pub: new[] { "Lua" }, rt: new[] { "UnLua" }, noDisable: true),
			Opt(new[] { "GameplayAbilities.uplugin" }, "WITH_GAS", "WITH_GAS", rt: new[] { "GameplayAbilities" }, uproj: "GameplayAbilities"),
			Opt(new[] { "Niagara.uplugin" }, "WITH_NIAGARA", "WITH_NIAGARA", rt: new[] { "Niagara" }, ed: new[] { "NiagaraEditor" }, uproj: "Niagara"),
			Opt(new[] { "StateTree.uplugin" }, "WITH_STATETREE", "WITH_STATETREE", 5, 5, new[] { "StateTreeModule" }, new[] { "StateTreeEditorModule" }),
			Opt(new[] { "ModelViewViewModel.uplugin" }, "WITH_MVVM", "WITH_MVVM", 5, 5, new[] { "ModelViewViewModel" }, new[] { "ModelViewViewModelBlueprint" }),
			Opt(new[] { "EnhancedInput.uplugin" }, "WITH_ENHANCED_INPUT", "WITH_ENHANCED_INPUT", 5, 0, new[] { "EnhancedInput" }, new[] { "EnhancedInputEditor" }),
			Opt(new[] { "ControlRig.uplugin" }, "WITH_CONTROL_RIG", "WITH_CONTROL_RIG", 5, 0, new[] { "ControlRig", "RigVM" }, new[] { "ControlRigDeveloper" }),
			Opt(new[] { "IKRig.uplugin" }, "WITH_IK_RIG", "WITH_IK_RIG", 5, 0, new[] { "IKRig" }, new[] { "IKRigEditor", "IKRigDeveloper" }),
			Opt(new[] { "Metasound.uplugin" }, "WITH_METASOUND", "WITH_METASOUND", 5, 0, new[] { "MetasoundEngine", "MetasoundFrontend", "MetasoundGraphCore" }, new[] { "MetasoundEditor" }),
			Opt(new[] { "PCG.uplugin" }, "WITH_PCG", "WITH_PCG", 5, 4, new[] { "PCG" }, new[] { "PCGEditor" }),
			Opt(new[] { "PoseSearch.uplugin" }, "WITH_POSE_SEARCH", "WITH_POSE_SEARCH", 5, 4, new[] { "PoseSearch" }, new[] { "PoseSearchEditor" }),
			Opt(new[] { "Paper2D.uplugin" }, "WITH_PAPER2D", "WITH_PAPER2D", rt: new[] { "Paper2D" }),
			// GeometryCollection 双候选 uplugin
			Opt(new[] { "GeometryCollectionPlugin.uplugin", "GeometryCollectionEngine.uplugin" }, "WITH_GEOMETRY_COLLECTION", "WITH_GEOMETRY_COLLECTION", 5, 0, new[] { "GeometryCollectionEngine" }),
			Opt(new[] { "CommonUI.uplugin" }, "WITH_COMMON_UI", "WITH_COMMON_UI", 5, 0, new[] { "CommonUI" }),
			Opt(new[] { "MovieRenderPipeline.uplugin" }, "WITH_MOVIE_RENDER_PIPELINE", "WITH_MOVIE_RENDER_PIPELINE", 5, 0, new[] { "MovieRenderPipelineCore", "MovieRenderPipelineSettings" }, new[] { "MovieRenderPipelineEditor" }),
		};
	}

	/// <summary>表项工厂，省略参数用默认值。</summary>
	private static OptionalPluginConfig Opt(
		string[] files, string define, string env,
		int minMajor = 0, int minMinor = 0,
		string[] rt = null, string[] ed = null,
		string uproj = null, string[] pub = null, bool noDisable = false)
	{
		return new OptionalPluginConfig
		{
			UpluginFiles = files, Define = define, EnvVar = env,
			MinEngineMajor = minMajor, MinEngineMinor = minMinor,
			RuntimeModules = rt, EditorModules = ed,
			UprojectPluginName = uproj, PublicRuntimeModules = pub,
			EnvVarSupportsDisable = !noDisable,
		};
	}

	private bool ApplyOptionalPlugin(ReadOnlyTargetRules Target, OptionalPluginConfig C,
		List<string> SearchDirs, string ProjectRoot)
	{
		bool on = MeetsMinEngineVersion(Target, C.MinEngineMajor, C.MinEngineMinor)
			&& DetectAnyEnginePlugin(SearchDirs, C.UpluginFiles);
		if (on && C.UprojectPluginName != null && ProjectRoot != null
			&& IsPluginExplicitlyDisabledInUproject(ProjectRoot, C.UprojectPluginName))
			on = false;

		string ev = System.Environment.GetEnvironmentVariable(C.EnvVar);
		if (ev == "1") on = true;
		if (C.EnvVarSupportsDisable && ev == "0") on = false;

		if (on)
		{
			if (C.PublicRuntimeModules != null) PublicDependencyModuleNames.AddRange(C.PublicRuntimeModules);
			if (C.RuntimeModules != null) PrivateDependencyModuleNames.AddRange(C.RuntimeModules);
			if (Target.bBuildEditor && C.EditorModules != null) PrivateDependencyModuleNames.AddRange(C.EditorModules);
			PublicDefinitions.Add(C.Define + "=1");
		}
		else PublicDefinitions.Add(C.Define + "=0");
		return on;
	}

	/// <summary>UnLua 主版本号（VersionName 首位数字）。</summary>
	private void ApplyUnLuaVersionDefines(bool bHasUnLua, List<string> SearchDirs)
	{
		if (!bHasUnLua) { PublicDefinitions.Add("UNLUA_VERSION_MAJOR=0"); return; }

		int major = 1;
		foreach (string dir in SearchDirs)
		{
			if (major > 1) break;
			try
			{
				foreach (string path in System.IO.Directory.GetFiles(dir, "UnLua.uplugin", System.IO.SearchOption.AllDirectories))
				{
					string json = System.IO.File.ReadAllText(path);
					int vi = json.IndexOf("\"VersionName\"");
					if (vi >= 0)
					{
						int qi = json.IndexOf("\"", json.IndexOf(":", vi) + 1);
						if (qi >= 0)
						{
							int ds = qi + 1, de = ds;
							while (de < json.Length && char.IsDigit(json[de])) de++;
							if (de > ds) int.TryParse(json.Substring(ds, de - ds), out major);
						}
					}
					break;
				}
			}
			catch (System.Exception) { }
		}
		PublicDefinitions.Add("UNLUA_VERSION_MAJOR=" + major);
	}

	private static bool MeetsMinEngineVersion(ReadOnlyTargetRules T, int maj, int min)
	{
		if (maj <= 0) return true;
		if (T.Version.MajorVersion > maj) return true;
		return T.Version.MajorVersion == maj && T.Version.MinorVersion >= min;
	}

	private static string FindProjectRoot(string moduleDir)
	{
		for (int i = 0; i < 10 && moduleDir != null; ++i)
		{
			moduleDir = System.IO.Path.GetDirectoryName(moduleDir);
			if (moduleDir != null && System.IO.Directory.GetFiles(moduleDir, "*.uproject").Length > 0)
				return moduleDir;
		}
		return null;
	}

	private static List<string> CollectPluginSearchDirs(string projectRoot)
	{
		var dirs = new List<string>();
		if (projectRoot != null)
		{
			string pp = System.IO.Path.Combine(projectRoot, "Plugins");
			if (System.IO.Directory.Exists(pp)) dirs.Add(pp);
			string ep = System.IO.Path.GetFullPath(System.IO.Path.Combine(projectRoot, "..", "Engine", "Plugins"));
			if (System.IO.Directory.Exists(ep)) dirs.Add(ep);
		}
		string eng = System.Environment.GetEnvironmentVariable("UE_ENGINE_DIRECTORY");
		if (!string.IsNullOrEmpty(eng))
		{
			string ep = System.IO.Path.Combine(eng, "Plugins");
			if (System.IO.Directory.Exists(ep)) dirs.Add(ep);
		}
		return dirs;
	}

	private static bool DetectAnyEnginePlugin(List<string> dirs, string[] names)
	{
		if (names == null) return false;
		foreach (string name in names)
			foreach (string dir in dirs)
				try
				{
					if (System.IO.Directory.GetFiles(dir, name, System.IO.SearchOption.AllDirectories).Length > 0)
						return true;
				}
				catch (System.Exception) { }
		return false;
	}

	/// <summary>.uproject 显式 Enabled:false（空白容忍）且非 true 时返回 true。</summary>
	private static bool IsPluginExplicitlyDisabledInUproject(string projectRoot, string pluginName)
	{
		try
		{
			foreach (string f in System.IO.Directory.GetFiles(projectRoot, "*.uproject"))
			{
				string c = System.IO.File.ReadAllText(f);
				if (!c.Contains("\"" + pluginName + "\"")) return false;
				bool en = System.Text.RegularExpressions.Regex.IsMatch(c, "\"Enabled\"\\s*:\\s*true");
				return System.Text.RegularExpressions.Regex.IsMatch(c, "\"Enabled\"\\s*:\\s*false") && !en;
			}
		}
		catch (System.Exception) { }
		return false;
	}

	private static void ApplyCustomEngineCompatDefines(ModuleRules Module)
	{
		if (ShouldDefineWithEditorEncryption(Module))
			Module.PublicDefinitions.Add("WITH_EDITOR_ENCRYPTION=0");
	}

	private static bool ShouldDefineWithEditorEncryption(ModuleRules Module)
	{
		foreach (string d in ResolveEngineDirectoryCandidates(Module))
		{
			string h = System.IO.Path.Combine(d, "Source", "Runtime", "Core", "Public", "Misc", "Build.h");
			if (!System.IO.File.Exists(h)) continue;
			try { if (System.IO.File.ReadAllText(h).Contains("WITH_EDITOR_ENCRYPTION")) return true; }
			catch (System.Exception) { }
		}
		return System.Environment.GetEnvironmentVariable("NEXUS_WITH_EDITOR_ENCRYPTION_FALLBACK") == "1";
	}

	private static List<string> ResolveEngineDirectoryCandidates(ModuleRules Module)
	{
		var seen = new HashSet<string>(System.StringComparer.OrdinalIgnoreCase);
		var list = new List<string>();
		try
		{
			var p = typeof(ModuleRules).GetProperty("EngineDirectory",
				System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Static);
			if (p != null)
			{
				bool st = p.GetGetMethod().IsStatic;
				TryAddEngineDirectory(st ? p.GetValue(null) as string : p.GetValue(Module) as string, seen, list);
			}
		}
		catch { }
		TryAddEngineDirectory(System.Environment.GetEnvironmentVariable("UE_ENGINE_DIRECTORY"), seen, list);
		TryAddEngineDirectory(System.Environment.GetEnvironmentVariable("UE4_ROOT"), seen, list);
		TryAddEngineDirectory(System.Environment.GetEnvironmentVariable("UNREAL_ENGINE_PATH"), seen, list);
		return list;
	}

	private static void TryAddEngineDirectory(string dir, HashSet<string> seen, List<string> list)
	{
		if (string.IsNullOrEmpty(dir)) return;
		try
		{
			dir = System.IO.Path.GetFullPath(dir);
			if (System.IO.Directory.Exists(dir) && seen.Add(dir)) list.Add(dir);
		}
		catch (System.Exception) { }
	}
}
