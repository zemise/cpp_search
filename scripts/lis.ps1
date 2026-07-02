# Unified LIS Workbench build helper.
# Usage:
#   .\lis.ps1 build
#   .\lis.ps1 clean
#   .\lis.ps1 run
#   .\lis.ps1 package
#   .\lis.ps1 rebuild-package

param(
    [Parameter(Position = 0)]
    [ValidateSet("build", "clean", "run", "package", "rebuild-package", "help")]
    [string]$Command = "build",

    [string]$Config = "Release",
    [string]$Generator = "",
    [ValidateSet("auto", "github", "local", "package")]
    [string]$LabelPrintSource = "auto",
    [string]$LabelPrintVersion = "v1.2.10",
    [string]$LabelPrintLocalPath = "",
    [string]$LabelPrintPackagePath = "",
    [string]$AppVersion = "",
    [string]$OutputName = "LISWorkbench-Setup.exe",
    [string]$NsisPath = "",
    [string[]]$CMakeArgs = @()
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $Root "build\main-app"
$InstallerDir = Join-Path $Root "out\windows\installer"
$LabelPrintDepsDir = Join-Path $Root "build\deps\LabelPrint"
$EffectiveGenerator = ""

function Show-Help {
    Write-Host "LIS Workbench build helper"
    Write-Host ""
    Write-Host "Commands:"
    Write-Host "  build            Build lis_workbench.exe, Updater.exe, and NSIS installer"
    Write-Host "  clean            Remove build/main-app"
    Write-Host "  run              Build and run lis_workbench.exe"
    Write-Host "  package          Build Release, create NSIS installer, and create update package"
    Write-Host "  rebuild-package  Clean, build Release, create NSIS installer, and create update package"
    Write-Host ""
    Write-Host "Options:"
    Write-Host "  -LabelPrintSource <auto|github|local|package>"
    Write-Host "                                auto: package uses GitHub, build/run prefer local LabelPrint source"
    Write-Host "  -LabelPrintVersion <version>  GitHub LabelPrint release version"
    Write-Host "  -LabelPrintLocalPath <path>   Local LabelPrint source root"
    Write-Host "  -LabelPrintPackagePath <path> Extracted LabelPrint release root"
    Write-Host "  -Generator <name>              CMake generator"
    Write-Host "  -Config <Release|Debug>        Build config"
    Write-Host "  -AppVersion <version>          Installer version"
    Write-Host "  -OutputName <name>             Installer output exe name"
}

function Resolve-AppVersion {
    if ($AppVersion) {
        return $AppVersion
    }
    $line = Select-String -Path (Join-Path $Root "src\version.h") -Pattern 'kVersion\s*=' | Select-Object -First 1
    if (-not $line) {
        throw "Cannot read version from src\version.h"
    }
    $value = [regex]::Match($line.Line, '"([^"]+)"').Groups[1].Value
    if (-not $value) {
        throw "Invalid version line: $($line.Line)"
    }
    return $value
}

function Resolve-DefaultLabelPrintSource([string]$CommandName) {
    if ($LabelPrintSource -ne "auto") {
        return $LabelPrintSource
    }
    if ($LabelPrintPackagePath) {
        return "package"
    }
    if ($CommandName -eq "package" -or $CommandName -eq "rebuild-package") {
        return "github"
    }
    if ($LabelPrintLocalPath) {
        return "local"
    }
    $defaultLocalPath = Resolve-DefaultLabelPrintLocalPath
    if (Test-Path $defaultLocalPath) {
        return "local"
    }
    return ""
}

function Resolve-DefaultLabelPrintLocalPath {
    if ($LabelPrintLocalPath) {
        return $LabelPrintLocalPath
    }
    return Join-Path $Root "..\..\020 LabelPrint\LabelPrint"
}

function Get-CmakeVisualStudioGenerators {
    $items = @()
    $lines = cmake -G --help 2>&1 | Select-String "Visual Studio \d+ 20\d+"
    foreach ($line in $lines) {
        $name = $line.ToString().TrimStart("* ").Split("=")[0].Trim()
        if ($name) { $items += $name }
    }
    return $items
}

function Get-VsGeneratorFromMajor([int]$Major) {
    switch ($Major) {
        17 { return "Visual Studio 17 2022" }
        18 { return "Visual Studio 18 2026" }
        default { return "" }
    }
}

function Get-InstalledVisualStudioGenerators {
    $items = @()
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $versions = & $vswhere -products * -requires Microsoft.Component.MSBuild -property installationVersion 2>$null
        foreach ($version in $versions) {
            if ($version -match "^(\d+)\.") {
                $name = Get-VsGeneratorFromMajor ([int]$Matches[1])
                if ($name -and -not ($items -contains $name)) { $items += $name }
            }
        }
    }
    return $items
}

function Resolve-EffectiveGenerator {
    if ($script:EffectiveGenerator) {
        return $script:EffectiveGenerator
    }

    if ($Generator) {
        $script:EffectiveGenerator = $Generator
        return $script:EffectiveGenerator
    }

    $cachePath = Join-Path $BuildDir "CMakeCache.txt"
    if (Test-Path $cachePath) {
        $cachedGenerator = Select-String -Path $cachePath -Pattern '^CMAKE_GENERATOR:INTERNAL=' |
            Select-Object -First 1
        if ($cachedGenerator -and ($cachedGenerator.Line -match '=(.+)$')) {
            $script:EffectiveGenerator = $Matches[1]
            return $script:EffectiveGenerator
        }
    }

    $cmakeGenerators = Get-CmakeVisualStudioGenerators
    $installedGenerators = Get-InstalledVisualStudioGenerators
    $preferred = @("Visual Studio 17 2022", "Visual Studio 18 2026")
    foreach ($candidate in $preferred) {
        if (($installedGenerators -contains $candidate) -and ($cmakeGenerators -contains $candidate)) {
            $script:EffectiveGenerator = $candidate
            return $script:EffectiveGenerator
        }
    }
    if ($installedGenerators.Count -gt 0) {
        foreach ($candidate in $installedGenerators) {
            if ($cmakeGenerators -contains $candidate) {
                $script:EffectiveGenerator = $candidate
                return $script:EffectiveGenerator
            }
        }
    }
    if ($cmakeGenerators.Count -gt 0) {
        $script:EffectiveGenerator = $cmakeGenerators[0]
        return $script:EffectiveGenerator
    }

    $script:EffectiveGenerator = "Visual Studio 17 2022"
    return $script:EffectiveGenerator
}

function Resolve-LabelPrintAssetName {
    # Always use the static-MSVCRT / Win7-compatible package.
    # VS2022-static and VS2026 share stable MSVC ABI for C linkage.
    return "labelprint-$LabelPrintVersion-windows-x64-vs2022-win7.zip"
}

function Resolve-ExtractedLabelPrintPackageRoot([string]$ExtractRoot) {
    $config = Get-ChildItem -Path $ExtractRoot -Recurse -Filter LabelPrintConfig.cmake -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $config) {
        return ""
    }
    return Split-Path (Split-Path $config.FullName -Parent) -Parent
}

function Resolve-GitHubLabelPrintPackage {
    $asset = Resolve-LabelPrintAssetName
    $versionDir = Join-Path $LabelPrintDepsDir $LabelPrintVersion
    $extractRoot = Join-Path $versionDir ([System.IO.Path]::GetFileNameWithoutExtension($asset))
    $packageRoot = Resolve-ExtractedLabelPrintPackageRoot $extractRoot
    if ($packageRoot) {
        Write-Host "LabelPrint GitHub package: $packageRoot"
        return $packageRoot
    }

    New-Item -ItemType Directory -Force $versionDir | Out-Null
    $zipPath = Join-Path $versionDir $asset
    if (-not (Test-Path $zipPath)) {
        $url = "https://github.com/zemise/LabelPrint/releases/download/$LabelPrintVersion/$asset"
        Write-Host "Downloading LabelPrint: $url"
        Invoke-WebRequest -Uri $url -OutFile $zipPath
    } else {
        Write-Host "Using cached LabelPrint zip: $zipPath"
    }

    Remove-Item -Recurse -Force $extractRoot -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $extractRoot | Out-Null
    Expand-Archive -Path $zipPath -DestinationPath $extractRoot -Force

    $packageRoot = Resolve-ExtractedLabelPrintPackageRoot $extractRoot
    if (-not $packageRoot) {
        throw "LabelPrintConfig.cmake not found in $zipPath"
    }
    Write-Host "LabelPrint GitHub package: $packageRoot"
    return $packageRoot
}

function Resolve-LabelPrintBuildOptions([string]$CommandName) {
    $source = Resolve-DefaultLabelPrintSource $CommandName
    $result = @{
        Source = $source
        PackagePath = ""
        ExtraCMakeArgs = @()
    }

    switch ($source) {
        "github" {
            $result.PackagePath = Resolve-GitHubLabelPrintPackage
        }
        "package" {
            if (-not $LabelPrintPackagePath) {
                throw "-LabelPrintSource package requires -LabelPrintPackagePath."
            }
            $result.PackagePath = $LabelPrintPackagePath
        }
        "local" {
            $localPath = Resolve-DefaultLabelPrintLocalPath
            if (-not (Test-Path $localPath)) {
                throw "LabelPrint local source path does not exist: $localPath"
            }
            Write-Host "LabelPrint local source: $localPath"
            $result.ExtraCMakeArgs = @(
                "-DCMAKE_PREFIX_PATH=",
                "-DLabelPrint_DIR=LabelPrint_DIR-NOTFOUND",
                "-DCMAKE_DISABLE_FIND_PACKAGE_LabelPrint=ON",
                "-DLIS_LABELPRINT_DIR=$localPath"
            )
        }
        default {
        }
    }

    return $result
}

function Invoke-BuildMain([switch]$Clean, [switch]$Run, [string]$BuildConfig) {
    $effectiveGenerator = Resolve-EffectiveGenerator
    $labelPrint = Resolve-LabelPrintBuildOptions $Command
    $buildParams = @{
        Config = $BuildConfig
        Generator = $effectiveGenerator
    }
    if ($Clean) { $buildParams.Clean = $true }
    if ($Run) { $buildParams.Run = $true }
    if ($labelPrint.PackagePath) { $buildParams.LabelPrintPackagePath = $labelPrint.PackagePath }
    $mergedCMakeArgs = @()
    if ($labelPrint.ExtraCMakeArgs.Count -gt 0) { $mergedCMakeArgs += $labelPrint.ExtraCMakeArgs }
    if ($CMakeArgs.Count -gt 0) { $mergedCMakeArgs += $CMakeArgs }
    if ($mergedCMakeArgs.Count -gt 0) { $buildParams.CMakeArgs = $mergedCMakeArgs }
    & (Join-Path $PSScriptRoot "build_main.ps1") @buildParams
}

function Resolve-Makensis {
    if ($NsisPath) {
        if (-not (Test-Path $NsisPath)) {
            throw "NSIS makensis.exe not found: $NsisPath"
        }
        return $NsisPath
    }

    $default = Join-Path ${env:ProgramFiles(x86)} "NSIS\makensis.exe"
    if (Test-Path $default) {
        return $default
    }

    $cmd = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    throw "makensis.exe not found. Install NSIS or pass -NsisPath."
}

function Invoke-NsisInstaller([string]$BuildConfig) {
    $version = Resolve-AppVersion
    New-Item -ItemType Directory -Force $InstallerDir | Out-Null
    $makensis = Resolve-Makensis
    & $makensis `
        "/DAPP_VERSION=$version" `
        "/DAPP_EXE=lis_workbench.exe" `
        "/DBUILD_DIR=..\build\main-app\$BuildConfig" `
        "/DOUTPUT_DIR=..\out\windows\installer" `
        "/DOUTPUT_NAME=$OutputName" `
        "packaging\LISWorkbench.nsi"

    if ($LASTEXITCODE -ne 0) {
        throw "NSIS packaging failed"
    }

    Write-Host "==> Installer: out\windows\installer\$OutputName"
}

function Invoke-Package([switch]$CleanFirst) {
    if ($CleanFirst) {
        Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
        $script:EffectiveGenerator = ""
    }
    Invoke-BuildMain -Clean:$CleanFirst -BuildConfig "Release"

    $version = Resolve-AppVersion
    Invoke-NsisInstaller -BuildConfig "Release"

    & (Join-Path $PSScriptRoot "create_update_package.ps1") `
        -Version $version `
        -BuildDir "build\main-app\Release" `
        -OutputRoot "out\windows\update"

    Write-Host "==> Installer: out\windows\installer\$OutputName"
    Write-Host "==> Update manifest: out\windows\update\updates\manifest.json"
    Write-Host "==> Update package: out\windows\update\updates\LISWorkbench-$version-win7-win11.zip"
}

Push-Location $Root
try {
    switch ($Command) {
        "help" {
            Show-Help
        }
        "clean" {
            Write-Host "==> Cleaning..."
            Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
            Write-Host "==> Removed: build\main-app"
        }
        "build" {
            Invoke-BuildMain -BuildConfig $Config
            Invoke-NsisInstaller -BuildConfig $Config
        }
        "run" {
            Invoke-BuildMain -Run -BuildConfig $Config
        }
        "package" {
            Invoke-Package
        }
        "rebuild-package" {
            Invoke-Package -CleanFirst
        }
    }
} finally {
    Pop-Location
}
