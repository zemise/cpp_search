# lis-workbench Windows 打包说明

## GitHub Actions 自动打包

每次 push 到 `main` 或 `feat/*` 分支，或手动执行 `workflow_dispatch`，GitHub Actions 会在 `windows-2022` runner 上构建 Win32 主程序并生成 NSIS 安装包。

当前 CI 产物面向 Windows 7 到 Windows 11 的兼容安装包：

| Job | Runner | Toolchain | LabelPrint | 产物 |
|-----|--------|-----------|------------|------|
| `windows-installer` | `windows-2022` | Visual Studio 2022 x64 | `labelprint-v1.2.10-windows-x64-vs2022-win7.zip` | 安装包、更新 zip、manifest |

CI 会从 LabelPrint GitHub Release 下载 `v1.2.10` 的 Win7 兼容包，通过 `build_main.ps1 -LabelPrintPackagePath` 传给 CMake，再用 `packaging/LISWorkbench.nsi` 打包。产物可从 Actions 页面下载 Artifacts。安装包会包含 `lis_workbench.exe` 和自动更新器 `Updater.exe`。出于隐私考虑，HIV 统计表 DOCX 模版不随安装包发布，需要在页面中通过 `上传模版` 放入安装目录 `templates\HIVStatisticsTemplate.docx`。常用电话 RTF 文件同样不随安装包发布，现场可在 `常用电话` 页面上传，或自行放入安装目录 `documents`。

推送与 `src/version.h` 一致的 `v*` 标签时，例如 `v2026.08.27.3`，CI 会自动创建或更新同名 GitHub Release，并上传；普通分支 push 和 PR 只生成 Actions artifact，不发布 Release。

```text
LISWorkbench-Setup-<version>-win7-win11.exe
manifest.json
LISWorkbench-<version>-win7-win11.zip
```

自动更新产物由 `scripts/create_update_package.ps1` 生成：

```text
out/windows/update/updates/manifest.json
out/windows/update/updates/LISWorkbench-<version>-win7-win11.zip
```

Actions 会上传 `LISWorkbench-Updates-<version>-win7-win11` artifact。把其中的 `updates` 目录整体放到共享目录或 HTTP 目录后，系统设置页的更新源可分别指向该目录或其中的 `manifest.json`。设置页默认选择共享文件夹，并只显示当前更新源对应的共享目录或 HTTP 地址。manifest 与 zip 同目录，便于同一份 manifest 兼容共享目录、普通 HTTP 和 GitHub Release。
更新 zip 不包含 HIV 统计表 DOCX 模版和常用电话 RTF 文件；自动更新不会覆盖现场已上传的 `templates\HIVStatisticsTemplate.docx` 或 `documents` 目录。

使用 GitHub Release 作为外网更新源时，系统设置页的 HTTP 地址填写：

```text
https://github.com/zemise/lis-workbench/releases/latest/download/manifest.json
```

系统设置页的 HTTP 地址为空时会默认填入这条 latest 地址，后续发布新版本不需要改客户端配置。

如果启用系统设置页中的 `自动检查更新`，主程序启动后会延迟检查 manifest，每天最多一次；自动检查只提示新版本，用户确认后再下载并安装。也可以通过菜单栏 `系统 -> 检查更新` 手动触发同一套更新流程。

如果现场更新失败，可在安装目录下的 `log\updater.log` 查看更新器日志并回传分析。

说明：GitHub Actions 不能真正提供 Windows 7 runner 做运行验证；这里的 “Win7-Win11” 表示使用 VS2022、静态 MSVC runtime、Win7 兼容宏和 LabelPrint Win7 兼容包构建出的安装包，目标是覆盖 Windows 7 到 Windows 11。

## 本地打包（macOS）

从项目根目录执行：

```bash
scripts/build_windows_package.sh
```

输出：

```text
out/windows/portable/LISWorkbench/result_search.exe
out/windows/installer/LISWorkbench-Setup.exe
```

## 实机开发（Windows）

前提：安装 CMake、Visual Studio 2022/2026。

```powershell
# 一键构建主程序（自动检测 VS 版本，默认优先 VS 2022）
.\scripts\build_main.ps1 -Run

# 全新构建
.\scripts\build_main.ps1 -Clean -Run

# 根目录快捷构建：构建主程序和 Updater，并输出 NSIS 安装包到 out\windows\installer
.\lis.ps1 build
```

## 实机打包（Windows）

推荐使用根目录快捷脚本：

```powershell
.\lis.ps1 rebuild-package -LabelPrintSource github -LabelPrintVersion v1.2.10
.\lis.ps1 rebuild-package -LabelPrintSource local -LabelPrintLocalPath "Z:\Local\Code\020 LabelPrint\LabelPrint"
.\lis.ps1 rebuild-package -LabelPrintSource package -LabelPrintPackagePath "C:\Deps\LabelPrint\labelprint-v1.2.10-windows-x64-vs2022-win7"
```

生成结果：

```text
out\windows\installer\LISWorkbench-Setup.exe
out\windows\update\updates\manifest.json
out\windows\update\updates\LISWorkbench-<version>-win7-win11.zip
```

`lis.ps1 build` 会从 `src\version.h` 自动读取版本号并生成 `out\windows\installer\LISWorkbench-Setup.exe`，但不会生成更新 zip/manifest，也不会在默认 `auto` 策略下下载远程 LabelPrint 包；默认会优先使用本地 LabelPrint 源码目录，并禁用外部 `find_package(LabelPrint)`，避免旧缓存或外部 package 造成 `/MD` 与 `/MT` 运行库不一致。`lis.ps1 package` 和 `lis.ps1 rebuild-package` 会从 `src\version.h` 自动读取版本号；需要覆盖时可传 `-AppVersion vYYYY.MM.DD`。
如果未显式传入 `-Generator`，`lis.ps1 package` 会优先复用现有 `build\main-app\CMakeCache.txt` 中的生成器；没有缓存时再按与 `build_main.ps1` 相同的规则解析实际 Visual Studio 生成器，并据此选择匹配的 LabelPrint release 包，避免 VS2026 构建误用 VS2022/Win7 包。

等价的底层命令如下：

```powershell
# Win32 主程序。默认优先使用 VS 2022，并静态链接 MSVC runtime，便于兼容 Windows 7。
# 正式打包建议通过 lis.ps1 从 GitHub release 下载 LabelPrint 包，或显式指定解压目录。
.\lis.ps1 rebuild-package -LabelPrintSource github -LabelPrintVersion v1.2.10

# NSIS 安装包
New-Item -ItemType Directory -Force out\windows\installer
& "C:\Program Files (x86)\NSIS\makensis.exe" /DAPP_VERSION=v2026.08.27.3 /DAPP_EXE=lis_workbench.exe /DBUILD_DIR=..\build\main-app\Release /DOUTPUT_DIR=..\out\windows\installer /DOUTPUT_NAME=LISWorkbench-Setup.exe packaging\LISWorkbench.nsi
```

如果只面向 Windows 10/11，可以使用 VS 2026 对应的 LabelPrint release 包：

```powershell
.\lis.ps1 rebuild-package -Generator "Visual Studio 18 2026" -LabelPrintSource github -LabelPrintVersion v1.2.10
```

`lis.ps1` 的 `-LabelPrintSource` 可选择 LabelPrint 来源：

- `github`：从 `zemise/LabelPrint` GitHub Release 下载并缓存 release 包，适合正式打包。
- `local`：使用 `-LabelPrintLocalPath` 指向本地源码，适合本机联调。
- `package`：使用 `-LabelPrintPackagePath` 指向已解压的 release 包。
- `auto`：默认策略，`package/rebuild-package` 使用 GitHub；`build/run` 优先使用本地 LabelPrint 源码目录。

`-LabelPrintPackagePath` 会传给 CMake 的 `CMAKE_PREFIX_PATH`，使主项目优先通过 `find_package(LabelPrint 1.2 CONFIG)` 使用 release 包。该路径必须是 release zip 解压后的根目录，且包含 `cmake\LabelPrintConfig.cmake`；路径无效时脚本会直接停止。

需要传入其他 CMake 变量时，可使用 `-CMakeArgs`：

```powershell
.\scripts\build_main.ps1 -Clean -Config Release -CMakeArgs "-DCMAKE_PREFIX_PATH=C:\Deps\LabelPrint\labelprint-v1.2.10-windows-x64-vs2022-win7","-DLIS_LABELPRINT_DIR=C:\src\LabelPrint"
```

`build_main.ps1` 会优先选择已安装的 VS 2022；如果本机没有 VS 2022，会自动退到已安装的 VS 2026。Windows 7 包必须安装 VS 2022 Build Tools 后再构建，也不要把 VS 2026 的 `Microsoft.VC*.CRT` 目录打进安装包。较新的运行库可能依赖 Win8+ 入口，例如 `CreateFile2`、`GetSystemTimePreciseAsFileTime`，在 Win7 上会启动失败。只面向 Windows 10/11 时可以显式使用：

```powershell
.\scripts\build_main.ps1 -Clean -Config Release -Generator "Visual Studio 18 2026"
```

安装脚本会在写入新程序前清理安装目录中旧包残留的 `MSVCP*.dll`、`VCRUNTIME*.dll`、`ucrtbase.dll` 和 `api-ms-win-crt-*.dll`。如果手动复制过运行库，建议先卸载旧版本或删除安装目录后再安装。

## Visual Studio 版本

| VS 版本 | CMake Generator |
|---------|----------------|
| VS 2022 (17.x) | `"Visual Studio 17 2022"` |
| VS 2026 (18.x) | `"Visual Studio 18 2026"` |

## 运行时依赖

- 目标 Windows 需安装 SQL Server ODBC 驱动（ODBC Driver 17/18 for SQL Server 或系统自带）
- Win32 主程序默认 `LIS_STATIC_MSVC_RUNTIME=ON`，VS 原生 Release 包不再需要额外携带 `MSVCP140.dll` / `VCRUNTIME140.dll`。
- 当前 Win32 目标按 Windows 7 兼容编译，CMake 固定 `WINVER/_WIN32_WINNT=0x0601`，并避免直接导入 Win8/Win10 专用入口。若目标机仍提示 `CreateFile2`、`GetSystemTimePreciseAsFileTime` 等入口点缺失，优先检查是否使用了 VS 2026 构建产物，或安装目录中是否混入了较新且不兼容 Win7 的第三方 DLL / MSVC 运行库。
- Qt 版需附带 Qt5Widgets.dll、Qt5Core.dll、Qt5Gui.dll 等（或用 `windeployqt` 自动收集）
- **Qwt**（趋势图渲染）：`qwt.dll` + `Qt5OpenGL.dll` 由 CMake post-build 自动复制到 exe 目录。
- MinGW 静态链接的 Win32 版无需额外 GCC 运行时

## 构建 Qwt

```powershell
# 1. 克隆源码
git clone https://git.code.sf.net/p/qwt/git C:\qwt-src
cd C:\qwt-src && git checkout qwt-6.3.0

# 2. 在 x64 Native Tools 终端编译
qmake qwt.pro
nmake

# 3. CMake 自动检测 C:\qwt-src 下的库和头文件
```
