FFMPEG Media Plugin for unreal engine 
===================================

A plugin that let's you use the FFMPEG library as media player


## Features:
   - Works on Windows and Mac
   - Support for hardware accelerated codecs
   - Support for videos with alpha   

## How to use

1. The plugin was tested on Unreal 4.19.2
2. You can clone the repository to `<Game>/Plugins/` or if you want you can use git submodules to your own git repository. Alternatively, you can copy to the `Engine/Plugins/` if you wish to make the plugin available to all of your projects.
   > Do not forget to run UE4's `Generate Project Files` to account for these changes!
3. Follow the steps to [play a video tutorial](https://docs.unrealengine.com/en-us/Engine/MediaFramework/HowTo/FileMediaSource) but insead of using the automatic player, choose the FFMPEGPlayer inside the players overrides for the step 5.


![Player Overrrides](https://github.com/bakjos/FFMPEGMedia/raw/master/docs/mediatype.png)
