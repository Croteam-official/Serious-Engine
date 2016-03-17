# Serious Engine
========================

This is the source code for Serious Engine v.1.10, including the following projects:

* `DedicatedServer`
* `Ecc` The *Entity Class Compiler*, a custom build tool used to compile *.es files
* `Engine` Serious Engine 1.10
* `EngineGUI` Common GUI things for game tools
* `EntitiesMP` All the entity logic
* `GameGUIMP` Common GUI things for game tools
* `GameMP` All the game logic
* `Modeler` Serious Modeler
* `RCon` Used to connect to servers using an admin password
* `SeriousSam` The main game executable
* `SeriousSkaStudio` Serious Ska Studio
* `WorldEditor` Serious Editor
* `DecodeReport` Used to decode crash *.rpt files
* `Depend` Used to build a list of dependency files based on a list of root files
* `LWSkaExporter` Exporter for use in LightWave
* `MakeFONT` Used for generating *.fnt files
* `Shaders` Compiled shaders
* `GameAgent` The serverlist masterserver written in Python

These have been modified to run correctly under the recent version of Windows. (Tested: Win7 x64, Win8 x64, Win8.1 x64)

Building
--------

To build Serious Engine 1, you'll need Visual Studio 2013 or 2015, Professional or Community edition ( https://www.visualstudio.com/post-download-vs?sku=community ).

Try to keep the path to the solution file as short as possible. Do not use spaces in the path.

Once you've installed Visual Studio and (optionally) DirectX8 SDK, you can build the engine solution (`/Sources/All.sln`). Press F7 or Build -> Build solution. The libraries and executables will be put into `\Bin\` directory (or `\Bin\Debug\` if you are using the Debug configuration).

Optional features
-----------------

DirectX support is disabled by default. If you need DirectX support you'll have to download DirectX8 SDK (headers & libraries) ( http://files.seriouszone.com/download.php?fileid=759 or https://www.microsoft.com/en-us/download/details.aspx?id=6812 ) and then enable the SE1_D3D switch for all projects in the solution (Project properties -> Configuration properties -> C/C++ -> Preprocessor -> Preprocessor definitions -> Add "SE1_D3D" for both Debug and Release builds). You will also need to make sure the DirectX8 headers and libraries are located in the following folders (make the folder structure if it's not existing yet):
* `/Tools.Win32/Libraries/DX8SDK/Include/..`
* `/Tools.Win32/Libraries/DX8SDK/Lib/..`

MP3 and OGG playback is disabled by default. If you need these features, you will have to copy amp11lib.dll (for MP3) and/or vorbis.dll with vorbisfile.dll (for OGG) to the '\Bin\' directory (and '\Bin\Debug\' for MP3/OGG support in debug mode)

3D Exploration support is disabled in the open source version of Serious Engine 1 due to copyright issues. In case if you need to create new models you will have to either use editing tools from any of the original games, or write your own code for 3D object import/export.

IFeel support is disabled in the open source version of Serious Engine 1 due to copyright issues. In case if you need IFeel support you will have to copy IFC22.dll and ImmWrapper.dll from the original game into the `\Bin\` folder.

Running
-------

This version of the engine comes with a set of resources (`\SE1_10.GRO`) that allow you to freely use the engine without any additional resources required. However if you want to open or modify levels from Serious Sam Classic: The First Encounter or The Second Encounter (including most user-made levels), you will have to copy the game's resources (.GRO files) into the engine folder. You can buy the original games on Steam, as a part of a bundle with Serious Sam Revolution ( http://store.steampowered.com/app/227780 )

When running a selected project, make sure its project settings on Debugging is set to the right command:
* For debug:
    $(SolutionDir)..\Bin\Debug\$(TargetName).exe`
* For release:
    $(SolutionDir)..\Bin\$(TargetName).exe`
And its working directory:
    $(SolutionDir)..\

Common problems
---------------

If you are getting "fatal error C1083: Cannot open include file: 'Engine/Classes/BaseEvents.h'", or if ECC crashes during compilation of ES files in debug mode, make your solution path shorter. Paths longer than 31 symbols are known to cause problems in debug mode.
Before starting the build process, make sure you have a "Temp" folder in your development directory. If it doesn't exist, create it.
SeriousSkaStudio has some issues with MFC windows that can prevent the main window from being displayed properly.

License
-------

Serious Engine is licensed under the GNU GPL v2 (see LICENSE file).

Some of the code included with the engine sources is not licensed under the GNU GPL v2:

* zlib (located in `Sources/Engine/zlib`) by Jean-loup Gailly and Mark Adler
* LightWave SDK (located in `Sources/LWSkaExporter/SDK`) by NewTek Inc.
