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
$pythonVersion = '3.13.17'
$pythonArchiveName = "python-$pythonVersion-embed-amd64.zip"
$pythonArchiveSha256 = '9e35319bffedab8b36ad44d8f377cb4f1733bcec257a0b6d04d94f01486ddb38'
$msiVersion = $Version.Split('-', 2)[0]
$versionParts = $msiVersion.Split('.') | ForEach-Object { [int]$_ }
if ($versionParts[0] -gt 255 -or $versionParts[1] -gt 255 -or $versionParts[2] -gt 65535) {
    throw 'Release version exceeds Windows Installer version limits.'
}

$appExecutable = Join-Path $publishRoot 'ETS2AP.Ui.exe'
if (-not (Test-Path -LiteralPath $appExecutable -PathType Leaf)) {
    throw 'Published WPF application executable was not found.'
}
$noticeSourcePath = Join-Path $sourceRoot 'NOTICE'
if (-not (Test-Path -LiteralPath $noticeSourcePath -PathType Leaf)) {
    throw 'Project NOTICE file was not found.'
}
$noticeTargetPath = Join-Path $publishRoot 'NOTICE.txt'
if (Test-Path -LiteralPath $noticeTargetPath -PathType Leaf) {
    $sourceNoticeHash = (Get-FileHash -LiteralPath $noticeSourcePath -Algorithm SHA256).Hash
    $targetNoticeHash = (Get-FileHash -LiteralPath $noticeTargetPath -Algorithm SHA256).Hash
    if ($sourceNoticeHash -ne $targetNoticeHash) {
        throw 'Published NOTICE.txt exists with content different from the project NOTICE.'
    }
}
else {
    Copy-Item -LiteralPath $noticeSourcePath -Destination $noticeTargetPath
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
$sdkLicenseSource = Join-Path $sourceRoot 'third_party\scs_sdk\sdk_license.txt'
if (-not (Test-Path -LiteralPath $sdkLicenseSource -PathType Leaf)) {
    throw 'SCS SDK license file was not found.'
}
$connectorLicenseFolder = Join-Path $connectorRoot 'licenses'
[void](New-Item -ItemType Directory -Path $connectorLicenseFolder -Force)
$connectorLicense = Join-Path $connectorLicenseFolder 'scs-sdk-mit.txt'
Copy-Item -LiteralPath $sdkLicenseSource -Destination $connectorLicense
$sdkLicenseHash = (Get-FileHash -LiteralPath $connectorLicense -Algorithm SHA256).Hash.ToLowerInvariant()
$licenseUrl = 'https://polyformproject.org/licenses/noncommercial/1.0.0'
$manifest = [ordered]@{
    format = 1
    version = $Version
    copyright = 'Copyright (c) 2026 Kingsley'
    license = [ordered]@{
        name = 'PolyForm Noncommercial License 1.0.0'
        url = $licenseUrl
    }
    third_party_licenses = @([ordered]@{
        path = 'licenses/scs-sdk-mit.txt'
        sha256 = $sdkLicenseHash
    })
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
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $connectorLicense, 'licenses/scs-sdk-mit.txt',
        [IO.Compression.CompressionLevel]::Optimal) | Out-Null
}
finally {
    $archive.Dispose()
}

$pythonCacheRoot = Join-Path $sourceRoot 'build\python-runtime'
if (-not (Test-Path -LiteralPath $pythonCacheRoot -PathType Container)) {
    [void](New-Item -ItemType Directory -Path $pythonCacheRoot)
}
$pythonArchivePath = Join-Path $pythonCacheRoot $pythonArchiveName
if (Test-Path -LiteralPath $pythonArchivePath -PathType Leaf) {
    $cachedHash = (Get-FileHash -LiteralPath $pythonArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($cachedHash -ne $pythonArchiveSha256) {
        throw 'Cached CPython embeddable package has an unexpected SHA-256 digest.'
    }
}
else {
    $downloadPath = Join-Path $pythonCacheRoot ("$pythonArchiveName." + [guid]::NewGuid().ToString('N') + '.download')
    $pythonUri = "https://www.python.org/ftp/python/$pythonVersion/$pythonArchiveName"
    Invoke-WebRequest -Uri $pythonUri -OutFile $downloadPath
    $downloadHash = (Get-FileHash -LiteralPath $downloadPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($downloadHash -ne $pythonArchiveSha256) {
        throw 'Downloaded CPython embeddable package failed SHA-256 verification.'
    }
    Move-Item -LiteralPath $downloadPath -Destination $pythonArchivePath
}

$pythonRuntimeRoot = Join-Path $outputRoot 'python-runtime-payload'
if (Test-Path -LiteralPath $pythonRuntimeRoot) {
    throw 'Python runtime payload directory already exists; use a fresh release output directory.'
}
[void](New-Item -ItemType Directory -Path $pythonRuntimeRoot)
Expand-Archive -LiteralPath $pythonArchivePath -DestinationPath $pythonRuntimeRoot
$runtimeFiles = @(
    'python.exe', 'python313.dll', 'python313.zip', 'python313._pth',
    'vcruntime140.dll', 'vcruntime140_1.dll', 'LICENSE.txt'
)
foreach ($runtimeFile in $runtimeFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $pythonRuntimeRoot $runtimeFile) -PathType Leaf)) {
        throw "CPython package is missing a required runtime file: $runtimeFile"
    }
}
$pythonPackageSource = Join-Path $sourceRoot 'python\ets2ap'
$pythonServiceRoot = Join-Path $outputRoot 'python'
if (Test-Path -LiteralPath $pythonServiceRoot) {
    throw 'Python service payload directory already exists; use a fresh release output directory.'
}
$pythonModulesRoot = Join-Path $pythonServiceRoot 'ets2ap'
[void](New-Item -ItemType Directory -Path $pythonModulesRoot -Force)
$expectedPythonModuleFiles = @(
    '__init__.py', 'control_client.py', 'control_protocol.py',
    'telemetry.py', 'ui_protocol.py', 'ui_server.py'
)
foreach ($moduleFile in $expectedPythonModuleFiles) {
    $moduleSource = Join-Path $pythonPackageSource $moduleFile
    if (-not (Test-Path -LiteralPath $moduleSource -PathType Leaf)) {
        throw "Python service package is missing a required module: $moduleFile"
    }
    Copy-Item -LiteralPath $moduleSource -Destination (Join-Path $pythonModulesRoot $moduleFile)
}
$pthPath = Join-Path $pythonRuntimeRoot 'python313._pth'
$pthLines = [Collections.Generic.List[string]]::new()
foreach ($line in [IO.File]::ReadAllLines($pthPath)) { $pthLines.Add($line) }
$moduleSearchPath = '..\python'
if (-not $pthLines.Contains($moduleSearchPath)) {
    $currentDirectoryIndex = $pthLines.IndexOf('.')
    if ($currentDirectoryIndex -lt 0) { throw 'CPython module search configuration has no runtime directory entry.' }
    $pthLines.Insert($currentDirectoryIndex + 1, $moduleSearchPath)
    [IO.File]::WriteAllLines($pthPath, $pthLines, [Text.Encoding]::ASCII)
}
$runtimeVersionOutput = & (Join-Path $pythonRuntimeRoot 'python.exe') -I -S -c 'import sys; print(".".join(map(str, sys.version_info[:3])))'
if ($LASTEXITCODE -ne 0 -or [string]::Join('', $runtimeVersionOutput).Trim() -ne $pythonVersion) {
    throw 'Bundled CPython runtime did not report its pinned version.'
}
$moduleImportOutput = & (Join-Path $pythonRuntimeRoot 'python.exe') -I -S -c 'import ets2ap.control_client, ets2ap.ui_server; print("service modules import successfully")'
if ($LASTEXITCODE -ne 0 -or [string]::Join('', $moduleImportOutput).Trim() -ne 'service modules import successfully') {
    throw 'Bundled CPython runtime could not import the packaged UI service modules.'
}

$licenseText = [IO.File]::ReadAllText((Join-Path $sourceRoot 'LICENSE'))
$licenseText = [regex]::Replace($licenseText, '(?m)\r?\n<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->\s*$', '')
$licenseText = [regex]::Replace($licenseText, '(?m)^\s{0,3}#{1,6}\s+', '')
$licenseText = [regex]::Replace($licenseText, '<(https?://[^>]+)>', '$1')
$licenseText = [regex]::Replace($licenseText, '\[([^\]]+)\]\((#[^)]+)\)', '$1')
$licenseText = [regex]::Replace($licenseText, '\[([^\]]+)\]\(([^)]+)\)', '$1 ($2)')
$licenseText = $licenseText.Replace('***', '').Replace('**', '').Replace('__', '').Replace('`', '')
$licenseText = [regex]::Replace($licenseText, '(?m)^\s{0,3}>\s?', '')
if ($licenseText -match '(?m)^\s{0,3}#{1,6}\s|\*\*|\*|__|`|\[[^\]]+\]\([^)]+\)|^\s{0,3}>\s') {
    throw 'PolyForm license contains unconverted Markdown in the MSI display text.'
}
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
        [void]$escapedLicense.Append("\u$($signedCode)?")
    }
}
$licenseRtfPath = Join-Path $outputRoot 'license.rtf'
$licenseRtf = "{\rtf1\ansi\deff0{\fonttbl{\f0\fswiss Arial;}}\viewkind4\uc1\f0\fs18 $($escapedLicense.ToString())}"
[IO.File]::WriteAllText($licenseRtfPath, $licenseRtf, [Text.Encoding]::ASCII)

$msiName = "ETS2AP_V$Version`_x64_en-US.msi"
$wixProject = Join-Path $PSScriptRoot 'ETS2AP.Installer.wixproj'
$wixInputs = @(
    (Join-Path $sourceRoot 'LICENSE'),
    (Join-Path $sourceRoot 'NOTICE'),
    (Join-Path $PSScriptRoot 'Product.wxs'),
    $wixProject,
    $PSCommandPath
)
$wixInputHashes = ($wixInputs | ForEach-Object { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }) -join ''
$wixHasher = [Security.Cryptography.SHA256]::Create()
try {
    $wixFingerprint = [Convert]::ToHexString($wixHasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($wixInputHashes))).Substring(0, 16).ToLowerInvariant()
}
finally {
    $wixHasher.Dispose()
}
$wixBuildRoot = Join-Path $sourceRoot "build\wix-$msiVersion-$wixFingerprint"
$wixIntermediateRoot = [IO.Path]::GetFullPath((Join-Path $wixBuildRoot 'obj')) + [IO.Path]::DirectorySeparatorChar
$wixOutputRoot = [IO.Path]::GetFullPath((Join-Path $wixBuildRoot 'bin')) + [IO.Path]::DirectorySeparatorChar
& dotnet build $wixProject --configuration Release `
    "-p:AppVersion=$msiVersion" `
    "-p:BaseIntermediateOutputPath=$wixIntermediateRoot" `
    "-p:OutputPath=$wixOutputRoot" `
    "-p:PublishDirectory=$publishRoot" `
    "-p:PythonRuntimeDirectory=$pythonRuntimeRoot" `
    "-p:PythonModuleDirectory=$pythonModulesRoot" `
    "-p:LicenseRtf=$licenseRtfPath"
if ($LASTEXITCODE -ne 0) {
    throw "WiX MSI build failed with exit code $LASTEXITCODE."
}
$builtMsi = Join-Path $wixOutputRoot "ETS2AP_$($msiVersion)_x64_en-US.msi"
if (-not (Test-Path -LiteralPath $builtMsi -PathType Leaf)) {
    throw 'WiX did not produce the expected MSI.'
}
$msiPath = Join-Path $outputRoot $msiName
Copy-Item -LiteralPath $builtMsi -Destination $msiPath

$installer = New-Object -ComObject WindowsInstaller.Installer
$database = $installer.OpenDatabase($msiPath, 0)
$expectedRuntimeFiles = @('python.exe', 'python313.dll', 'python313.zip', 'python313._pth', 'LICENSE.txt', 'vcruntime140.dll', 'vcruntime140_1.dll')
$fileView = $database.OpenView('SELECT FileName, Component_ FROM File')
[void]$fileView.Execute()
$runtimeFileComponents = @{}
$pythonModuleFileComponents = @{}
$noticeComponent = $null
while ($record = $fileView.Fetch()) {
    $longName = ($record.StringData(1) -split '\|')[-1]
    if ($expectedRuntimeFiles -contains $longName) { $runtimeFileComponents[$longName] = $record.StringData(2) }
    if ($expectedPythonModuleFiles -contains $longName) { $pythonModuleFileComponents[$longName] = $record.StringData(2) }
    if ($longName -eq 'NOTICE.txt') { $noticeComponent = $record.StringData(2) }
}
$fileView.Close()
$componentView = $database.OpenView('SELECT Component, Directory_ FROM Component')
[void]$componentView.Execute()
$runtimeComponents = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$pythonModuleComponents = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$installFolderComponents = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
while ($record = $componentView.Fetch()) {
    if ($record.StringData(2) -eq 'PythonRuntimeFolder') { [void]$runtimeComponents.Add($record.StringData(1)) }
    if ($record.StringData(2) -eq 'PythonModulesFolder') { [void]$pythonModuleComponents.Add($record.StringData(1)) }
    if ($record.StringData(2) -eq 'INSTALLFOLDER') { [void]$installFolderComponents.Add($record.StringData(1)) }
}
$componentView.Close()
foreach ($runtimeFile in $expectedRuntimeFiles) {
    if (-not $runtimeFileComponents.ContainsKey($runtimeFile) -or
        -not $runtimeComponents.Contains($runtimeFileComponents[$runtimeFile])) {
        throw "Built MSI is missing a required runtime file: runtime\$runtimeFile"
    }
}
foreach ($moduleFile in $expectedPythonModuleFiles) {
    if (-not $pythonModuleFileComponents.ContainsKey($moduleFile) -or
        -not $pythonModuleComponents.Contains($pythonModuleFileComponents[$moduleFile])) {
        throw "Built MSI is missing a required Python service module: python\ets2ap\$moduleFile"
    }
}
if ($null -eq $noticeComponent -or -not $installFolderComponents.Contains($noticeComponent)) {
    throw 'Built MSI is missing NOTICE.txt from the application install folder.'
}

$directoryView = $database.OpenView('SELECT Directory, Directory_Parent, DefaultDir FROM Directory')
[void]$directoryView.Execute()
$runtimeDirectoryFound = $false
$pythonServiceDirectoryFound = $false
$pythonModulesDirectoryFound = $false
while ($record = $directoryView.Fetch()) {
    if ($record.StringData(1) -eq 'PythonRuntimeFolder' -and
        $record.StringData(2) -eq 'INSTALLFOLDER' -and
        ($record.StringData(3) -split '\|')[-1] -eq 'runtime') {
        $runtimeDirectoryFound = $true
    }
    if ($record.StringData(1) -eq 'PythonServiceFolder' -and
        $record.StringData(2) -eq 'INSTALLFOLDER' -and
        ($record.StringData(3) -split '\|')[-1] -eq 'python') { $pythonServiceDirectoryFound = $true }
    if ($record.StringData(1) -eq 'PythonModulesFolder' -and
        $record.StringData(2) -eq 'PythonServiceFolder' -and
        ($record.StringData(3) -split '\|')[-1] -eq 'ets2ap') { $pythonModulesDirectoryFound = $true }
}
$directoryView.Close()
if (-not $runtimeDirectoryFound) { throw 'Built MSI does not install the runtime files under INSTALLFOLDER\runtime.' }
if (-not $pythonServiceDirectoryFound -or -not $pythonModulesDirectoryFound) {
    throw 'Built MSI does not install the Python service package under INSTALLFOLDER\python\ets2ap.'
}

$registryView = $database.OpenView('SELECT * FROM Registry')
[void]$registryView.Execute()
$installLocationFound = $false
while ($record = $registryView.Fetch()) {
    if ($record.IntegerData(2) -eq 2 -and
        $record.StringData(3) -eq 'Software\Kingsley\ETS2 Auto Pilot' -and
        $record.StringData(4) -eq 'InstallLocation' -and
        $record.StringData(5) -eq '[INSTALLFOLDER]') {
        $installLocationFound = $true
        break
    }
}
$registryView.Close()
if (-not $installLocationFound) { throw 'Built MSI does not register its per-machine install location for the native peer check.' }
$database = $null
$installer = $null

foreach ($asset in @($connectorZip, $msiPath)) {
    $item = Get-Item -LiteralPath $asset
    if ($item.Length -le 0) { throw "Release asset is empty: $($item.Name)" }
    Write-Output "$($item.Name) $($item.Length) bytes"
}
Write-Output "CPython $pythonVersion x64 runtime and Python UI service modules bundled and MSI tables verified."
Write-Output 'PolyForm license and project NOTICE included in the MSI; connector manifest includes license and copyright metadata.'
