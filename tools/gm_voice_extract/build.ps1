# =============================================================================
# build.ps1 — gm_extract を CMake でビルドし、必要なら抽出まで実行
#
# cmake が PATH に無い場合、Visual Studio 2026/2022 にバンドルされた cmake.exe
# を自動検出します。-CMakePath で明示も可能。
#
# 既存ワークフローと同じ書き方:
#   $cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
#   .\build.ps1 -CMakePath $cmake -ToolchainFile "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" -Run .\GeneralUser-GS.sf2
# =============================================================================
param(
    [switch]$Clean,
    [string]$Run = "",
    [string]$Out = "generated",
    [string]$Programs = "",
    [string]$Config = "Release",
    [string]$Generator = "",
    [string]$CMakePath = "",
    [string]$ToolchainFile = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

# --- resolve cmake: -CMakePath > PATH > VS auto-detect ---
if ($CMakePath) {
    if (-not (Test-Path $CMakePath)) { throw "cmake.exe not found: $CMakePath" }
    $cmake = $CMakePath
} elseif (Get-Command cmake -ErrorAction SilentlyContinue) {
    $cmake = "cmake"
} else {
    $vsRoots = @(
        "C:\Program Files\Microsoft Visual Studio\18",
        "C:\Program Files\Microsoft Visual Studio\2022"
    )
    $cmake = $null
    foreach ($root2 in $vsRoots) {
        if (-not (Test-Path $root2)) { continue }
        $found = Get-ChildItem $root2 -Directory -ErrorAction SilentlyContinue |
            ForEach-Object { Join-Path $_.FullName "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" } |
            Where-Object { Test-Path $_ } |
            Select-Object -First 1
        if ($found) { $cmake = $found; break }
    }
    if (-not $cmake) {
        throw "cmake not found on PATH or VS install. Use -CMakePath, or open 'Developer PowerShell for VS 2026'."
    }
    Write-Host "auto-detected cmake: $cmake" -ForegroundColor DarkCyan
}

if ($Clean -and (Test-Path build)) {
    Write-Host "cleaning build/ ..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force build
}

# configure if not already configured (check the CMake cache, not just the dir)
if (-not (Test-Path "build\CMakeCache.txt")) {
    $cfgArgs = @("-S", ".", "-B", "build")
    if ($Generator)     { $cfgArgs += @("-G", $Generator) }
    if ($ToolchainFile) { $cfgArgs += "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile" }
    Write-Host "configuring ($cmake $($cfgArgs -join ' ')) ..." -ForegroundColor Cyan
    & $cmake @cfgArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "cmake configure failed; wiping build\ so next run will retry" -ForegroundColor Yellow
        if (Test-Path build) { Remove-Item -Recurse -Force build }
        throw "cmake configure failed"
    }
}

Write-Host "building ($Config) ..." -ForegroundColor Cyan
& $cmake --build build --config $Config
if ($LASTEXITCODE -ne 0) { throw "build failed" }

$candidates = @(
    "build/$Config/gm_extract.exe",
    "build/gm_extract.exe",
    "build/$Config/gm_extract",
    "build/gm_extract"
)
$exe = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) { throw "ビルド成果物が見つかりません (候補: $($candidates -join ', '))" }
Write-Host "built: $exe" -ForegroundColor Green

if ($Run) {
    if (-not (Test-Path $Run)) { throw "SF2 が見つかりません: $Run" }
    $exeArgs = @($Run, "--out", $Out)
    if ($Programs) { $exeArgs += @("--programs", $Programs) }
    Write-Host "running: $exe $($exeArgs -join ' ')" -ForegroundColor Cyan
    & ".\$exe" @exeArgs
    if ($LASTEXITCODE -ne 0) { throw "gm_extract が異常終了 (exit $LASTEXITCODE)" }
    Write-Host "生成物: $Out\voice_table.cpp, $Out\voices\voice_table.h, $Out\voices\voice_harm_data.h" -ForegroundColor Green
}
