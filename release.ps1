# CClickGUI Release Packager
# 用法:
#   powershell -ExecutionPolicy Bypass -File release.ps1
#   powershell -ExecutionPolicy Bypass -File release.ps1 -Compiler "D:\mingw64\bin\g++.exe"
#   powershell -ExecutionPolicy Bypass -File release.ps1 -SkipBuild
#
# 产物:
#   dist/CClickGUI-<version>-win64-mingw.zip
#   dist/SHA256SUMS.txt

[CmdletBinding()]
param(
    [string]$Version = "",
    [string]$Compiler = "D:\mingw64\bin\g++.exe",
    [string]$BuildDir = "build\release",
    [string]$DistDir = "dist",
    [string]$MakeProgram = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

function Invoke-Checked {
    param(
        [scriptblock]$Command,
        [string]$Description
    )
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Description 失败（退出码 $LASTEXITCODE）"
    }
}

# ---- 读取 / 校验版本号 ----
$HeaderPath = Join-Path $Root "include\CClickGUI.h"
$Header = Get-Content $HeaderPath -Raw -Encoding UTF8
$Match = [regex]::Match($Header, '#define\s+CGUI_VERSION_STRING\s+"([^"]+)"')
if (-not $Match.Success) {
    throw "无法从 include\CClickGUI.h 读取 CGUI_VERSION_STRING"
}
$HeaderVersion = $Match.Groups[1].Value
if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = $HeaderVersion
}
elseif ($Version -ne $HeaderVersion) {
    throw "指定版本 $Version 与头文件版本 $HeaderVersion 不一致"
}

if (-not (Test-Path $Compiler)) {
    throw "找不到 g++：$Compiler（可用 -Compiler 指定路径）"
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "找不到 cmake，请先安装 CMake 并加入 PATH"
}

function Test-ReadableFile {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    try {
        $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        $stream.Close()
        return $true
    }
    catch { return $false }
}

# Ninja 在 WinGet 下可能是不可读取的符号链接，自动回退到真实安装路径
$NinjaPath = $null
if (-not [string]::IsNullOrWhiteSpace($MakeProgram)) {
    if (-not (Test-ReadableFile $MakeProgram)) { throw "指定的 ninja 不可读取：$MakeProgram" }
    $NinjaPath = $MakeProgram
}
else {
    $NinjaCmd = Get-Command ninja -ErrorAction SilentlyContinue
    if ($NinjaCmd -and (Test-ReadableFile $NinjaCmd.Source)) { $NinjaPath = $NinjaCmd.Source }
    if (-not $NinjaPath -and $env:LOCALAPPDATA) {
        $PackageRoot = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages"
        if (Test-Path $PackageRoot) {
            $Candidates = Get-ChildItem $PackageRoot -Recurse -Filter ninja.exe -File -ErrorAction SilentlyContinue
            foreach ($Candidate in $Candidates) {
                if (Test-ReadableFile $Candidate.FullName) { $NinjaPath = $Candidate.FullName; break }
            }
        }
    }
    if (-not $NinjaPath) {
        throw "找不到可读取的 ninja，请安装 Ninja 或通过 -MakeProgram 指定 ninja.exe"
    }
}

Write-Host "==> CClickGUI $Version" -ForegroundColor Cyan

# ---- 构建 Release：静态库 + 演示程序 ----
if (-not $SkipBuild) {
    Write-Host "==> 配置 CMake（Ninja / Release）" -ForegroundColor Cyan
    $CMakeConfigure = @(
        "-S", $Root,
        "-B", (Join-Path $Root $BuildDir),
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_CXX_COMPILER=$Compiler",
        "-DCMAKE_MAKE_PROGRAM=$NinjaPath"
    )
    Invoke-Checked { cmake @CMakeConfigure } "CMake 配置"

    Write-Host "==> 构建静态库与演示程序" -ForegroundColor Cyan
    Invoke-Checked { cmake --build (Join-Path $Root $BuildDir) --config Release } "CMake 构建"
}

$BuildPath = Join-Path $Root $BuildDir
$LibPath = Join-Path $BuildPath "libclickgui.a"
$DemoPath = Join-Path $BuildPath "examples\cgu_demo.exe"
if (-not (Test-Path $LibPath)) { throw "未找到构建产物：$LibPath" }
if (-not (Test-Path $DemoPath)) { throw "未找到构建产物：$DemoPath" }

# ---- 组装发布目录 ----
$PackageName = "CClickGUI-$Version-win64-mingw"
$PackageDir = Join-Path (Join-Path $Root $DistDir) $PackageName
if (Test-Path $PackageDir) { Remove-Item $PackageDir -Recurse -Force }
New-Item -ItemType Directory -Path $PackageDir -Force | Out-Null

$Dirs = @("include", "lib", "bin", "src", "examples")
foreach ($Dir in $Dirs) {
    New-Item -ItemType Directory -Path (Join-Path $PackageDir $Dir) -Force | Out-Null
}

Copy-Item (Join-Path $Root "include\CClickGUI.h") (Join-Path $PackageDir "include")
Copy-Item $LibPath (Join-Path $PackageDir "lib")
Copy-Item $DemoPath (Join-Path $PackageDir "bin")
Copy-Item (Join-Path $Root "src\*") (Join-Path $PackageDir "src")
Copy-Item (Join-Path $Root "examples\Demo.cpp") (Join-Path $PackageDir "examples")
Copy-Item (Join-Path $Root "examples\CMakeLists.txt") (Join-Path $PackageDir "examples")
foreach ($File in @("CMakeLists.txt", "build.bat", "release.ps1", "README.md", "CHANGELOG.md")) {
    Copy-Item (Join-Path $Root $File) $PackageDir
}

# 生成包内版本说明
$BuildInfo = @(
    "CClickGUI $Version",
    "目标平台: x86_64-w64-mingw32",
    "构建时间: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')",
    "编译器: $(& $Compiler --version | Select-Object -First 1)",
    "",
    "目录说明:",
    "  include/  公开头文件",
    "  lib/      MinGW-w64 静态库 libclickgui.a",
    "  bin/      MinGW-w64 演示程序 cgu_demo.exe",
    "  src/      框架源码",
    "  examples/ 示例程序"
)
$BuildInfo | Set-Content (Join-Path $PackageDir "BUILD_INFO.txt") -Encoding UTF8

# ---- 压缩与校验 ----
$ZipPath = Join-Path (Join-Path $Root $DistDir) "$PackageName.zip"
if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
Compress-Archive -Path $PackageDir -DestinationPath $ZipPath -CompressionLevel Optimal

$Hash = (Get-FileHash $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
$SumsPath = Join-Path (Join-Path $Root $DistDir) "SHA256SUMS.txt"
"$Hash  $(Split-Path -Leaf $ZipPath)" | Set-Content $SumsPath -Encoding ASCII

$SizeMB = [math]::Round((Get-Item $ZipPath).Length / 1MB, 2)
Write-Host ""
Write-Host "发布包已生成：" -ForegroundColor Green
Write-Host "  $ZipPath ($SizeMB MB)"
Write-Host "  SHA256: $Hash"
Write-Host "  校验文件: $SumsPath"
