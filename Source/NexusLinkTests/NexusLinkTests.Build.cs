// Copyright byteyang. All Rights Reserved.

using UnrealBuildTool;

public class NexusLinkTests : ModuleRules
{
	public NexusLinkTests(ReadOnlyTargetRules Target) : base(Target)
	{
		ApplyCustomEngineCompatDefines(this);

		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"NexusLink",
				"Json",
				"JsonUtilities",
				"Projects",
			}
		);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
					"BlueprintGraph",
					"KismetCompiler",
					"UMG",
					"UMGEditor",
				}
			);
		}

		PrivateIncludePaths.AddRange(
			new string[]
			{
				"NexusLink/Private",
			}
		);
	}

	/// <summary>
	/// 与 NexusLink.Build.cs 同步：定制引擎 Build.h 要求 WITH_EDITOR_ENCRYPTION 时兜底为 0。
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
				if (System.IO.File.ReadAllText(buildH).Contains("WITH_EDITOR_ENCRYPTION"))
				{
					return true;
				}
			}
			catch (System.Exception) { }
		}

		return System.Environment.GetEnvironmentVariable("NEXUS_WITH_EDITOR_ENCRYPTION_FALLBACK") == "1";
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
		}
	}
}
