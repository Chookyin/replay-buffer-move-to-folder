# Description

Moves replay buffer recordings into a folder based on the active window.

Use Gemini to modify the code so that it waits for the MKV to be remuxed into MP4, then moves the MP4 and deletes the original MKV file.

Automatic remux to MP4 must be enabled in OBS settings.

**Works only on Windows!**

# Install

Download and run the installer from [releases](https://github.com/Chookyin/replay-buffer-move-to-folder/releases/latest). Windows Defender might complain.

Alternatively download the .zip and move contents into the obs-studio directory.

# Compile

Run `./.github/scripts/Build-Windows.ps1`.

Rename `src/plugin-main_save_mkv.c` to `sre/plugin-main.c` if the MKV file needs to be preserved.

For alternative build methods, please refer to the official Wiki:[obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate/wiki/Quick-Start-Guide#windows)

GitHub Actions should automatically build the project and create a release when a tagged commit is pushed.

Gets `Windows.h` and `Psapi.h` from `C:/Program Files (x86)/Windows Kits/10/Include/<version>/um` (see [CMakeLists.txt](CMakeLists.txt)). I am sure there's a better way, feel free to make a PR, I am not familiar with cmake (or C in general). Does compile without it, but then Visual Studio intellisense does not work.
