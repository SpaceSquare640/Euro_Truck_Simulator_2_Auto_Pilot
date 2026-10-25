# Edited by: OpenAI-Codex_Responsible: Source_Code
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$PublishDirectory,
    [Parameter(Mandatory = $true)][string]$NativePlugin,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($Version -notmatch '^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$') {
    throw 'Release version is not a supported semantic version.'
}

$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$publishRoot = (Resolve-Path -LiteralPath $PublishDirectory).Path
$nativePluginPath = (Resolve-Path -LiteralPath $NativePlugin).Path
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
$msiVersion = $Version.Split('-', 2)[0]
$versionParts = $msiVersion.Split('.') | ForEach-Object { [int]$_ }
if ($versionParts[0] -gt 255 -or $versionParts[1] -gt 255 -or $versionParts[2] -gt 65535) {
    throw 'Release version exceeds Windows Installer version limits.'
}

$appExecutable = Join-Path $publishRoot 'ETS2AP.Ui.exe'
if (-not (Test-Path -LiteralPath $appExecutable -PathType Leaf)) {
    throw 'Published WPF application executable was not found.'
}
$pluginBytes = [IO.File]::ReadAllBytes($nativePluginPath)
if ($pluginBytes.Length -lt 2 -or $pluginBytes[0] -ne 0x4D -or $pluginBytes[1] -ne 0x5A) {
    throw 'Native connector is not a Windows PE image.'
}

if (Test-Path -LiteralPath $outputRoot) {
    if ((Get-ChildItem -LiteralPath $outputRoot -Force | Measure-Object).Count -ne 0) {
        throw 'Release output directory must be new and empty.'
    }
}
else {
    [void](New-Item -ItemType Directory -Path $outputRoot)
}

$connectorRoot = Join-Path $outputRoot 'connector-package'
$pluginFolder = Join-Path $connectorRoot 'plugins'
[void](New-Item -ItemType Directory -Path $pluginFolder -Force)
$connectorPlugin = Join-Path $pluginFolder 'ets2ap_plugin.dll'
Copy-Item -LiteralPath $nativePluginPath -Destination $connectorPlugin
$pluginHash = (Get-FileHash -LiteralPath $connectorPlugin -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest = [ordered]@{
    format = 1
    version = $Version
    files = @([ordered]@{ name = 'ets2ap_plugin.dll'; sha256 = $pluginHash })
}
$manifestPath = Join-Path $connectorRoot 'manifest.json'
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))

Add-Type -AssemblyName System.IO.Compression.FileSystem
$connectorZip = Join-Path $outputRoot "ETS2AP_DLL_V$Version.zip"
$archive = [IO.Compression.ZipFile]::Open($connectorZip, [IO.Compression.ZipArchiveMode]::Create)
try {
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $manifestPath, 'manifest.json',
        [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $connectorPlugin, 'plugins/ets2ap_plugin.dll',
        [IO.Compression.CompressionLevel]::Optimal) | Out-Null
}
finally {
    $archive.Dispose()
}

$licenseText = [IO.File]::ReadAllText((Join-Path $sourceRoot 'LICENSE'))
$escapedLicense = [Text.StringBuilder]::new()
foreach ($character in $licenseText.ToCharArray()) {
    $code = [int]$character
    if ($code -eq 92) { [void]$escapedLicense.Append('\\') }
    elseif ($code -eq 123) { [void]$escapedLicense.Append('\{') }
    elseif ($code -eq 125) { [void]$escapedLicense.Append('\}') }
    elseif ($code -eq 13) { continue }
    elseif ($code -eq 10) { [void]$escapedLicense.Append('\par').Append([Environment]::NewLine) }
    elseif ($code -lt 128) { [void]$escapedLicense.Append($character) }
    else {
        $signedCode = if ($code -gt 32767) { $code - 65536 } else { $code }
        [void]$escapedLicense.Append("\u$signedCode?")
    }
}
$licenseRtfPath = Join-Path $outputRoot 'license.rtf'
$licenseRtf = "{\rtf1\ansi\deff0{\fonttbl{\f0\fswiss Arial;}}\viewkind4\uc1\f0\fs18 $($escapedLicense.ToString())}"
[IO.File]::WriteAllText($licenseRtfPath, $licenseRtf, [Text.Encoding]::ASCII)

$msiName = "ETS2AP_V$Version`_x64_en-US.msi"
$wixProject = Join-Path $PSScriptRoot 'ETS2AP.Installer.wixproj'
& dotnet build $wixProject --configuration Release `
    "-p:AppVersion=$msiVersion" `
    "-p:PublishDirectory=$publishRoot" `
    "-p:LicenseRtf=$licenseRtfPath"
if ($LASTEXITCODE -ne 0) {
    throw "WiX MSI build failed with exit code $LASTEXITCODE."
}
$builtMsi = Join-Path $PSScriptRoot "bin\Release\ETS2AP_$($msiVersion)_x64_en-US.msi"
if (-not (Test-Path -LiteralPath $builtMsi -PathType Leaf)) {
    throw 'WiX did not produce the expected MSI.'
}
$msiPath = Join-Path $outputRoot $msiName
Copy-Item -LiteralPath $builtMsi -Destination $msiPath

$setupName = "ETS2AP_V$Version`_x64-setup.exe"
$setupPath = Join-Path $outputRoot $setupName
$sedPath = Join-Path $outputRoot 'setup.sed'
$sed = @"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=1
HideExtractAnimation=0
UseLongFileName=1
InsideCompressed=1
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=%PostInstallCmd%
AdminQuietInstCmd=%AdminQuietInstCmd%
UserQuietInstCmd=%UserQuietInstCmd%
SourceFiles=SourceFiles
[Strings]
InstallPrompt=
DisplayLicense=
FinishMessage=Installation complete.
TargetName=$setupPath
FriendlyName=ETS2 Auto Pilot $Version Setup
AppLaunched=msiexec.exe /i `"$msiName`"
PostInstallCmd=<None>
AdminQuietInstCmd=
UserQuietInstCmd=
FILE0=`"$msiName`"
[SourceFiles]
SourceFiles0=$outputRoot\
[SourceFiles0]
%FILE0%=
"@
[IO.File]::WriteAllText($sedPath, $sed, [Text.Encoding]::Default)

$iexpress = Join-Path $env:WINDIR 'System32\iexpress.exe'
if (-not (Test-Path -LiteralPath $iexpress -PathType Leaf)) {
    throw 'Windows IExpress is not available on this build runner.'
}
& $iexpress /N /Q $sedPath
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $setupPath -PathType Leaf)) {
    throw "IExpress setup EXE build failed with exit code $LASTEXITCODE."
}

foreach ($asset in @($connectorZip, $msiPath, $setupPath)) {
    $item = Get-Item -LiteralPath $asset
    if ($item.Length -le 0) { throw "Release asset is empty: $($item.Name)" }
    Write-Output "$($item.Name) $($item.Length) bytes"
}
