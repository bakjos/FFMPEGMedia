// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class FFMPEGMedia : ModuleRules
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

			string[] dlls = {"avcodec-58.dll","avdevice-58.dll", "avfilter-7.dll", "avformat-58.dll", "avutil-56.dll", "swresample-3.dll", "swscale-5.dll", "postproc-55.dll"};

			string BinariesPath = Path.Combine(Path.Combine(Path.Combine(ThirdPartyPath, "ffmpeg", "bin"), "vs"), PlatformString);
			foreach (string dll in dlls)
			{
				PublicDelayLoadDLLs.Add(dll);
				//CopyToBinaries(Path.Combine(BinariesPath, dll), Target);
				RuntimeDependencies.Add(Path.Combine(BinariesPath, dll), StagedFileType.NonUFS);
			}

		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			isLibrarySupported = true;
			//string LibrariesPath = Path.Combine(Path.Combine(ThirdPartyPath, "ffmpeg", "lib"), "osx");
            string LibrariesPath = "/usr/local/lib";
			System.Console.WriteLine("... LibrariesPath -> " + LibrariesPath);

            string[] libs = {"libavcodec.58.dylib","libavdevice.58.dylib", "libavfilter.7.dylib", "libavformat.58.dylib", "libavutil.56.dylib", "libswresample.3.dylib", "libswscale.5.dylib", "libpostproc.55.dylib"};
            foreach (string lib in libs)
            {
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, lib));
                //PublicDelayLoadDLLs.Add(Path.Combine(LibrariesPath, lib));
                //CopyToBinaries(Path.Combine(LibrariesPath, lib), Target);
	            //RuntimeDependencies.Add(Path.Combine(LibrariesPath, lib), StagedFileType.NonUFS);
            }

        } else if (Target.Platform == UnrealTargetPlatform.Android) {
          isLibrarySupported = true;
          
          string LibrariesPath =Path.Combine(Path.Combine(ThirdPartyPath, "ffmpeg", "lib"), "android");
          string[] Platforms = { "armeabi-v7a", "arm64-v8a", "x86", "x86_64"  };
          
          
          string[] libs = {"libavcodec.so","libavdevice.so", "libavfilter.so", "libavformat.so", "libavutil.so", "libswresample.so", "libswscale.so"};
          
          System.Console.WriteLine("Architecture: " + Target);
          
          
          foreach (string platform in Platforms)
          {
              foreach (string lib in libs)
              {
                   PublicAdditionalLibraries.Add(Path.Combine(Path.Combine(LibrariesPath, platform), lib ));
              }
          }
                    
          string finalPath =  Path.Combine(ModulePath, "FFMPEGMedia_APL.xml");
          System.Console.WriteLine("... APL Path -> " + finalPath);
          AdditionalPropertiesForReceipt.Add(new ReceiptProperty("AndroidPlugin", finalPath));
        }

		if (isLibrarySupported)
		{
			// Include path
			PublicIncludePaths.Add(Path.Combine(ThirdPartyPath, "ffmpeg", "include"));
		}


		return isLibrarySupported;
	}

	public FFMPEGMedia(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bEnableExceptions = true;
		//OptimizeCode = CodeOptimization.Never;

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"Media",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"MediaUtils",
				"RenderCore",
				"FFMPEGMediaFactory",
				"Projects",
			});
            
        if (Target.Platform == UnrealTargetPlatform.Android)
        {
            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "ApplicationCore",
                    "Launch"
                }
            );
        }

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Media",
			});

		PrivateIncludePaths.AddRange(
			new string[] {
				"FFMPEGMedia/Private",
				"FFMPEGMedia/Private/Player",
				"FFMPEGMedia/Private/FFMPEG",
			});

		LoadFFmpeg(Target);
	}
}
