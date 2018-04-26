// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class UnrealFFMPEG : ModuleRules
{

	private string ModulePath
	{
		get { return ModuleDirectory; }
	}

	private string ThirdPartyPath
	{
		get { return Path.GetFullPath(Path.Combine(ModulePath, "../../ThirdParty/")); }
	}

	private string UProjectPath
	{
		get { return Directory.GetParent(ModulePath).Parent.FullName; }
	}


	private void CopyToBinaries(string Filepath, ReadOnlyTargetRules Target)
	{
		string binariesDir = Path.Combine(UProjectPath, "Binaries", Target.Platform.ToString());
		string filename = Path.GetFileName(Filepath);

		System.Console.WriteLine("Writing file " + Filepath + " to " + binariesDir);

		if (!Directory.Exists(binariesDir))
			Directory.CreateDirectory(binariesDir);

		if (!File.Exists(Path.Combine(binariesDir, filename)))
			File.Copy(Filepath, Path.Combine(binariesDir, filename), true);
	}


	public bool LoadFFmpeg(ReadOnlyTargetRules Target)
	{
		bool isLibrarySupported = false;

		if ((Target.Platform == UnrealTargetPlatform.Win64) || (Target.Platform == UnrealTargetPlatform.Win32))
		{
			isLibrarySupported = true;

			string PlatformString = (Target.Platform == UnrealTargetPlatform.Win64) ? "x64" : "Win32";
			string LibrariesPath = Path.Combine(Path.Combine(Path.Combine(ThirdPartyPath, "ffmpeg", "lib"), "vs"), PlatformString);


			System.Console.WriteLine("... LibrariesPath -> " + LibrariesPath);

			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "avcodec.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "avdevice.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "avfilter.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "avformat.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "avutil.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "swresample.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "swscale.lib"));

			string[] dlls = {"avcodec-57.dll","avdevice-57.dll", "avfilter-6.dll", "avformat-57.dll", "avutil-55.dll", "swresample-2.dll", "swscale-4.dll"};

			string BinariesPath = Path.Combine(Path.Combine(Path.Combine(ThirdPartyPath, "ffmpeg", "bin"), "vs"), PlatformString);
			foreach (string dll in dlls)
			{
				PublicDelayLoadDLLs.Add(Path.Combine(BinariesPath, dll));
				CopyToBinaries(Path.Combine(BinariesPath, dll), Target);
			}

		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			isLibrarySupported = true;
			string LibrariesPath = Path.Combine(Path.Combine(ThirdPartyPath, "ffmpeg", "lib"), "osx");

			System.Console.WriteLine("... LibrariesPath -> " + LibrariesPath);

			/*PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "avcodec.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "avdevice.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "avfilter.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "avformat.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "avutil.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "swresample.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "swscale.a"));*/

		}

		if (isLibrarySupported)
		{
			// Include path
			PublicIncludePaths.Add(Path.Combine(ThirdPartyPath, "ffmpeg", "include"));
		}


		return isLibrarySupported;
	}

	public UnrealFFMPEG(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				"UnrealFFMPEG/Public"
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				"UnrealFFMPEG/Private",
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"RenderCore",
				"RHI",
				"ShaderCore",
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

		LoadFFmpeg(Target);
	}
}
