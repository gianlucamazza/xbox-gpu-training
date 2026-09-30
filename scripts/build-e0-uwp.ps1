$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10"
$sdk = Get-ChildItem "$sdkRoot\Include" -Directory | Where-Object { Test-Path "$($_.FullName)\um\d3d12.h" } | Sort-Object Name -Descending | Select-Object -First 1
if (-not $sdk) { throw "Windows SDK with D3D12 headers missing" }
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -property installationPath
$targets = Get-ChildItem "$vs\MSBuild\Microsoft\VC" -Directory | Sort-Object Name -Descending | Select-Object -First 1
$toolset = Get-ChildItem "$($targets.FullName)\Platforms\x64\PlatformToolsets" -Directory | Where-Object { $_.Name -match '^v\d+$' } | Sort-Object Name -Descending | Select-Object -First 1
if (-not $toolset) { throw "MSVC platform toolset missing" }
& nuget restore uwp/packages.config -PackagesDirectory uwp/packages -NonInteractive
if ($LASTEXITCODE -ne 0) { throw "CppWinRT restore failed" }
$dxc = "$sdkRoot\bin\$($sdk.Name)\x64\dxc.exe"
if (-not (Test-Path $dxc)) { throw "DXC missing" }
& $dxc -T cs_6_0 -E CSMain -Gis -Fo uwp/Assets/e0_tensor.cso src/hlsl/e0_tensor.hlsl
if ($LASTEXITCODE -ne 0) { throw "E0 HLSL compile failed" }
# Stamp package revision before packaging; original manifest is restored after build.
$manifest = Get-Content uwp/AppxManifest.xml -Raw
try {
  $revision = if ($env:GITHUB_RUN_NUMBER) { [int]$env:GITHUB_RUN_NUMBER } else { 0 }
  $manifest.Replace('Version="0.1.0.0"', "Version=`"0.1.0.$revision`"") | Set-Content uwp/AppxManifest.xml -Encoding utf8
  & msbuild uwp/XgpuE0.vcxproj /m /p:Configuration=Release /p:Platform=x64 "/p:XgpuSdkVersion=$($sdk.Name)" "/p:XgpuToolset=$($toolset.Name)" /p:AppxPackageSigningEnabled=false /p:UapAppxPackageBuildMode=SideloadOnly
  if ($LASTEXITCODE -ne 0) { throw "UWP build failed" }
} finally { [IO.File]::WriteAllText("$root\uwp\AppxManifest.xml", $manifest) }
