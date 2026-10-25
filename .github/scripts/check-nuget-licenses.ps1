# Edited by: OpenAI-Codex_Responsible: Source_Code
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$AssetsFile,

    [string[]]$AdditionalPackage = @(),

    [switch]$AllowCiOnlyWixMsRl
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$allowedLicenses = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
foreach ($license in @('MIT', 'BSD-2-Clause', 'BSD-3-Clause', 'Apache-2.0', 'Zlib', 'ISC')) {
    [void]$allowedLicenses.Add($license)
}

function Remove-OuterParentheses {
    param([Parameter(Mandatory = $true)][string]$Expression)

    $value = $Expression.Trim()
    while ($value.Length -ge 2 -and $value[0] -eq '(' -and $value[$value.Length - 1] -eq ')') {
        $depth = 0
        $wrapsEntireExpression = $true
        for ($index = 0; $index -lt $value.Length; $index++) {
            if ($value[$index] -eq '(') { $depth++ }
            elseif ($value[$index] -eq ')') { $depth-- }

            if ($depth -lt 0) { throw "Malformed SPDX expression: $Expression" }
            if ($depth -eq 0 -and $index -lt ($value.Length - 1)) {
                $wrapsEntireExpression = $false
                break
            }
        }

        if ($depth -ne 0) { throw "Malformed SPDX expression: $Expression" }
        if (-not $wrapsEntireExpression) { break }
        $value = $value.Substring(1, $value.Length - 2).Trim()
    }

    return $value
}

function Split-TopLevelOperator {
    param(
        [Parameter(Mandatory = $true)][string]$Expression,
        [Parameter(Mandatory = $true)][ValidateSet('AND', 'OR')][string]$Operator
    )

    $parts = [System.Collections.Generic.List[string]]::new()
    $separator = " $Operator "
    $depth = 0
    $start = 0
    $index = 0

    while ($index -lt $Expression.Length) {
        if ($Expression[$index] -eq '(') {
            $depth++
            $index++
            continue
        }
        if ($Expression[$index] -eq ')') {
            $depth--
            if ($depth -lt 0) { throw "Malformed SPDX expression: $Expression" }
            $index++
            continue
        }

        if ($depth -eq 0 -and
            ($index + $separator.Length) -le $Expression.Length -and
            [string]::Compare($Expression, $index, $separator, 0, $separator.Length,
                [System.StringComparison]::OrdinalIgnoreCase) -eq 0) {
            $parts.Add($Expression.Substring($start, $index - $start).Trim())
            $index += $separator.Length
            $start = $index
            continue
        }

        $index++
    }

    if ($depth -ne 0) { throw "Malformed SPDX expression: $Expression" }
    if ($parts.Count -gt 0) { $parts.Add($Expression.Substring($start).Trim()) }
    else { $parts.Add($Expression.Trim()) }
    return ,$parts.ToArray()
}

function Test-LicenseExpression {
    param(
        [Parameter(Mandatory = $true)][string]$Expression,
        [Parameter(Mandatory = $true)][string]$PackageId,
        [Parameter(Mandatory = $true)][bool]$AllowWixMsRl
    )

    $value = Remove-OuterParentheses $Expression
    if ([string]::IsNullOrWhiteSpace($value)) { return $false }

    $orParts = Split-TopLevelOperator $value 'OR'
    if ($orParts.Count -gt 1) {
        foreach ($part in $orParts) {
            if (Test-LicenseExpression $part $PackageId $AllowWixMsRl) { return $true }
        }
        return $false
    }

    $andParts = Split-TopLevelOperator $value 'AND'
    if ($andParts.Count -gt 1) {
        foreach ($part in $andParts) {
            if (-not (Test-LicenseExpression $part $PackageId $AllowWixMsRl)) { return $false }
        }
        return $true
    }

    if ($value -match '\s+WITH\s+') { return $false }
    if ($allowedLicenses.Contains($value)) { return $true }
    return $AllowWixMsRl -and
        $PackageId.StartsWith('WixToolset.', [System.StringComparison]::OrdinalIgnoreCase) -and
        $value -eq 'MS-RL'
}

function Get-PackageLicense {
    param(
        [Parameter(Mandatory = $true)][string]$PackageId,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$PackageDirectory,
        [Parameter(Mandatory = $true)][bool]$AllowWixMsRl
    )

    $nuspecs = @(Get-ChildItem -LiteralPath $PackageDirectory -Filter '*.nuspec' -File)
    if ($nuspecs.Count -ne 1) {
        $script:failures.Add("${PackageId}/${Version}: expected exactly one .nuspec in the restored package.")
        return
    }

    $settings = [System.Xml.XmlReaderSettings]::new()
    $settings.DtdProcessing = [System.Xml.DtdProcessing]::Prohibit
    $settings.XmlResolver = $null
    $reader = [System.Xml.XmlReader]::Create($nuspecs[0].FullName, $settings)
    try {
        $document = [System.Xml.XmlDocument]::new()
        $document.XmlResolver = $null
        $document.Load($reader)
    }
    finally {
        $reader.Dispose()
    }

    $metadata = $document.SelectSingleNode("/*[local-name()='package']/*[local-name()='metadata']")
    if ($null -eq $metadata) {
        $script:failures.Add("${PackageId}/${Version}: package nuspec has no metadata node.")
        return
    }
    $licenseNode = $metadata.SelectSingleNode("*[local-name()='license']")
    if ($null -eq $licenseNode -or $licenseNode.GetAttribute('type') -ne 'expression') {
        $script:failures.Add("${PackageId}/${Version}: package does not declare a machine-readable SPDX license expression.")
        return
    }

    $expression = $licenseNode.InnerText.Trim()
    try {
        if (Test-LicenseExpression $expression $PackageId $AllowWixMsRl) {
            if ($AllowWixMsRl -and $PackageId.StartsWith('WixToolset.', [System.StringComparison]::OrdinalIgnoreCase) -and $expression -match '(^|\s)MS-RL(\s|$)') {
                Write-Host "ALLOW (CI-only approved WiX MS-RL): $PackageId/$Version ($expression)"
            }
            else {
                Write-Host "ALLOW: $PackageId/$Version ($expression)"
            }
        }
        else {
            $script:failures.Add("${PackageId}/${Version}: license expression is outside the allow-list: $expression")
        }
    }
    catch {
        $script:failures.Add("${PackageId}/${Version}: invalid SPDX license expression '$expression' ($($_.Exception.Message))")
    }
}

function Get-PackageDirectory {
    param(
        [Parameter(Mandatory = $true)][string[]]$PackageFolders,
        [Parameter(Mandatory = $true)][string]$RelativePath
    )

    if ([System.IO.Path]::IsPathRooted($RelativePath)) {
        throw "NuGet package path must be relative: $RelativePath"
    }

    foreach ($folder in $PackageFolders) {
        $root = [System.IO.Path]::GetFullPath($folder).TrimEnd(
            [System.IO.Path]::DirectorySeparatorChar,
            [System.IO.Path]::AltDirectorySeparatorChar)
        $candidate = [System.IO.Path]::GetFullPath((Join-Path $root $RelativePath))
        $prefix = $root + [System.IO.Path]::DirectorySeparatorChar
        if (-not $candidate.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "NuGet package path escapes its package folder: $RelativePath"
        }
        if (Test-Path -LiteralPath $candidate -PathType Container) { return $candidate }
    }

    return $null
}

$assetsPath = (Resolve-Path -LiteralPath $AssetsFile).Path
$isWixBuildAssets = $assetsPath -match '[\\/]packaging[\\/]windows[\\/]obj[\\/]project\.assets\.json$'
if ($AllowCiOnlyWixMsRl -and -not $isWixBuildAssets) {
    throw '-AllowCiOnlyWixMsRl is restricted to packaging/windows/obj/project.assets.json.'
}

$assets = Get-Content -LiteralPath $assetsPath -Raw | ConvertFrom-Json -AsHashtable
if ($null -eq $assets.packageFolders -or $null -eq $assets.libraries) {
    throw "NuGet assets file is missing packageFolders or libraries: $assetsPath"
}

$packageFolders = @($assets.packageFolders.Keys)
$failures = [System.Collections.Generic.List[string]]::new()
$checked = 0

foreach ($entry in $assets.libraries.GetEnumerator()) {
    if ($entry.Value.type -ne 'package') { continue }

    $separator = $entry.Key.LastIndexOf('/')
    if ($separator -le 0 -or $separator -eq ($entry.Key.Length - 1)) {
        $failures.Add("Invalid NuGet library identity: $($entry.Key)")
        continue
    }

    $packageId = $entry.Key.Substring(0, $separator)
    $version = $entry.Key.Substring($separator + 1)
    $packagePath = Get-PackageDirectory $packageFolders ([string]$entry.Value.path)
    if ($null -eq $packagePath) {
        $failures.Add("${packageId}/${version}: restored package directory was not found.")
        continue
    }

    $checked++
    Get-PackageLicense $packageId $version $packagePath $isWixBuildAssets
}

foreach ($specification in $AdditionalPackage) {
    $separator = $specification.LastIndexOf('/')
    if ($separator -le 0 -or $separator -eq ($specification.Length - 1)) {
        throw "Additional package must use PackageId/Version format: $specification"
    }

    $packageId = $specification.Substring(0, $separator)
    $version = $specification.Substring($separator + 1)
    $relativePath = '{0}/{1}' -f $packageId.ToLowerInvariant(), $version.ToLowerInvariant()
    $packagePath = Get-PackageDirectory $packageFolders $relativePath
    if ($null -eq $packagePath) {
        $failures.Add("${packageId}/${version}: SDK package directory was not found in the restored NuGet cache.")
        continue
    }

    $checked++
    Get-PackageLicense $packageId $version $packagePath $isWixBuildAssets
}

Write-Host "NuGet packages checked: $checked"
if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    throw "NuGet license check failed for $($failures.Count) package(s)."
}
