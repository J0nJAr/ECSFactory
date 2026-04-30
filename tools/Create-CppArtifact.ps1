param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("EngineCore", "GameClient", "GameServer", "tests")]
    [string]$Target,

    [Parameter(Mandatory = $true)]
    [ValidateSet("pair", "source", "header")]
    [string]$Kind,

    [Parameter(Mandatory = $true)]
    [string]$RelativePath
)

$ErrorActionPreference = "Stop"

$workspaceRoot = Split-Path -Parent $PSScriptRoot
$targetRoot = Join-Path $workspaceRoot $Target
$cmakePath = Join-Path $targetRoot "CMakeLists.txt"

if (-not (Test-Path -LiteralPath $cmakePath)) {
    throw "CMakeLists.txt not found for target '$Target'."
}

$normalizedRelativePath = $RelativePath.Trim().Replace("\", "/").TrimStart("/")
if ([string]::IsNullOrWhiteSpace($normalizedRelativePath)) {
    throw "RelativePath must not be empty."
}

$baseRelativePath = [System.IO.Path]::ChangeExtension($normalizedRelativePath, $null)
if ([string]::IsNullOrWhiteSpace($baseRelativePath)) {
    throw "RelativePath must include a file name."
}

$headerRelativePath = "$baseRelativePath.hpp"
$sourceRelativePath = "$baseRelativePath.cpp"
$headerAbsolutePath = Join-Path $targetRoot ($headerRelativePath.Replace("/", [System.IO.Path]::DirectorySeparatorChar))
$sourceAbsolutePath = Join-Path $targetRoot ($sourceRelativePath.Replace("/", [System.IO.Path]::DirectorySeparatorChar))

function Ensure-ParentDirectory {
    param([string]$Path)

    $parent = Split-Path -Parent $Path
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
}

function To-IncludePath {
    param([string]$Path)

    if ($Target -eq "EngineCore") {
        return $Path
    }

    return "$Target/$Path"
}

function New-HeaderContent {
    param([string]$Path)

    $includePath = To-IncludePath -Path $Path
    return @(
        "#pragma once",
        "",
        "// TODO: declare symbols for $includePath."
    )
}

function New-SourceContent {
    param([string]$HeaderPath)

    if ([string]::IsNullOrWhiteSpace($HeaderPath)) {
        return @(
            "// TODO: implement symbols for $sourceRelativePath."
        )
    }

    $includePath = To-IncludePath -Path $HeaderPath
    return @(
        "#include `"$includePath`"",
        "",
        "// TODO: implement symbols declared in $includePath."
    )
}

function Add-UniqueLineToIndentedBlock {
    param(
        [string]$Content,
        [string]$Anchor,
        [string]$LineToAdd
    )

    if ($Content -match "(?m)^[ \t]*" + [regex]::Escape($LineToAdd) + "$") {
        return $Content
    }

    $anchorPattern = [regex]::Escape($Anchor)
    $replacement = "$Anchor`r`n        $LineToAdd"
    if ($Content -notmatch $anchorPattern) {
        throw "Could not find anchor '$Anchor' in $cmakePath."
    }

    return [regex]::Replace($Content, $anchorPattern, [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $replacement }, 1)
}

function Add-UniqueLineBeforeFirstMatch {
    param(
        [string]$Content,
        [string]$Pattern,
        [string]$LineToAdd,
        [string]$Indent
    )

    if ($Content -match "(?m)^[ \t]*" + [regex]::Escape($LineToAdd) + "$") {
        return $Content
    }

    $regex = [regex]::new($Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)
    $match = $regex.Match($Content)
    if (-not $match.Success) {
        throw "Could not find insertion point '$Pattern' in $cmakePath."
    }

    $insertText = "$Indent$LineToAdd`r`n"
    return $Content.Insert($match.Index, $insertText)
}

function Add-SourceToAddExecutable {
    param(
        [string]$Content,
        [string]$ExecutableName,
        [string]$LineToAdd
    )

    if ($Content -match "(?m)^[ \t]*" + [regex]::Escape($LineToAdd) + "$") {
        return $Content
    }

    $singleLinePattern = "add_executable\(" + [regex]::Escape($ExecutableName) + "\s+([^\r\n\)]+)\)"
    $singleLineMatch = [regex]::Match($Content, $singleLinePattern)
    if ($singleLineMatch.Success) {
        $existing = $singleLineMatch.Groups[1].Value.Trim()
        $replacement = "add_executable($ExecutableName`r`n    $existing`r`n    $LineToAdd`r`n)"
        return $Content.Remove($singleLineMatch.Index, $singleLineMatch.Length).Insert($singleLineMatch.Index, $replacement)
    }

    return Add-UniqueLineBeforeFirstMatch -Content $Content -Pattern "(?m)^\s*\)" -LineToAdd $LineToAdd -Indent "    "
}

if ($Kind -in @("pair", "header")) {
    Ensure-ParentDirectory -Path $headerAbsolutePath
    if (-not (Test-Path -LiteralPath $headerAbsolutePath)) {
        Set-Content -LiteralPath $headerAbsolutePath -Value (New-HeaderContent -Path $headerRelativePath) -Encoding utf8
    }
}

if ($Kind -in @("pair", "source")) {
    Ensure-ParentDirectory -Path $sourceAbsolutePath
    if (-not (Test-Path -LiteralPath $sourceAbsolutePath)) {
        $headerForInclude = if ($Kind -eq "pair") { $headerRelativePath } else { $null }
        Set-Content -LiteralPath $sourceAbsolutePath -Value (New-SourceContent -HeaderPath $headerForInclude) -Encoding utf8
    }
}

$cmakeContent = Get-Content -LiteralPath $cmakePath -Raw

switch ($Target) {
    "EngineCore" {
        if ($Kind -in @("pair", "source")) {
            $cmakeContent = Add-UniqueLineToIndentedBlock -Content $cmakeContent -Anchor "    PRIVATE" -LineToAdd $sourceRelativePath
        }

        if ($Kind -in @("pair", "header")) {
            $cmakeContent = Add-UniqueLineBeforeFirstMatch -Content $cmakeContent -Pattern "(?m)^\\s*\\)" -LineToAdd $headerRelativePath -Indent "            "
        }
    }
    "GameClient" {
        if ($Kind -in @("pair", "source")) {
            $cmakeContent = Add-SourceToAddExecutable -Content $cmakeContent -ExecutableName "GameClient" -LineToAdd $sourceRelativePath
        }
    }
    "GameServer" {
        if ($Kind -in @("pair", "source")) {
            $cmakeContent = Add-SourceToAddExecutable -Content $cmakeContent -ExecutableName "GameServer" -LineToAdd $sourceRelativePath
        }
    }
    "tests" {
        if ($Kind -in @("pair", "source")) {
            $cmakeContent = Add-UniqueLineBeforeFirstMatch -Content $cmakeContent -Pattern "(?m)^\\s*\\)" -LineToAdd $sourceRelativePath -Indent "    "
        }
    }
}

Set-Content -LiteralPath $cmakePath -Value $cmakeContent -Encoding utf8

Write-Output "Created/updated artifacts for ${Target}:"
if ($Kind -in @("pair", "header")) {
    Write-Output "  $headerRelativePath"
}
if ($Kind -in @("pair", "source")) {
    Write-Output "  $sourceRelativePath"
}
Write-Output "  CMakeLists.txt updated"
