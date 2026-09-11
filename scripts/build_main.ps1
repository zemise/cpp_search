# Build and run Win32 main application
# Usage: .\scripts\build_main.ps1 [-Clean] [-Run] [-Config Release|Debug] [-Generator "Visual Studio 17 2022"] [-LabelPrintPackagePath C:\Deps\LabelPrint\v1.2.10] [-CMakeArgs "-DKEY=VALUE"]

param(
    [switch]$Clean,
    [switch]$Run,
    [string]$Config = "Release",
    [string]$Generator = "",
    [string]$LabelPrintPackagePath = "",
    [string[]]$CMakeArgs = @()
)

$ErrorActionPreference = "Stop"
$BuildDir = "build\main-app"
$Exe = "$BuildDir\$Config\lis_workbench.exe"
$UpdaterExe = "$BuildDir\$Config\Updater.exe"

function Assert-LabelPrintPackagePath([string]$Path) {
    if (-not $Path) { return }
    if (-not (Test-Path $Path)) {
        throw "LabelPrint package path does not exist: $Path"
    }
    $configFile = Join-Path $Path "cmake\LabelPrintConfig.cmake"
    if (-not (Test-Path $configFile)) {
        throw "LabelPrint package path must point to an extracted release root containing cmake\LabelPrintConfig.cmake: $Path"
    }
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

# Detect VS generator. Prefer installed VS 2022 for Windows 7 runtime compatibility,
# otherwise fall back to the newest installed VS that CMake supports.
$cmakeGenerators = Get-CmakeVisualStudioGenerators
if ($Generator) {
    $vsGen = $Generator
} else {
    $installedGenerators = Get-InstalledVisualStudioGenerators
    $preferred = @("Visual Studio 17 2022", "Visual Studio 18 2026")
    foreach ($candidate in $preferred) {
        if (($installedGenerators -contains $candidate) -and ($cmakeGenerators -contains $candidate)) {
            $vsGen = $candidate
            break
        }
    }
    if (-not $vsGen -and $installedGenerators.Count -gt 0) {
        foreach ($candidate in $installedGenerators) {
            if ($cmakeGenerators -contains $candidate) {
                $vsGen = $candidate
                break
            }
        }
    }
    if (-not $vsGen -and $cmakeGenerators.Count -gt 0) {
        $vsGen = $cmakeGenerators[0]
    }
    if (-not $vsGen) { $vsGen = "Visual Studio 17 2022" }
}
Write-Host "Generator: $vsGen"
Assert-LabelPrintPackagePath $LabelPrintPackagePath

# Clean
if ($Clean) {
    Write-Host "==> Cleaning..."
    Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
}

# Configure. Re-run configure when external CMake arguments are supplied so
# package paths such as LabelPrint release zips can update an existing cache.
$NeedsConfigure = -not (Test-Path "$BuildDir\CMakeCache.txt")
if ($LabelPrintPackagePath -or $CMakeArgs.Count -gt 0) {
    $NeedsConfigure = $true
}

if ($NeedsConfigure) {
    Write-Host "==> Configuring..."
    $configureArgs = @("-S", ".", "-B", $BuildDir, "-G", $vsGen, "-A", "x64", "-DLIS_STATIC_MSVC_RUNTIME=ON")
    if ($LabelPrintPackagePath) {
        Write-Host "LabelPrint package: $LabelPrintPackagePath"
        $configureArgs += "-DCMAKE_PREFIX_PATH=$LabelPrintPackagePath"
    }
    if ($CMakeArgs.Count -gt 0) {
        $configureArgs += $CMakeArgs
    }
    if ($vsGen -notmatch "^Visual Studio ") {
        $configureArgs += "-DCMAKE_BUILD_TYPE=$Config"
    }
    cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
}

# Build
Write-Host "==> Building ($Config)..."
cmake --build $BuildDir --target main_app --config $Config -j $env:NUMBER_OF_PROCESSORS
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
cmake --build $BuildDir --target updater --config $Config -j $env:NUMBER_OF_PROCESSORS
if ($LASTEXITCODE -ne 0) { throw "Updater build failed" }

Write-Host "==> Ready: $Exe"
Write-Host "==> Updater: $UpdaterExe"

# Run
if ($Run) {
    Write-Host "==> Running..."
    Start-Process -FilePath (Join-Path (Get-Location) $Exe)
}
