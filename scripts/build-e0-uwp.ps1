# Build the unsigned E0 UWP Release package.
# Package identity is configurable; defaults keep the identity of installed packages
# (changing Name or Publisher installs a separate app with an empty LocalState).
param(
  [string]$IdentityName = $(if ($env:XGPU_E0_IDENTITY_NAME) { $env:XGPU_E0_IDENTITY_NAME } else { "GianlucaMazza.XgpuE0" }),
  [string]$Publisher = $(if ($env:XGPU_E0_PUBLISHER) { $env:XGPU_E0_PUBLISHER } else { "CN=uwp-crossbuild-dev" }),
  [string]$PublisherDisplayName = $(if ($env:XGPU_E0_PUBLISHER_DISPLAY_NAME) { $env:XGPU_E0_PUBLISHER_DISPLAY_NAME } else { "Gianluca Mazza" }),
  [string]$Version = $(if ($env:XGPU_E0_VERSION) { $env:XGPU_E0_VERSION } elseif ($env:GITHUB_RUN_NUMBER) { "0.1.0.$([int]$env:GITHUB_RUN_NUMBER)" } else { "0.1.0.0" }),
  [string]$Commit = $(if ($env:GITHUB_SHA) { $env:GITHUB_SHA } else { "unknown" })
)
$ErrorActionPreference = "Stop"
if ($Version -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw "Version must be a four-part number: $Version" }
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
# Stamp identity and version before packaging; the original manifest is restored after build.
$manifestPath = "$root\uwp\AppxManifest.xml"
$original = [IO.File]::ReadAllText($manifestPath)
try {
  $xml = New-Object System.Xml.XmlDocument
  $xml.PreserveWhitespace = $true
  $xml.LoadXml($original)
  $xml.Package.Identity.Name = $IdentityName
  $xml.Package.Identity.Publisher = $Publisher
  $xml.Package.Identity.Version = $Version
  $xml.Package.Properties.PublisherDisplayName = $PublisherDisplayName
  $xml.Save($manifestPath)
  Write-Host "E0 package identity: $IdentityName $Version ($Publisher), commit $Commit"
  & msbuild uwp/XgpuE0.vcxproj /m /p:Configuration=Release /p:Platform=x64 "/p:XgpuSdkVersion=$($sdk.Name)" "/p:XgpuToolset=$($toolset.Name)" "/p:XgpuCommit=$Commit" /p:AppxPackageSigningEnabled=false /p:UapAppxPackageBuildMode=SideloadOnly
  if ($LASTEXITCODE -ne 0) { throw "UWP build failed" }
} finally { [IO.File]::WriteAllText($manifestPath, $original) }
