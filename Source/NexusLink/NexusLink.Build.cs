// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLink : ModuleRules
{
	public NexusLink(ReadOnlyTargetRules Target) : base(Target)
	{
		ApplyCustomEngineCompatDefines(this);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"DeveloperSettings",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Projects",
				"Sockets",
				"Networking",
				"Json",
				"JsonUtilities",
				"HTTP",
				// UE 5.0+ 将模块 HttpServer 重命名为 HTTPServer
				Target.GetType().GetProperty("Version") != null ? "HTTPServer" : "HttpServer",
				"WebSocketNetworking",
				"AssetRegistry",
				"UMG",
				"GameplayTags",
				"AIModule",
				"GameplayTasks",
			"ImageWrapper",
			"AnimGraphRuntime",
			"RenderCore",
			"PhysicsCore",
			}
		);

		// 编辑器专属依赖（设置面板、状态栏、蓝图编辑）
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
				"Settings",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"UnrealEd",
				"LevelEditor",
				"KismetCompiler",
				"Kismet",
				"BlueprintGraph",
				"AnimGraph",
				"UMGEditor",
				"AssetTools",
				"MaterialEditor",
				"PropertyEditor",
				"ContentBrowser",
				"ContentBrowserData",
				"LevelSequence",
				"MovieScene",
				"MovieSceneTracks",
				}
			);

			// LiveCoding 仅 Windows 平台存在（Engine/Source/Developer/Windows/LiveCoding）
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PrivateDependencyModuleNames.Add("LiveCoding");
			}
		}

		// 可选 UnLua 支持：项目或引擎插件目录中存在 UnLua 时自动启用
		bool bHasUnLua = false;

		// 从 ModuleDirectory 向上查找项目根目录（含 .uproject 的目录）
		string SearchDir = ModuleDirectory;
		string ProjectRoot = null;
		for (int i = 0; i < 10 && SearchDir != null; ++i)
		{
			SearchDir = System.IO.Path.GetDirectoryName(SearchDir);
			if (SearchDir != null && System.IO.Directory.GetFiles(SearchDir, "*.uproject").Length > 0)
			{
				ProjectRoot = SearchDir;
				break;
			}
		}

		// 收集所有可能的插件搜索路径
		var PluginSearchDirs = new System.Collections.Generic.List<string>();

		// 1. 项目 Plugins 目录
		if (ProjectRoot != null)
		{
			string ProjectPlugins = System.IO.Path.Combine(ProjectRoot, "Plugins");
			if (System.IO.Directory.Exists(ProjectPlugins))
			{
				PluginSearchDirs.Add(ProjectPlugins);
			}
		}

		// 2. 引擎 Plugins 目录（从项目根的 ../ 或 EngineDirectory 环境推断）
		if (ProjectRoot != null)
		{
			// 源码构建：项目通常与 Engine 同级或位于 Engine 子目录
			string EnginePlugins = System.IO.Path.Combine(ProjectRoot, "..", "Engine", "Plugins");
			EnginePlugins = System.IO.Path.GetFullPath(EnginePlugins);
			if (System.IO.Directory.Exists(EnginePlugins))
			{
				PluginSearchDirs.Add(EnginePlugins);
			}
		}

		// 3. Launcher 安装引擎：UE_ENGINE_DIRECTORY 环境变量
		string EngineEnvDir = System.Environment.GetEnvironmentVariable("UE_ENGINE_DIRECTORY");
		if (!string.IsNullOrEmpty(EngineEnvDir))
		{
			string EnvPlugins = System.IO.Path.Combine(EngineEnvDir, "Plugins");
			if (System.IO.Directory.Exists(EnvPlugins))
			{
				PluginSearchDirs.Add(EnvPlugins);
			}
		}

		foreach (string Dir in PluginSearchDirs)
		{
			if (bHasUnLua) break;
			try
			{
				foreach (string File in System.IO.Directory.GetFiles(
					Dir, "UnLua.uplugin", System.IO.SearchOption.AllDirectories))
				{
					bHasUnLua = true;
					break;
				}
			}
			catch (System.Exception)
			{
				// 权限不足等情况静默跳过
			}
		}

		// 环境变量强制开启
		string EnvUnLua = System.Environment.GetEnvironmentVariable("WITH_UNLUA");
		if (EnvUnLua == "1") bHasUnLua = true;

		// ── 可选 GameplayAbilities 支持 ─────────────────────────────────────────
		// 仅当项目 .uproject 中已启用 GameplayAbilities 插件时才链接，避免污染无 GAS 项目。
		bool bHasGas = false;
		foreach (string Dir in PluginSearchDirs)
		{
			if (bHasGas) break;
			try
			{
				foreach (string File in System.IO.Directory.GetFiles(
					Dir, "GameplayAbilities.uplugin", System.IO.SearchOption.AllDirectories))
				{
					bHasGas = true;
					break;
				}
			}
			catch (System.Exception)
			{
				// 权限不足等情况静默跳过
			}
		}

		// 检查 .uproject 是否明确禁用 GameplayAbilities。
		// 规则：
		//   - 找到 .uproject 且含 "GameplayAbilities" + "Enabled": false → 强制关闭
		//   - 找到 .uproject 但未提及 GameplayAbilities（用户尚未配置）→ 保持探测结果
		//   - 没找到 .uproject（BuildPlugin 独立构建模式）→ 保持探测结果（引擎自带插件即可）
		if (bHasGas && ProjectRoot != null)
		{
			try
			{
				foreach (string UprojectFile in System.IO.Directory.GetFiles(ProjectRoot, "*.uproject"))
				{
					string Content = System.IO.File.ReadAllText(UprojectFile);
					bool Mentioned = Content.Contains("\"GameplayAbilities\"");
					if (Mentioned)
					{
						// 只有在 .uproject 中显式写了 "Enabled": false 才关闭
						bool ExplicitlyEnabled  = Content.Contains("\"Enabled\": true");
						bool ExplicitlyDisabled = Content.Contains("\"Enabled\": false") && !ExplicitlyEnabled;
						if (ExplicitlyDisabled)
							bHasGas = false;
						// 否则：显式 true 或未写 Enabled 均保持 bHasGas = true
					}
					// .uproject 完全未提及 GameplayAbilities → 保持探测结果不变
					break;
				}
			}
			catch (System.Exception) { }
		}

		// 环境变量可强制开启或关闭（0=关闭，1=开启）
		string EnvGas = System.Environment.GetEnvironmentVariable("WITH_GAS");
		if (EnvGas == "1") bHasGas = true;
		if (EnvGas == "0") bHasGas = false;

		if (bHasGas)
		{
			PrivateDependencyModuleNames.Add("GameplayAbilities");
			PublicDefinitions.Add("WITH_GAS=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_GAS=0");
		}

		// ── 可选 Niagara 支持（引擎 Niagara 插件存在时链接）────────────────────
		bool bHasNiagara = false;
		foreach (string Dir in PluginSearchDirs)
		{
			if (bHasNiagara) break;
			try
			{
				foreach (string File in System.IO.Directory.GetFiles(
					Dir, "Niagara.uplugin", System.IO.SearchOption.AllDirectories))
				{
					bHasNiagara = true;
					break;
				}
			}
			catch (System.Exception) { }
		}

		if (bHasNiagara && ProjectRoot != null)
		{
			try
			{
				foreach (string UprojectFile in System.IO.Directory.GetFiles(ProjectRoot, "*.uproject"))
				{
					string Content = System.IO.File.ReadAllText(UprojectFile);
					if (Content.Contains("\"Niagara\""))
					{
						bool ExplicitlyEnabled  = Content.Contains("\"Enabled\": true");
						bool ExplicitlyDisabled = Content.Contains("\"Enabled\": false") && !ExplicitlyEnabled;
						if (ExplicitlyDisabled)
							bHasNiagara = false;
					}
					break;
				}
			}
			catch (System.Exception) { }
		}

		string EnvNiagara = System.Environment.GetEnvironmentVariable("WITH_NIAGARA");
		if (EnvNiagara == "1") bHasNiagara = true;
		if (EnvNiagara == "0") bHasNiagara = false;

		if (bHasNiagara)
		{
			PrivateDependencyModuleNames.Add("Niagara");
			PublicDefinitions.Add("WITH_NIAGARA=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_NIAGARA=0");
		}

		// ── 可选 StateTree 支持（UE 5.5+ 且引擎 StateTree 插件存在时链接）──────────
		// UE 5.5 以下 StateTree API 不稳定（Experimental）；5.5 起接口固化、可用于检查资产。
		bool bHasStateTree = false;
		bool bEngineIsUE55Plus = (Target.Version.MajorVersion > 5)
			|| (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 5);

		if (bEngineIsUE55Plus)
		{
			foreach (string Dir in PluginSearchDirs)
			{
				if (bHasStateTree) break;
				try
				{
					foreach (string File in System.IO.Directory.GetFiles(
						Dir, "StateTree.uplugin", System.IO.SearchOption.AllDirectories))
					{
						bHasStateTree = true;
						break;
					}
				}
				catch (System.Exception) { }
			}
		}

		string EnvStateTree = System.Environment.GetEnvironmentVariable("WITH_STATETREE");
		if (EnvStateTree == "1") bHasStateTree = true;
		if (EnvStateTree == "0") bHasStateTree = false;

		if (bHasStateTree)
		{
			PrivateDependencyModuleNames.Add("StateTreeModule");
			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.Add("StateTreeEditorModule");
			}
			PublicDefinitions.Add("WITH_STATETREE=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_STATETREE=0");
		}

		// ── 可选 UMG MVVM 支持（UE 5.5+ 且引擎 ModelViewViewModel 插件存在时链接）──
		// MVVM 自 UE 5.1 引入，但 5.5 起 API 稳定可靠；编辑器侧数据在 Blueprint 扩展中。
		bool bHasMvvm = false;

		if (bEngineIsUE55Plus)
		{
			foreach (string Dir in PluginSearchDirs)
			{
				if (bHasMvvm) break;
				try
				{
					foreach (string File in System.IO.Directory.GetFiles(
						Dir, "ModelViewViewModel.uplugin", System.IO.SearchOption.AllDirectories))
					{
						bHasMvvm = true;
						break;
					}
				}
				catch (System.Exception) { }
			}
		}

		string EnvMvvm = System.Environment.GetEnvironmentVariable("WITH_MVVM");
		if (EnvMvvm == "1") bHasMvvm = true;
		if (EnvMvvm == "0") bHasMvvm = false;

		if (bHasMvvm)
		{
			PrivateDependencyModuleNames.Add("ModelViewViewModel");
			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.Add("ModelViewViewModelBlueprint");
			}
			PublicDefinitions.Add("WITH_MVVM=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_MVVM=0");
		}

		// ── 可选 EnhancedInput 支持（UE 5.0+；4.26 插件 API 不稳定，跳过）────────────
		bool bHasEnhancedInput = false;
		bool bEngineIsUE50Plus = Target.Version.MajorVersion >= 5;

		if (bEngineIsUE50Plus)
		{
			foreach (string Dir in PluginSearchDirs)
			{
				if (bHasEnhancedInput) break;
				try
				{
					foreach (string File in System.IO.Directory.GetFiles(
						Dir, "EnhancedInput.uplugin", System.IO.SearchOption.AllDirectories))
					{
						bHasEnhancedInput = true;
						break;
					}
				}
				catch (System.Exception) { }
			}
		}

		string EnvEnhancedInput = System.Environment.GetEnvironmentVariable("WITH_ENHANCED_INPUT");
		if (EnvEnhancedInput == "1") bHasEnhancedInput = true;
		if (EnvEnhancedInput == "0") bHasEnhancedInput = false;

		if (bHasEnhancedInput)
		{
			PrivateDependencyModuleNames.Add("EnhancedInput");
			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.Add("EnhancedInputEditor");
			}
			PublicDefinitions.Add("WITH_ENHANCED_INPUT=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_ENHANCED_INPUT=0");
		}

		// ── 可选 ControlRig 支持（UE 5.0+；4.26 为 Experimental，API 不稳定，跳过）────────
		bool bHasControlRig = false;

		if (bEngineIsUE50Plus)
		{
			foreach (string Dir in PluginSearchDirs)
			{
				if (bHasControlRig) break;
				try
				{
					foreach (string File in System.IO.Directory.GetFiles(
						Dir, "ControlRig.uplugin", System.IO.SearchOption.AllDirectories))
					{
						bHasControlRig = true;
						break;
					}
				}
				catch (System.Exception) { }
			}
		}

		string EnvControlRig = System.Environment.GetEnvironmentVariable("WITH_CONTROL_RIG");
		if (EnvControlRig == "1") bHasControlRig = true;
		if (EnvControlRig == "0") bHasControlRig = false;

		if (bHasControlRig)
		{
			PrivateDependencyModuleNames.Add("ControlRig");
			PrivateDependencyModuleNames.Add("RigVM");
			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.Add("ControlRigDeveloper");
			}
			PublicDefinitions.Add("WITH_CONTROL_RIG=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_CONTROL_RIG=0");
		}

		// ── 可选 IKRig 支持（UE 5.0+）──────────────────────────────────────────────────
		bool bHasIKRig = false;

		if (bEngineIsUE50Plus)
		{
			foreach (string Dir in PluginSearchDirs)
			{
				if (bHasIKRig) break;
				try
				{
					foreach (string File in System.IO.Directory.GetFiles(
						Dir, "IKRig.uplugin", System.IO.SearchOption.AllDirectories))
					{
						bHasIKRig = true;
						break;
					}
				}
				catch (System.Exception) { }
			}
		}

		string EnvIKRig = System.Environment.GetEnvironmentVariable("WITH_IK_RIG");
		if (EnvIKRig == "1") bHasIKRig = true;
		if (EnvIKRig == "0") bHasIKRig = false;

		if (bHasIKRig)
		{
			PrivateDependencyModuleNames.Add("IKRig");
			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.Add("IKRigEditor");
				PrivateDependencyModuleNames.Add("IKRigDeveloper");
			}
			PublicDefinitions.Add("WITH_IK_RIG=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_IK_RIG=0");
		}

		// ── 可选 MetaSound 支持（UE 5.0+）──────────────────────────────────────────────
		bool bHasMetaSound = false;

		if (bEngineIsUE50Plus)
		{
			foreach (string Dir in PluginSearchDirs)
			{
				if (bHasMetaSound) break;
				try
				{
					foreach (string File in System.IO.Directory.GetFiles(
						Dir, "Metasound.uplugin", System.IO.SearchOption.AllDirectories))
					{
						bHasMetaSound = true;
						break;
					}
				}
				catch (System.Exception) { }
			}
		}

		string EnvMetaSound = System.Environment.GetEnvironmentVariable("WITH_METASOUND");
		if (EnvMetaSound == "1") bHasMetaSound = true;
		if (EnvMetaSound == "0") bHasMetaSound = false;

		if (bHasMetaSound)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "MetasoundEngine", "MetasoundFrontend", "MetasoundGraphCore" });
			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.Add("MetasoundEditor");
			}
			PublicDefinitions.Add("WITH_METASOUND=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_METASOUND=0");
		}

		// ── 可选 PCG 支持（UE 5.4+）────────────────────────────────────────────────────
		bool bEngineIsUE54Plus = (Target.Version.MajorVersion > 5)
			|| (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 4);
		bool bHasPCG = false;

		if (bEngineIsUE54Plus)
		{
			foreach (string Dir in PluginSearchDirs)
			{
				if (bHasPCG) break;
				try
				{
					foreach (string File in System.IO.Directory.GetFiles(
						Dir, "PCG.uplugin", System.IO.SearchOption.AllDirectories))
					{
						bHasPCG = true;
						break;
					}
				}
				catch (System.Exception) { }
			}
		}

		string EnvPCG = System.Environment.GetEnvironmentVariable("WITH_PCG");
		if (EnvPCG == "1") bHasPCG = true;
		if (EnvPCG == "0") bHasPCG = false;

		if (bHasPCG)
		{
			PrivateDependencyModuleNames.Add("PCG");
			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.Add("PCGEditor");
			}
			PublicDefinitions.Add("WITH_PCG=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_PCG=0");
		}

		// ── 可选 PoseSearch 支持（UE 5.4+）─────────────────────────────────────────────
		bool bHasPoseSearch = false;

		if (bEngineIsUE54Plus)
		{
			foreach (string Dir in PluginSearchDirs)
			{
				if (bHasPoseSearch) break;
				try
				{
					foreach (string File in System.IO.Directory.GetFiles(
						Dir, "PoseSearch.uplugin", System.IO.SearchOption.AllDirectories))
					{
						bHasPoseSearch = true;
						break;
					}
				}
				catch (System.Exception) { }
			}
		}

		string EnvPoseSearch = System.Environment.GetEnvironmentVariable("WITH_POSE_SEARCH");
		if (EnvPoseSearch == "1") bHasPoseSearch = true;
		if (EnvPoseSearch == "0") bHasPoseSearch = false;

		if (bHasPoseSearch)
		{
			PrivateDependencyModuleNames.Add("PoseSearch");
			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.Add("PoseSearchEditor");
			}
			PublicDefinitions.Add("WITH_POSE_SEARCH=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_POSE_SEARCH=0");
		}

		if (bHasUnLua)
		{
			PublicDependencyModuleNames.Add("Lua");
			PrivateDependencyModuleNames.Add("UnLua");
			PublicDefinitions.Add("WITH_UNLUA=1");

			// 读取 UnLua.uplugin 的 VersionName 判断主版本号
			int UnLuaVersionMajor = 1;
			foreach (string Dir in PluginSearchDirs)
			{
				if (UnLuaVersionMajor > 1) break;
				try
				{
					foreach (string UpluginPath in System.IO.Directory.GetFiles(
						Dir, "UnLua.uplugin", System.IO.SearchOption.AllDirectories))
					{
						string Json = System.IO.File.ReadAllText(UpluginPath);
						int vi = Json.IndexOf("\"VersionName\"");
						if (vi >= 0)
						{
							int qi = Json.IndexOf("\"", Json.IndexOf(":", vi) + 1);
							if (qi >= 0)
							{
								int ds = qi + 1, de = ds;
								while (de < Json.Length && char.IsDigit(Json[de])) de++;
								if (de > ds) int.TryParse(Json.Substring(ds, de - ds), out UnLuaVersionMajor);
							}
						}
						break;
					}
				}
				catch (System.Exception) { }
			}
			PublicDefinitions.Add("UNLUA_VERSION_MAJOR=" + UnLuaVersionMajor);
		}
		else
		{
			PublicDefinitions.Add("WITH_UNLUA=0");
			PublicDefinitions.Add("UNLUA_VERSION_MAJOR=0");
		}
	}

	/// <summary>
	/// 定制引擎（如 LetsGo）在 Build.h 用 #error 要求 UBT 预定义 WITH_EDITOR_ENCRYPTION；
	/// Rider 直编时 EngineDirectory 可能为空，需多路径探测 + 环境变量兜底。
	/// </summary>
	private static void ApplyCustomEngineCompatDefines(ModuleRules Module)
	{
		if (ShouldDefineWithEditorEncryption(Module))
		{
			Module.PublicDefinitions.Add("WITH_EDITOR_ENCRYPTION=0");
		}
	}

	private static bool ShouldDefineWithEditorEncryption(ModuleRules Module)
	{
		foreach (string engineDir in ResolveEngineDirectoryCandidates(Module))
		{
			string buildH = System.IO.Path.Combine(engineDir, "Source", "Runtime", "Core", "Public", "Misc", "Build.h");
			if (!System.IO.File.Exists(buildH)) continue;

			try
			{
				string content = System.IO.File.ReadAllText(buildH);
				// 含 #ifndef 或 #error「UBT should always define…」等形态
				if (content.Contains("WITH_EDITOR_ENCRYPTION"))
				{
					return true;
				}
			}
			catch (System.Exception)
			{
				// 权限/IO 异常时尝试下一候选路径
			}
		}

		// Rider 直编：未走 UBT 注入且读不到 Build.h 时，由环境变量强制（LetsGo 等定制引擎）
		string force = System.Environment.GetEnvironmentVariable("NEXUS_WITH_EDITOR_ENCRYPTION_FALLBACK");
		return force == "1";
	}

	private static System.Collections.Generic.List<string> ResolveEngineDirectoryCandidates(ModuleRules Module)
	{
		var seen = new System.Collections.Generic.HashSet<string>(System.StringComparer.OrdinalIgnoreCase);
		var list = new System.Collections.Generic.List<string>();

		// EngineDirectory 在 UE 5.8 改为 static 属性，用反射兼容 instance（UE4~5.7）和 static（UE5.8+）
		try
		{
			var Prop = typeof(ModuleRules).GetProperty("EngineDirectory",
				System.Reflection.BindingFlags.Public |
				System.Reflection.BindingFlags.Instance |
				System.Reflection.BindingFlags.Static);
			if (Prop != null)
			{
				bool IsStatic = Prop.GetGetMethod().IsStatic;
				string EngDir = IsStatic
					? Prop.GetValue(null) as string
					: Prop.GetValue(Module) as string;
				TryAddEngineDirectory(EngDir, seen, list);
			}
		}
		catch { /* 属性不存在或访问失败时继续 */ }

		TryAddEngineDirectory(System.Environment.GetEnvironmentVariable("UE_ENGINE_DIRECTORY"), seen, list);
		TryAddEngineDirectory(System.Environment.GetEnvironmentVariable("UE4_ROOT"), seen, list);
		TryAddEngineDirectory(System.Environment.GetEnvironmentVariable("UNREAL_ENGINE_PATH"), seen, list);

		return list;
	}

	private static void TryAddEngineDirectory(
		string dir,
		System.Collections.Generic.HashSet<string> seen,
		System.Collections.Generic.List<string> list)
	{
		if (string.IsNullOrEmpty(dir)) return;

		try
		{
			dir = System.IO.Path.GetFullPath(dir);
			if (System.IO.Directory.Exists(dir) && seen.Add(dir))
			{
				list.Add(dir);
			}
		}
		catch (System.Exception)
		{
			// 权限/IO 异常时静默跳过
		}
	}
}
