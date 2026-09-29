# Windows 7 Qt runtime provenance

- Scope: Qt 5.15.2 MSVC2019 x64 runtime DLLs and plugins copied into the `win7` installer subdirectory by `windeployqt`. The Qt SDK itself stays outside version control under `Qt/win7-compat/5.15.2/msvc2019_64`.
- Source: official Qt 5.15.2 Windows MSVC2019 x64 archives, including Qt WebSockets, downloaded from `download.qt.io` with `aqtinstall`.
- Use: Windows 7 SP1, Windows 8, and Windows 8.1 client executables. Windows 10/11 continue to use Qt 6.8.3.
- License: Qt open-source LGPLv3 dynamic-link option; full text in `LICENSE.LGPLv3`, obtained from the official Qt 5.15.2 `qtbase` source repository.
- Maintenance: Qt 5.15.2 is an older compatibility runtime. New fixes should be applied to both client branches where applicable.
