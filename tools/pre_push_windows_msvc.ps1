param(
    [string]$BuildDir = "",
    [switch]$Clean,
    [switch]$Ci,
    [switch]$Shipping
)

$ErrorActionPreference = "Stop"

if ($env:OS -ne "Windows_NT") {
    Write-Error "windows-msvc preflight must run on Windows."
    exit 1
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $RepoRoot

if (-not $BuildDir) {
    if ($Ci) {
        $BuildDir = if ($Shipping) { "build-windows-shipping" } else { "build" }
    } else {
        $BuildDir = if ($Shipping) { "out/build/prepush-windows-msvc-shipping" } else { "out/build/prepush-windows-msvc" }
    }
}

function Resolve-RepoPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Assert-PathUnderRepo {
    param([string]$Path)

    $FullPath = Resolve-RepoPath $Path
    $RepoFull = [System.IO.Path]::GetFullPath($RepoRoot)
    $Comparison = [System.StringComparison]::OrdinalIgnoreCase
    $FullTrimmed = $FullPath.TrimEnd('\', '/')
    $RepoTrimmed = $RepoFull.TrimEnd('\', '/')

    if ($FullTrimmed -eq $RepoTrimmed -or -not $FullTrimmed.StartsWith($RepoTrimmed + [System.IO.Path]::DirectorySeparatorChar, $Comparison)) {
        throw "Refusing to clean build path outside repo: $FullPath"
    }

    return $FullPath
}

function Find-VsDevCmd {
    $InstallerDir = "C:\Program Files (x86)\Microsoft Visual Studio\Installer"
    if (Test-Path -LiteralPath $InstallerDir) {
        $env:PATH = "$InstallerDir;$env:PATH"
    }

    $VsWhere = Join-Path $InstallerDir "vswhere.exe"
    if (Test-Path -LiteralPath $VsWhere) {
        $InstallPath = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -eq 0 -and $InstallPath) {
            $Candidate = Join-Path ($InstallPath | Select-Object -First 1) "Common7\Tools\VsDevCmd.bat"
            if (Test-Path -LiteralPath $Candidate) {
                return $Candidate
            }
        }
    }

    $Candidates = @(
        "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
    )

    foreach ($Candidate in $Candidates) {
        if (Test-Path -LiteralPath $Candidate) {
            return $Candidate
        }
    }

    throw "VsDevCmd.bat not found."
}

function Find-VcpkgRoot {
    $Preferred = "C:\vcpkg"
    if (Test-Path -LiteralPath (Join-Path $Preferred "scripts\buildsystems\vcpkg.cmake")) {
        return $Preferred
    }

    if ($env:VCPKG_ROOT -and (Test-Path -LiteralPath (Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"))) {
        return $env:VCPKG_ROOT
    }

    return $null
}

function Invoke-CmdChecked {
    param(
        [string]$Command,
        [string]$LogPath
    )

    cmd /d /s /c "$Command 2>&1" | Tee-Object -FilePath $LogPath
    $Exit = $LASTEXITCODE
    if ($Exit -ne 0) {
        throw "Command failed with exit $Exit. See $LogPath"
    }
}

$BuildPath = Assert-PathUnderRepo $BuildDir
$LogDir = Join-Path $RepoRoot "logs/build"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$ModeName = if ($Shipping) { "shipping" } else { "debug" }
$CMakeBuildType = if ($Shipping) { "Release" } else { "Debug" }
$ConfigName = if ($Shipping) { "Release" } else { "Debug" }
$ShippingArg = if ($Shipping) { " -DFATE_SHIPPING=ON" } else { "" }
$LogPath = Join-Path $LogDir "prepush-windows-msvc-$ModeName.log"
$VsDevCmd = Find-VsDevCmd
$VcpkgRoot = Find-VcpkgRoot

$ProbeCommand = "call `"$VsDevCmd`" -arch=x64 -no_logo >nul && where cl && cl"
$ProbeOutput = cmd /d /s /c "$ProbeCommand 2>&1"
$ClPath = $ProbeOutput | Where-Object { $_ -match '\\cl\.exe$' } | Select-Object -First 1
$ClVersion = $ProbeOutput | Where-Object { $_ -match 'Microsoft.*C/C\+\+.*Version' } | Select-Object -First 1
if (-not $ClPath -or -not $ClVersion) {
    Write-Host ($ProbeOutput -join "`n")
    Write-Error "Unable to resolve MSVC compiler from VsDevCmd."
    exit 1
}

$CMakeVersion = (& cmake --version | Select-Object -First 1)
$Stamp = @(
    "VsDevCmd=$VsDevCmd",
    "VcpkgRoot=$VcpkgRoot",
    "ClPath=$ClPath",
    "ClVersion=$ClVersion",
    "CMake=$CMakeVersion",
    "BuildType=$CMakeBuildType",
    "FATE_SHIPPING=$Shipping",
    "FATE_FORCE_DEMO_BUILD=ON"
) -join "`n"
$StampPath = Join-Path $BuildPath ".fate_preflight_toolchain"

if (Test-Path -LiteralPath $BuildPath) {
    $ExistingStamp = if (Test-Path -LiteralPath $StampPath) { Get-Content -LiteralPath $StampPath -Raw } else { "" }
    if ($Clean -or ($ExistingStamp.Trim() -ne $Stamp.Trim())) {
        Write-Host "Cleaning stale preflight build directory: $BuildPath" -ForegroundColor Yellow
        Remove-Item -LiteralPath $BuildPath -Recurse -Force
    }
}

New-Item -ItemType Directory -Force -Path $BuildPath | Out-Null

Write-Host "=== windows-msvc $ModeName preflight ===" -ForegroundColor Cyan
Write-Host "Repo:      $RepoRoot"
Write-Host "BuildDir:  $BuildPath"
Write-Host "Mode:      $ModeName"
Write-Host "VsDevCmd:  $VsDevCmd"
Write-Host "vcpkg:     $VcpkgRoot"
Write-Host "Compiler:  $ClVersion"
Write-Host "Log:       $LogPath"
Write-Host ""

$VcpkgEnvCommand = ""
$ToolchainArg = ""
if ($VcpkgRoot) {
    $VcpkgEnvCommand = " && set `"VCPKG_ROOT=$VcpkgRoot`""
    $ToolchainPath = (Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake") -replace '\\', '/'
    $ToolchainArg = " -DCMAKE_TOOLCHAIN_FILE=`"$ToolchainPath`""
}

$BuildCommand = "call `"$VsDevCmd`" -arch=x64 -no_logo$VcpkgEnvCommand && cmake -S `"$RepoRoot`" -B `"$BuildPath`" -DCMAKE_BUILD_TYPE=$CMakeBuildType -DFATE_FORCE_DEMO_BUILD=ON$ShippingArg$ToolchainArg && cmake --build `"$BuildPath`" --config $ConfigName --target fate_engine -j4 && cmake --build `"$BuildPath`" --config $ConfigName --target FateDemo -j4"

try {
    Invoke-CmdChecked -Command $BuildCommand -LogPath $LogPath
    Set-Content -LiteralPath $StampPath -Value $Stamp -NoNewline
    Write-Host ""
    Write-Host "windows-msvc $ModeName preflight OK" -ForegroundColor Green
    exit 0
} catch {
    Write-Host ""
    Write-Error $_
    exit 1
}
