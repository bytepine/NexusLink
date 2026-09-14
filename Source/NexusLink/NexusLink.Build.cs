// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

/// <summary>
/// NexusLink（Runtime 模块）：Server / Dispatcher / Auth / Registry / 元工具 / 34 个 Runtime cap /
/// 运行时 Utils。不链接任何 UnrealEd 系编辑器模块，Development/DebugGame 独立 Game / DedicatedServer
/// 包可直接编入。编辑器专属实现（195 个 EditorOnly cap、编辑器 Utils、Slate 设置定制）在 NexusLinkEditor。
/// </summary>
public class NexusLink : ModuleRules
{
	public NexusLink(ReadOnlyTargetRules Target) : base(Target)
	{
		NexusLinkOptionalPlugins.ApplyCustomEngineCompatDefines(this);

		PublicDependencyModuleNames.AddRange(new[] { "Core", "DeveloperSettings" });
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"CoreUObject", "Engine", "Projects", "Sockets", "Networking", "Json", "JsonUtilities",
			"AssetRegistry", "UMG", "GameplayTags", "AIModule", "GameplayTasks",
			"ImageWrapper", "AnimGraphRuntime", "RenderCore", "PhysicsCore",
			"LevelSequence", "MovieScene", "MovieSceneTracks", "Foliage", "MediaAssets",
			"Slate", "SlateCore", "ApplicationCore", "InputCore",
		});

		// Shipping 不带 MCP 服务器（NexusLinkBuildConfig.h 的 NEXUSLINK_WITH_SERVER=0），
		// 传输层依赖随之不链，避免正式包内残留未用的 HTTP/WS 监听代码
		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateDependencyModuleNames.AddRange(new[]
			{
				"HTTP",
				// UE 5.0+ 将模块 HttpServer 重命名为 HTTPServer
				Target.GetType().GetProperty("Version") != null ? "HTTPServer" : "HttpServer",
				"WebSocketNetworking",
			});
		}

		string ProjectRoot = NexusLinkOptionalPlugins.FindProjectRoot(ModuleDirectory);
		var SearchDirs = NexusLinkOptionalPlugins.CollectPluginSearchDirs(ProjectRoot, this);

		// Runtime 模块只消费 34 个 Runtime cap 用到的三个可选插件（UnLua / GAS / Niagara）的运行时部分；
		// 不链接任何 EditorModules（即便当前 Target 是 NexusEditor，也不允许 ed: 部分进入本模块）。
		// 其余可选插件（StateTree/MVVM/ControlRig/... 等）只被 Editor 域 cap 使用，宏由
		// NexusLinkEditor.Build.cs 定义，本模块不重复 Add（避免 C4005 重定义）。
		bool bHasUnLua = false;
		foreach (var C in NexusLinkOptionalPlugins.BuildFullTable())
		{
			if (C.Define != "WITH_UNLUA" && C.Define != "WITH_GAS" && C.Define != "WITH_NIAGARA")
			{
				continue;
			}
			if (NexusLinkOptionalPlugins.Apply(this, Target, C, SearchDirs, ProjectRoot, bAllowEditorModules: false)
				&& C.Define == "WITH_UNLUA")
				bHasUnLua = true;
		}
		NexusLinkOptionalPlugins.ApplyUnLuaVersionDefines(this, bHasUnLua, SearchDirs);
	}
}

/// <summary>
/// NexusLink（Runtime）与 NexusLinkEditor（Editor）共用的可选插件探测/引擎兼容逻辑。
/// UBT 把同一 Target 编译图里所有模块的 *.Build.cs 编译进同一个 Rules Assembly，
/// 故这个类即便定义在 NexusLink.Build.cs 里，NexusLinkEditor.Build.cs 也能直接引用
/// （无需 using/命名空间），不必额外建一个不会被 UBT 发现的独立 .cs 文件。
/// </summary>
public static class NexusLinkOptionalPlugins
{
	/// <summary>可选插件探测配置：UpluginFiles、MinEngine、模块、Define、EnvVar。</summary>
	public struct OptionalPluginConfig
	{
		public string[] UpluginFiles;
		public string UprojectPluginName; // 非空时检查 .uproject Enabled
		public int MinEngineMajor, MinEngineMinor;
		public string[] PublicRuntimeModules, RuntimeModules, EditorModules;
		public string Define, EnvVar;
		public bool EnvVarSupportsDisable; // 默认 true；UnLua 仅 =1
	}

	/// <summary>全量可选插件表（两模块共用，顺序与旧版一致）。</summary>
	public static OptionalPluginConfig[] BuildFullTable()
	{
		return new[]
		{
			Opt(new[] { "UnLua.uplugin" }, "WITH_UNLUA", "WITH_UNLUA", pub: new[] { "Lua" }, rt: new[] { "UnLua" }, noDisable: true),
			Opt(new[] { "GameplayAbilities.uplugin" }, "WITH_GAS", "WITH_GAS", rt: new[] { "GameplayAbilities" }, uproj: "GameplayAbilities"),
			Opt(new[] { "Niagara.uplugin" }, "WITH_NIAGARA", "WITH_NIAGARA", rt: new[] { "Niagara" }, ed: new[] { "NiagaraEditor" }, uproj: "Niagara"),
			Opt(new[] { "StateTree.uplugin" }, "WITH_STATETREE", "WITH_STATETREE", 5, 5, new[] { "StateTreeModule" }, new[] { "StateTreeEditorModule" }),
			Opt(new[] { "ModelViewViewModel.uplugin" }, "WITH_MVVM", "WITH_MVVM", 5, 5, new[] { "ModelViewViewModel" }, new[] { "ModelViewViewModelBlueprint" }),
			Opt(new[] { "EnhancedInput.uplugin" }, "WITH_ENHANCED_INPUT", "WITH_ENHANCED_INPUT", 5, 0, new[] { "EnhancedInput" }, new[] { "InputEditor" }),
			Opt(new[] { "ControlRig.uplugin" }, "WITH_CONTROL_RIG", "WITH_CONTROL_RIG", 5, 0, new[] { "ControlRig", "RigVM" }, new[] { "ControlRigDeveloper", "RigVMDeveloper" }),
			Opt(new[] { "IKRig.uplugin" }, "WITH_IK_RIG", "WITH_IK_RIG", 5, 0, new[] { "IKRig" }, new[] { "IKRigEditor", "IKRigDeveloper" }),
			Opt(new[] { "Metasound.uplugin" }, "WITH_METASOUND", "WITH_METASOUND", 5, 0, new[] { "MetasoundEngine", "MetasoundFrontend", "MetasoundGraphCore" }, new[] { "MetasoundEditor" }),
			Opt(new[] { "PCG.uplugin" }, "WITH_PCG", "WITH_PCG", 5, 4, new[] { "PCG" }, new[] { "PCGEditor" }),
			Opt(new[] { "PoseSearch.uplugin" }, "WITH_POSE_SEARCH", "WITH_POSE_SEARCH", 5, 4, new[] { "PoseSearch" }, new[] { "PoseSearchEditor" }),
			Opt(new[] { "Paper2D.uplugin" }, "WITH_PAPER2D", "WITH_PAPER2D", rt: new[] { "Paper2D" }),
			// GeometryCollection 双候选 uplugin
			Opt(new[] { "GeometryCollectionPlugin.uplugin", "GeometryCollectionEngine.uplugin" }, "WITH_GEOMETRY_COLLECTION", "WITH_GEOMETRY_COLLECTION", 5, 0, new[] { "GeometryCollectionEngine" }),
			Opt(new[] { "CommonUI.uplugin" }, "WITH_COMMON_UI", "WITH_COMMON_UI", 5, 0, new[] { "CommonUI" }),
			Opt(new[] { "MovieRenderPipeline.uplugin" }, "WITH_MOVIE_RENDER_PIPELINE", "WITH_MOVIE_RENDER_PIPELINE", 5, 0, new[] { "MovieRenderPipelineCore", "MovieRenderPipelineSettings" }, new[] { "MovieRenderPipelineEditor" }),
			// PythonScriptPlugin 的模块 Type=UncookedOnly，只在 Editor 目标链接；define 加 NEXUS_ 前缀避免与引擎自身的 WITH_PYTHON 冲突
			Opt(new[] { "PythonScriptPlugin.uplugin" }, "WITH_NEXUS_PYTHON", "WITH_NEXUS_PYTHON", ed: new[] { "PythonScriptPlugin" }, uproj: "PythonScriptPlugin"),
		};
	}

	/// <summary>表项工厂，省略参数用默认值。</summary>
	public static OptionalPluginConfig Opt(
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

	/// <summary>
	/// 探测并按需应用一条可选插件配置到指定模块。
	/// bAllowEditorModules=false 时即使检测到编辑器目标也不链接 C.EditorModules
	/// （用于 NexusLink Runtime 模块：不应再携带任何 UnrealEd 系依赖)。
	/// </summary>
	public static bool Apply(ModuleRules Module, ReadOnlyTargetRules Target, OptionalPluginConfig C,
		List<string> SearchDirs, string ProjectRoot, bool bAllowEditorModules, bool bDefineMacro = true)
	{
		bool on = MeetsMinEngineVersion(Target, C.MinEngineMajor, C.MinEngineMinor)
			&& DetectAnyEnginePlugin(SearchDirs, C.UpluginFiles);
		if (on && C.UprojectPluginName != null && ProjectRoot != null
			&& IsPluginExplicitlyDisabledInUproject(ProjectRoot, C.UprojectPluginName))
			on = false;
		if (on && C.RuntimeModules != null)
		{
			foreach (string m in C.RuntimeModules)
			{
				if (!ModuleRulesFileExists(SearchDirs, m)) { on = false; break; }
			}
		}

		string ev = System.Environment.GetEnvironmentVariable(C.EnvVar);
		if (ev == "1") on = true;
		if (C.EnvVarSupportsDisable && ev == "0") on = false;

		if (on)
		{
			if (C.PublicRuntimeModules != null) Module.PublicDependencyModuleNames.AddRange(C.PublicRuntimeModules);
			if (C.RuntimeModules != null) Module.PrivateDependencyModuleNames.AddRange(C.RuntimeModules);
			if (bAllowEditorModules && Target.bBuildEditor && C.EditorModules != null)
			{
				foreach (string m in C.EditorModules)
				{
					if (ModuleRulesFileExists(SearchDirs, m))
						Module.PrivateDependencyModuleNames.Add(m);
				}
			}
			if (bDefineMacro) Module.PublicDefinitions.Add(C.Define + "=1");
			foreach (string file in C.UpluginFiles)
				MarkOptionalPluginLinked(Module, System.IO.Path.GetFileNameWithoutExtension(file));
		}
		else if (bDefineMacro) Module.PublicDefinitions.Add(C.Define + "=0");
		return on;
	}

	/// <summary>
	/// 磁盘 .uplugin 保持 Enabled:false，不强制启用宿主插件。
	/// UBT 在 ModuleRules 构造之后才根据 Descriptor.Plugins 建依赖；此处把实际链接的项改成 Enabled:true，消除「未声明插件依赖」警告。
	/// </summary>
	public static void MarkOptionalPluginLinked(ModuleRules Module, string pluginName)
	{
		if (string.IsNullOrEmpty(pluginName)) return;
		try
		{
			var pluginField = typeof(ModuleRules).GetField("Plugin",
				System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Public);
			object pluginInfo = pluginField != null ? pluginField.GetValue(Module) : null;
			if (pluginInfo != null)
			{
				object descriptor = pluginInfo.GetType().GetField("Descriptor") != null
					? pluginInfo.GetType().GetField("Descriptor").GetValue(pluginInfo)
					: pluginInfo.GetType().GetProperty("Descriptor").GetValue(pluginInfo);
				if (descriptor != null)
				{
					var pluginsField = descriptor.GetType().GetField("Plugins");
					object pluginsObj = pluginsField != null ? pluginsField.GetValue(descriptor) : null;
					if (pluginsObj == null)
					{
						pluginsObj = new List<PluginReferenceDescriptor>();
						pluginsField.SetValue(descriptor, pluginsObj);
					}
					var plugins = pluginsObj as System.Collections.IList;
					if (plugins != null)
					{
						bool found = false;
						foreach (object entry in plugins)
						{
							if (entry == null) continue;
							var nameProp = entry.GetType().GetField("Name");
							string n = nameProp != null ? nameProp.GetValue(entry) as string : null;
							if (!string.Equals(n, pluginName, System.StringComparison.OrdinalIgnoreCase)) continue;
							var en = entry.GetType().GetField("bEnabled");
							if (en != null) en.SetValue(entry, true);
							var opt = entry.GetType().GetField("bOptional");
							if (opt != null) opt.SetValue(entry, true);
							found = true;
							break;
						}
						if (!found)
						{
							var added = new PluginReferenceDescriptor(pluginName, null, true);
							added.bOptional = true;
							plugins.Add(added);
						}
					}
				}
			}
		}
		catch { }

		try
		{
			object inner = Module.Target;
			var innerField = Module.Target.GetType().GetField("Inner",
				System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Public);
			if (innerField != null)
			{
				object v = innerField.GetValue(Module.Target);
				if (v != null) inner = v;
			}
			var listField = inner.GetType().GetField("InternalPluginDependencies");
			object listObj = listField != null ? listField.GetValue(inner) : null;
			var list = listObj as System.Collections.IList;
			if (list != null)
			{
				bool has = false;
				foreach (object x in list)
				{
					if (string.Equals(x as string, pluginName, System.StringComparison.OrdinalIgnoreCase)) { has = true; break; }
				}
				if (!has) list.Add(pluginName);
			}
		}
		catch { }
	}

	/// <summary>UnLua 主版本号（VersionName 首位数字）；供两模块各自设置 UNLUA_VERSION_MAJOR。</summary>
	public static void ApplyUnLuaVersionDefines(ModuleRules Module, bool bHasUnLua, List<string> SearchDirs)
	{
		if (!bHasUnLua) { Module.PublicDefinitions.Add("UNLUA_VERSION_MAJOR=0"); return; }

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
		Module.PublicDefinitions.Add("UNLUA_VERSION_MAJOR=" + major);
	}

	public static bool MeetsMinEngineVersion(ReadOnlyTargetRules T, int maj, int min)
	{
		if (maj <= 0) return true;
		if (T.Version.MajorVersion > maj) return true;
		return T.Version.MajorVersion == maj && T.Version.MinorVersion >= min;
	}

	public static string FindProjectRoot(string moduleDir)
	{
		for (int i = 0; i < 10 && moduleDir != null; ++i)
		{
			moduleDir = System.IO.Path.GetDirectoryName(moduleDir);
			if (moduleDir != null && System.IO.Directory.GetFiles(moduleDir, "*.uproject").Length > 0)
				return moduleDir;
		}
		return null;
	}

	public static List<string> CollectPluginSearchDirs(string projectRoot, ModuleRules Module)
	{
		var dirs = new List<string>();
		var seen = new HashSet<string>(System.StringComparer.OrdinalIgnoreCase);
		if (projectRoot != null)
		{
			TryAddSearchDir(System.IO.Path.Combine(projectRoot, "Plugins"), seen, dirs);
			TryAddSearchDir(System.IO.Path.GetFullPath(System.IO.Path.Combine(projectRoot, "..", "Engine", "Plugins")), seen, dirs);
		}
		foreach (string eng in ResolveEngineDirectoryCandidates(Module))
			TryAddSearchDir(System.IO.Path.Combine(eng, "Plugins"), seen, dirs);
		return dirs;
	}

	public static bool ModuleRulesFileExists(List<string> dirs, string moduleName)
	{
		if (string.IsNullOrEmpty(moduleName) || dirs == null) return false;
		string file = moduleName + ".Build.cs";
		foreach (string dir in dirs)
			try
			{
				if (System.IO.Directory.GetFiles(dir, file, System.IO.SearchOption.AllDirectories).Length > 0)
					return true;
			}
			catch (System.Exception) { }
		return false;
	}

	public static void TryAddSearchDir(string dir, HashSet<string> seen, List<string> dirs)
	{
		if (string.IsNullOrEmpty(dir) || !System.IO.Directory.Exists(dir)) return;
		try { dir = System.IO.Path.GetFullPath(dir); } catch (System.Exception) { return; }
		if (seen.Add(dir)) dirs.Add(dir);
	}

	public static bool DetectAnyEnginePlugin(List<string> dirs, string[] names)
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

	public static bool JsonHasEnabledLiteral(string json, string literal)
	{
		int idx = 0;
		while ((idx = json.IndexOf("\"Enabled\"", idx, System.StringComparison.Ordinal)) >= 0)
		{
			int colon = json.IndexOf(':', idx + 9);
			if (colon < 0) break;
			int j = colon + 1;
			while (j < json.Length && char.IsWhiteSpace(json[j])) j++;
			if (j + literal.Length <= json.Length
				&& json.Substring(j, literal.Length) == literal)
			{
				char next = (j + literal.Length < json.Length) ? json[j + literal.Length] : ',';
				if (next == ',' || next == '}' || char.IsWhiteSpace(next))
					return true;
			}
			idx += 9;
		}
		return false;
	}

	/// <summary>.uproject 显式 Enabled:false（空白容忍）且非 true 时返回 true。</summary>
	public static bool IsPluginExplicitlyDisabledInUproject(string projectRoot, string pluginName)
	{
		try
		{
			foreach (string f in System.IO.Directory.GetFiles(projectRoot, "*.uproject"))
			{
				string c = System.IO.File.ReadAllText(f);
				if (!c.Contains("\"" + pluginName + "\"")) return false;
				bool en = JsonHasEnabledLiteral(c, "true");
				return JsonHasEnabledLiteral(c, "false") && !en;
			}
		}
		catch (System.Exception) { }
		return false;
	}

	public static void ApplyCustomEngineCompatDefines(ModuleRules Module)
	{
		if (ShouldDefineWithEditorEncryption(Module))
			Module.PublicDefinitions.Add("WITH_EDITOR_ENCRYPTION=0");
	}

	public static bool ShouldDefineWithEditorEncryption(ModuleRules Module)
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

	public static List<string> ResolveEngineDirectoryCandidates(ModuleRules Module)
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
		try
		{
			// UE4 UBT：静态 UnrealBuildTool.EngineDirectory（自定义引擎 / 无 ModuleRules.EngineDirectory 时也能定位）
			var ubt = typeof(ModuleRules).Assembly.GetType("UnrealBuildTool.UnrealBuildTool");
			var f = ubt != null ? ubt.GetField("EngineDirectory",
				System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Static) : null;
			object v = f != null ? f.GetValue(null) : null;
			if (v != null)
			{
				var fn = v.GetType().GetProperty("FullName");
				TryAddEngineDirectory(fn != null ? fn.GetValue(v) as string : v.ToString(), seen, list);
			}
		}
		catch { }
		TryAddEngineDirectory(System.Environment.GetEnvironmentVariable("UE_ENGINE_DIRECTORY"), seen, list);
		TryAddEngineDirectory(System.Environment.GetEnvironmentVariable("UE4_ROOT"), seen, list);
		TryAddEngineDirectory(System.Environment.GetEnvironmentVariable("UNREAL_ENGINE_PATH"), seen, list);
		return list;
	}

	public static void TryAddEngineDirectory(string dir, HashSet<string> seen, List<string> list)
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
