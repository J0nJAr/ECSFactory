#requires -Version 5.1
<#
.SYNOPSIS
    새 C++ 소스/헤더를 만들고, 그 파일을 해당 CMake 타깃에 등록한다.

.DESCRIPTION
    이 저장소의 CMake 소스 목록은 글롭이 아니라 명시적이다. 새 파일은 CMakeLists.txt에
    적히기 전까지 컴파일되지 않고, 그 사실이 조용히 지나가면 "왜 안 되는지 모르는" 시간을
    쓰게 된다. 이 스크립트가 그 두 단계(파일 생성 + 등록)를 한 번에 한다.

    ⚠️ 이 스크립트는 CMakeLists.txt를 **덮어쓴다.** 그래서 쓰기 직전에
    Assert-LinesPreserved 로 "원본의 모든 줄이 그대로 남아 있고, 늘어난 줄은 우리가
    넣은 것뿐"임을 검증한다. 예전에 이 파일을 통째로 비워버린 사고가 있었고, 그런 실수는
    되돌리기 전까지 알아채기 어렵다 — 검증을 코드로 박아 두는 편이 문서로 경고하는 것보다
    싸다.

.EXAMPLE
    powershell -File tools/Create-CppArtifact.ps1 -Target EngineCore   -Kind pair   -RelativePath ECS/MyThing.hpp
    powershell -File tools/Create-CppArtifact.ps1 -Target EngineRender -Kind pair   -RelativePath Render/Dx11/Foo.hpp
    powershell -File tools/Create-CppArtifact.ps1 -Target tests        -Kind source -RelativePath FooTests.cpp
#>
param(
    # ⚠️ EngineRender는 소스가 EngineCore/ 디렉터리 **안에** 있지만 별도 타깃이다
    #    (EngineCore/CMakeLists.txt의 두 번째 target_sources 블록).
    #    어느 블록에 등록되느냐가 곧 "dedicated server가 이 파일을 링크하는가"를 정하므로,
    #    타깃을 디렉터리로 추측하지 않고 호출자가 명시하게 한다.
    [Parameter(Mandatory = $true)]
    [ValidateSet("EngineCore", "EngineRender", "GameClient", "GameServer", "tests")]
    [string]$Target,

    [Parameter(Mandatory = $true)]
    [ValidateSet("pair", "source", "header")]
    [string]$Kind,

    # 타깃의 소스 루트 기준 상대 경로. 확장자는 있어도 되고 없어도 된다
    # (ECS/MyThing / ECS/MyThing.hpp / ECS/MyThing.cpp 모두 같은 쌍을 가리킨다).
    [Parameter(Mandatory = $true)]
    [string]$RelativePath
)

$ErrorActionPreference = "Stop"

# -----------------------------------------------------------------------------
# 타깃 서술표
#
# "타깃 이름 = 디렉터리 이름"이 아니다. 그 어긋남이 이 표가 존재하는 이유다.
#   - SourceRoot      : 파일이 실제로 놓이는 디렉터리
#   - CMakeDirectory  : 그 타깃을 정의한 CMakeLists.txt가 있는 디렉터리
#   - CMakeTargetName : CMake 안에서의 이름(tests 타깃의 실행 파일은 EngineCoreTests다)
#   - Style           : 등록 형태. 라이브러리는 target_sources(...), 실행 파일은 add_executable(...)
#   - IncludeStyle    : 생성된 .cpp가 짝 헤더를 어떻게 include할지
#       * "root"    — 타깃 루트가 include 디렉터리다(EngineCore가 그 디렉터리를 PUBLIC으로 건다).
#                     저장소 관례대로 "ECS/MyThing.hpp" 처럼 루트 기준으로 적는다.
#       * "sibling" — include 디렉터리가 없다. 따옴표 include는 포함하는 파일의 디렉터리부터
#                     찾으므로 옆에 있는 헤더는 파일명만 적어야 한다(tests의 "TestHarness.hpp"와 같은 형태).
# -----------------------------------------------------------------------------
$targetTable = @{
    "EngineCore"   = @{ SourceRoot = "EngineCore"; CMakeDirectory = "EngineCore"; CMakeTargetName = "EngineCore";      Style = "target_sources"; IncludeStyle = "root" }
    "EngineRender" = @{ SourceRoot = "EngineCore"; CMakeDirectory = "EngineCore"; CMakeTargetName = "EngineRender";    Style = "target_sources"; IncludeStyle = "root" }
    "GameClient"   = @{ SourceRoot = "GameClient"; CMakeDirectory = "GameClient"; CMakeTargetName = "GameClient";      Style = "add_executable"; IncludeStyle = "sibling" }
    "GameServer"   = @{ SourceRoot = "GameServer"; CMakeDirectory = "GameServer"; CMakeTargetName = "GameServer";      Style = "add_executable"; IncludeStyle = "sibling" }
    "tests"        = @{ SourceRoot = "tests";      CMakeDirectory = "tests";      CMakeTargetName = "EngineCoreTests"; Style = "add_executable"; IncludeStyle = "sibling" }
}

$spec = $targetTable[$Target]

$workspaceRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = Join-Path $workspaceRoot $spec.SourceRoot
$cmakePath = Join-Path (Join-Path $workspaceRoot $spec.CMakeDirectory) "CMakeLists.txt"

if (-not (Test-Path -LiteralPath $cmakePath)) {
    throw "타깃 '$Target'의 CMakeLists.txt를 찾지 못했다: $cmakePath"
}

# -----------------------------------------------------------------------------
# 경로 정규화
# -----------------------------------------------------------------------------
$normalizedRelativePath = $RelativePath.Trim().Replace("\", "/").TrimStart("/")
if ([string]::IsNullOrWhiteSpace($normalizedRelativePath)) {
    throw "RelativePath가 비어 있다."
}

# ⚠️ 예전에는 여기서 [System.IO.Path]::ChangeExtension($path, $null) 을 썼다. 그런데
#    PowerShell은 .NET 메서드의 string 매개변수에 $null을 넘길 때 빈 문자열로 바꿔서 전달한다.
#    ChangeExtension(path, "") 는 확장자를 "빈 확장자"로 바꾸므로 결과가 "MyThing." 이 되고,
#    거기에 ".hpp"를 붙여 MyThing..hpp 라는 파일이 만들어졌다.
#    (확장자를 붙여 부르든 안 붙이든 똑같이 발생했다 — 문서에 적혀 있던 "확장자를 빼면"이라는
#     조건은 실제보다 좁았다.)
#
#    [NullString]::Value 로 진짜 null을 넘기는 방법도 있지만, "PowerShell이 $null을 어떻게
#    마샬링하는가"라는 잘 안 보이는 지식에 다시 기대게 된다. 아래처럼 아는 확장자만 잘라내는
#    쪽이 동작이 눈에 보이고, 무엇을 확장자로 볼지도 우리가 정한다.
$knownExtensions = @(".hpp", ".h", ".hxx", ".cpp", ".cc", ".cxx")
$extension = [System.IO.Path]::GetExtension($normalizedRelativePath)
if ($extension -and ($knownExtensions -contains $extension.ToLowerInvariant())) {
    $baseRelativePath = $normalizedRelativePath.Substring(0, $normalizedRelativePath.Length - $extension.Length)
}
else {
    # 확장자가 없거나(ECS/MyThing) C++ 확장자가 아니면(ECS/My.Thing) 전체를 이름으로 본다.
    $baseRelativePath = $normalizedRelativePath
}

if ([string]::IsNullOrWhiteSpace($baseRelativePath) -or $baseRelativePath.EndsWith("/") -or $baseRelativePath.EndsWith(".")) {
    throw "RelativePath에 파일 이름이 없다: '$RelativePath'"
}

$headerRelativePath = "$baseRelativePath.hpp"
$sourceRelativePath = "$baseRelativePath.cpp"
$headerAbsolutePath = Join-Path $sourceRoot ($headerRelativePath.Replace("/", [System.IO.Path]::DirectorySeparatorChar))
$sourceAbsolutePath = Join-Path $sourceRoot ($sourceRelativePath.Replace("/", [System.IO.Path]::DirectorySeparatorChar))

$wantsHeader = $Kind -in @("pair", "header")
$wantsSource = $Kind -in @("pair", "source")

# -----------------------------------------------------------------------------
# 파일 생성
# -----------------------------------------------------------------------------
function Ensure-ParentDirectory {
    param([string]$Path)

    $parent = Split-Path -Parent $Path
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
}

# 생성 파일은 BOM 없는 UTF-8 + CRLF로 쓴다.
# BOM 없음: 저장소의 다른 .cpp/.hpp가 전부 그렇고, g++/clangd 모두 BOM 없는 UTF-8을 읽는다.
#           (Set-Content -Encoding utf8 은 PowerShell 5.1에서 BOM을 붙인다 — 그래서 안 쓴다.)
# CRLF   : 저장소의 기존 파일이 CRLF다. 새 파일만 LF면 diff가 지저분해진다.
function Write-TextFile {
    param([string]$Path, [string[]]$Lines, [bool]$WithBom = $false)

    $encoding = New-Object System.Text.UTF8Encoding($WithBom)
    [System.IO.File]::WriteAllText($Path, (($Lines -join "`r`n") + "`r`n"), $encoding)
}

function To-IncludePath {
    param([string]$Path)

    if ($spec.IncludeStyle -eq "root") {
        return $Path
    }

    # "sibling" — 생성된 .cpp 바로 옆에 헤더가 있으므로 파일명만 적는다.
    return [System.IO.Path]::GetFileName($Path)
}

if ($wantsHeader) {
    Ensure-ParentDirectory -Path $headerAbsolutePath
    if (-not (Test-Path -LiteralPath $headerAbsolutePath)) {
        $includePath = To-IncludePath -Path $headerRelativePath
        Write-TextFile -Path $headerAbsolutePath -Lines @(
            "#pragma once",
            "",
            "// TODO: $includePath 가 공개할 선언을 여기에 채운다."
        )
    }
}

if ($wantsSource) {
    Ensure-ParentDirectory -Path $sourceAbsolutePath
    if (-not (Test-Path -LiteralPath $sourceAbsolutePath)) {
        if ($Kind -eq "pair") {
            $includePath = To-IncludePath -Path $headerRelativePath
            Write-TextFile -Path $sourceAbsolutePath -Lines @(
                "#include `"$includePath`"",
                "",
                "// TODO: $includePath 의 선언을 여기에 구현한다."
            )
        }
        else {
            Write-TextFile -Path $sourceAbsolutePath -Lines @(
                "// TODO: $sourceRelativePath 의 구현을 여기에 채운다."
            )
        }
    }
}

# -----------------------------------------------------------------------------
# CMakeLists.txt 편집
#
# 핵심 원칙: **편집 범위를 해당 CMake 호출 안으로 한정한다.**
# 예전 구현은 파일 전체에서 "    PRIVATE" 같은 문자열을 찾아 바꿨다. EngineCore/CMakeLists.txt에는
# target_sources 블록이 둘(EngineCore, EngineRender) 있어서, EngineCore에 .cpp 하나를 넣으면
# **EngineRender에도 같이 등록**됐다 — 정확히 "서버가 링크하면 안 되는 것"과 "서버가 링크해야 하는 것"의
# 경계를 조용히 무너뜨리는 종류의 버그다. 그래서 먼저 호출의 괄호 범위를 찾고 그 안에서만 움직인다.
# -----------------------------------------------------------------------------

# 한 CMake 호출 `Command(Name ... )` 의 여는 괄호와 닫는 괄호 위치를 찾는다.
# 괄호를 세어서 짝을 맞춘다 — 이 저장소의 CMake 인자에는 괄호가 들어간 문자열이 없다.
function Get-CmakeCallSpan {
    param([string]$Content, [string]$Command, [string]$Name)

    $pattern = [regex]::Escape($Command) + '\s*\(\s*' + [regex]::Escape($Name) + '(?![A-Za-z0-9_])'
    $match = [regex]::Match($Content, $pattern)
    if (-not $match.Success) {
        throw "$cmakePath 에서 '$Command($Name ...)' 호출을 찾지 못했다."
    }

    $open = $Content.IndexOf("(", $match.Index)
    $depth = 0
    for ($i = $open; $i -lt $Content.Length; $i++) {
        $c = $Content[$i]
        if ($c -eq "(") { $depth++ }
        elseif ($c -eq ")") {
            $depth--
            if ($depth -eq 0) {
                return @{ Start = $match.Index; Open = $open; Close = $i }
            }
        }
    }

    throw "$cmakePath 의 '$Command($Name ...)' 호출에 닫는 괄호가 없다."
}

# 각 줄의 시작 오프셋·길이·본문. 삽입 위치를 문자 오프셋으로 계산하려면 이게 필요하다.
function Get-LineTable {
    param([string]$Content)

    $lines = New-Object System.Collections.ArrayList
    foreach ($m in [regex]::Matches($Content, "(?m)^[^\r\n]*")) {
        [void]$lines.Add(@{ Index = $m.Index; Length = $m.Length; Text = $m.Value })
    }
    return $lines
}

function Get-IndentWidth {
    param([string]$Text)

    $m = [regex]::Match($Text, '^[ \t]*')
    return $m.Length
}

# 앵커 줄(예: "    PRIVATE") 아래에 붙어 있는, 더 깊게 들여쓴 줄들이 목록이다.
# 그 목록에 $Entry 한 줄을 넣은 새 내용을 돌려준다. 이미 있으면 원본 그대로 돌려준다.
#
# 넣는 위치: 목록이 이미 오름차순으로 정렬되어 있으면 정렬 위치에, 아니면 맨 끝에.
# (저장소의 소스 목록은 대체로 정렬되어 있다. 정렬을 지켜 주면 diff가 작고 읽기 좋다.
#  정렬이 아닌 목록까지 억지로 정렬 위치에 끼우면 사람이 의도한 배치를 망가뜨리므로 그때는 끝에 붙인다.)
function Add-EntryToIndentedList {
    param(
        [string]$Content,
        [hashtable]$Span,
        [int]$AnchorLineIndex,   # 앵커 줄의 시작 오프셋
        [string]$Entry,
        [string]$Eol
    )

    $lines = Get-LineTable -Content $Content
    $anchorPosition = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i].Index -eq $AnchorLineIndex) { $anchorPosition = $i; break }
    }
    if ($anchorPosition -lt 0) {
        throw "앵커 줄을 줄 목록에서 찾지 못했다(내부 오류)."
    }

    $anchorIndent = Get-IndentWidth -Text $lines[$anchorPosition].Text

    # 앵커 아래로 "더 깊게 들여쓴 비어 있지 않은 줄"을 모은다.
    $items = New-Object System.Collections.ArrayList
    for ($i = $anchorPosition + 1; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line.Index -ge $Span.Close) { break }
        if ([string]::IsNullOrWhiteSpace($line.Text)) { break }
        if ((Get-IndentWidth -Text $line.Text) -le $anchorIndent) { break }
        [void]$items.Add($line)
    }

    # 이미 등록되어 있으면 그대로 둔다(같은 스크립트를 두 번 돌려도 안전해야 한다).
    foreach ($item in $items) {
        if ($item.Text.Trim() -eq $Entry) { return $Content }
    }

    if ($items.Count -eq 0) {
        $indent = " " * ($anchorIndent + 4)
        $insertAt = $lines[$anchorPosition].Index + $lines[$anchorPosition].Length
        return $Content.Insert($insertAt, "$Eol$indent$Entry")
    }

    $indent = " " * (Get-IndentWidth -Text $items[0].Text)

    $isSorted = $true
    for ($i = 1; $i -lt $items.Count; $i++) {
        if ([string]::CompareOrdinal($items[$i - 1].Text.Trim(), $items[$i].Text.Trim()) -gt 0) {
            $isSorted = $false
            break
        }
    }

    if ($isSorted) {
        foreach ($item in $items) {
            if ([string]::CompareOrdinal($item.Text.Trim(), $Entry) -gt 0) {
                # 이 줄 **앞에** 끼운다.
                return $Content.Insert($item.Index, "$indent$Entry$Eol")
            }
        }
    }

    $last = $items[$items.Count - 1]
    return $Content.Insert($last.Index + $last.Length, "$Eol$indent$Entry")
}

# add_executable(GameClient main.cpp) 처럼 한 줄로 적힌 호출은 먼저 여러 줄로 편다.
# 그래야 위의 "앵커 + 들여쓴 목록" 규칙을 똑같이 쓸 수 있다.
function Expand-SingleLineCall {
    param([string]$Content, [hashtable]$Span, [string]$Name, [string]$Command, [string]$Eol)

    $callText = $Content.Substring($Span.Start, $Span.Close - $Span.Start + 1)
    if ($callText.Contains("`n")) {
        return $Content   # 이미 여러 줄이다.
    }

    $inner = $Content.Substring($Span.Open + 1, $Span.Close - $Span.Open - 1).Trim()
    $arguments = [regex]::Split($inner, '\s+') | Where-Object { $_ -ne "" }
    if ($arguments.Count -lt 1 -or $arguments[0] -ne $Name) {
        throw "'$Command($Name ...)' 를 여러 줄로 펴지 못했다: $callText"
    }

    $rest = if ($arguments.Count -gt 1) { $arguments[1..($arguments.Count - 1)] } else { @() }
    $expanded = "$Command($Name$Eol"
    foreach ($argument in $rest) { $expanded += "    $argument$Eol" }
    $expanded += ")"

    return $Content.Remove($Span.Start, $Span.Close - $Span.Start + 1).Insert($Span.Start, $expanded)
}

# 스팬 안에서 `<Word>` 하나만 있는 줄을 찾아 그 시작 오프셋을 돌려준다.
#
# ⚠️ 끝에 `\r?` 가 붙어 있는 게 중요하다. .NET 정규식의 multiline `$` 는 `\n` **앞**에서만
#    맞는다 — `\r` 를 건너뛰어 주지 않는다. 이 저장소의 CMakeLists는 CRLF라서 `[ \t]*$` 만
#    쓰면 "    PRIVATE\r\n" 에 절대 맞지 않는다(예전 구현의 중복 등록 검사가 늘 실패하던 이유다).
function Find-AnchorLineIndex {
    param([string]$Content, [hashtable]$Span, [string]$Word)

    $regex = [regex]::new("(?m)^[ \t]*" + [regex]::Escape($Word) + "[ \t]*\r?$")
    $match = $regex.Match($Content, $Span.Open)
    if (-not $match.Success -or $match.Index -ge $Span.Close) {
        throw "$cmakePath 의 '$($spec.CMakeTargetName)' 블록 안에서 '$Word' 줄을 찾지 못했다."
    }
    return $match.Index
}

# ⚠️ 여기가 이 스크립트에서 제일 중요한 안전장치다.
#    "원본의 모든 줄이 순서대로 그대로 남아 있고, 늘어난 줄 수는 우리가 넣은 만큼뿐"을 확인한다.
#    이 두 조건이 동시에 참이면 결과는 정의상 "원본 + 삽입한 줄들"이다 — 내용이 날아가는 사고를
#    쓰기 전에 잡을 수 있다.
# 한 줄짜리 호출을 여러 줄로 펴는 것은 "줄을 지우고 다시 쓰는" 편집이라
# Assert-LinesPreserved 의 부분수열 검사를 통과할 수 없다(원본 줄이 사라지는 게 맞다).
# 그래서 이 단계만 별도 기준으로 검증한다: **호출 밖은 한 글자도 안 바뀌고, 호출 안의
# 인자 나열은 순서까지 그대로**여야 한다. 그 둘이 참이면 바뀐 것은 공백뿐이다.
function Assert-CallReformatSafe {
    param([string]$Before, [string]$After, [hashtable]$BeforeSpan, [hashtable]$AfterSpan)

    if ($Before.Substring(0, $BeforeSpan.Start) -ne $After.Substring(0, $AfterSpan.Start)) {
        throw "호출을 여러 줄로 펴는 중 앞부분이 바뀌었다. 쓰지 않고 중단한다."
    }
    if ($Before.Substring($BeforeSpan.Close + 1) -ne $After.Substring($AfterSpan.Close + 1)) {
        throw "호출을 여러 줄로 펴는 중 뒷부분이 바뀌었다. 쓰지 않고 중단한다."
    }

    $beforeTokens = [regex]::Split($Before.Substring($BeforeSpan.Open + 1, $BeforeSpan.Close - $BeforeSpan.Open - 1).Trim(), '\s+') | Where-Object { $_ -ne "" }
    $afterTokens = [regex]::Split($After.Substring($AfterSpan.Open + 1, $AfterSpan.Close - $AfterSpan.Open - 1).Trim(), '\s+') | Where-Object { $_ -ne "" }
    if (($beforeTokens -join "`n") -ne ($afterTokens -join "`n")) {
        throw "호출을 여러 줄로 펴는 중 인자 목록이 바뀌었다. 쓰지 않고 중단한다."
    }
}

function Assert-LinesPreserved {
    param([string]$Original, [string]$Updated, [int]$AddedLineCount)

    $originalLines = $Original -split "`r`n|`n"
    $updatedLines = $Updated -split "`r`n|`n"

    if ($updatedLines.Count -ne $originalLines.Count + $AddedLineCount) {
        throw "CMakeLists 편집 결과의 줄 수가 예상과 다르다(원본 $($originalLines.Count) + $AddedLineCount ≠ $($updatedLines.Count)). 쓰지 않고 중단한다."
    }

    # 원본이 결과의 부분수열인지 확인한다(탐욕 매칭으로 충분하다).
    $cursor = 0
    foreach ($line in $originalLines) {
        while ($cursor -lt $updatedLines.Count -and $updatedLines[$cursor] -ne $line) { $cursor++ }
        if ($cursor -ge $updatedLines.Count) {
            throw "CMakeLists 편집이 원본 줄을 잃어버렸다: '$line'. 쓰지 않고 중단한다."
        }
        $cursor++
    }
}

$originalBytes = [System.IO.File]::ReadAllBytes($cmakePath)
$hadBom = ($originalBytes.Length -ge 3 -and $originalBytes[0] -eq 0xEF -and $originalBytes[1] -eq 0xBB -and $originalBytes[2] -eq 0xBF)
$originalContent = [System.IO.File]::ReadAllText($cmakePath)

# 파일이 쓰던 줄바꿈을 그대로 따른다. 섞어 쓰면 diff 전체가 바뀐 것처럼 보인다.
$eol = if ($originalContent.Contains("`r`n")) { "`r`n" } else { "`n" }

$cmakeContent = $originalContent

# 줄 보존 검사의 기준점. 한 줄짜리 호출을 여러 줄로 펴는 단계는 별도 기준으로 검증한 뒤
# 이 기준점을 그 결과로 옮긴다 — 그래야 "줄 하나가 여러 줄이 되는" 정당한 재포맷과
# "내용이 날아가는" 사고를 구분할 수 있다.
$preservationBaseline = $originalContent
$addedLineCount = 0
$registeredHeader = $false
$registeredSource = $false

if ($spec.Style -eq "target_sources") {
    if ($wantsSource) {
        $span = Get-CmakeCallSpan -Content $cmakeContent -Command "target_sources" -Name $spec.CMakeTargetName
        $anchor = Find-AnchorLineIndex -Content $cmakeContent -Span $span -Word "PRIVATE"
        $before = $cmakeContent
        $cmakeContent = Add-EntryToIndentedList -Content $cmakeContent -Span $span -AnchorLineIndex $anchor -Entry $sourceRelativePath -Eol $eol
        if ($cmakeContent -ne $before) { $addedLineCount++; $registeredSource = $true }
    }

    if ($wantsHeader) {
        # FILE_SET HEADERS 의 FILES 목록. 스팬을 다시 구하는 이유: 위에서 소스를 끼워 넣었다면
        # 오프셋이 전부 밀렸다. 낡은 스팬을 재사용하는 건 이런 코드에서 가장 흔한 실수다.
        $span = Get-CmakeCallSpan -Content $cmakeContent -Command "target_sources" -Name $spec.CMakeTargetName
        $anchor = Find-AnchorLineIndex -Content $cmakeContent -Span $span -Word "FILES"
        $before = $cmakeContent
        $cmakeContent = Add-EntryToIndentedList -Content $cmakeContent -Span $span -AnchorLineIndex $anchor -Entry $headerRelativePath -Eol $eol
        if ($cmakeContent -ne $before) { $addedLineCount++; $registeredHeader = $true }
    }
}
else {
    if ($wantsSource) {
        $span = Get-CmakeCallSpan -Content $cmakeContent -Command "add_executable" -Name $spec.CMakeTargetName
        $expanded = Expand-SingleLineCall -Content $cmakeContent -Span $span -Name $spec.CMakeTargetName -Command "add_executable" -Eol $eol
        if ($expanded -ne $cmakeContent) {
            $expandedSpan = Get-CmakeCallSpan -Content $expanded -Command "add_executable" -Name $spec.CMakeTargetName
            Assert-CallReformatSafe -Before $cmakeContent -After $expanded -BeforeSpan $span -AfterSpan $expandedSpan
            $cmakeContent = $expanded
            $span = $expandedSpan
            # 재포맷을 검증했으니 여기서부터를 보존 검사의 기준으로 삼는다.
            $preservationBaseline = $cmakeContent
            $addedLineCount = 0
        }

        # 실행 파일 타깃에는 PRIVATE 같은 키워드가 없다. add_executable(<Name> 줄 자체가 앵커다.
        $lines = Get-LineTable -Content $cmakeContent
        $anchor = -1
        foreach ($line in $lines) {
            if ($line.Index -le $span.Start -and $span.Start -lt $line.Index + $line.Length) { $anchor = $line.Index; break }
        }
        if ($anchor -lt 0) { throw "add_executable 줄을 찾지 못했다(내부 오류)." }

        $before = $cmakeContent
        $cmakeContent = Add-EntryToIndentedList -Content $cmakeContent -Span $span -AnchorLineIndex $anchor -Entry $sourceRelativePath -Eol $eol
        if ($cmakeContent -ne $before) { $addedLineCount++; $registeredSource = $true }
    }
}

Assert-LinesPreserved -Original $preservationBaseline -Updated $cmakeContent -AddedLineCount $addedLineCount

if ($cmakeContent -ne $originalContent) {
    $encoding = New-Object System.Text.UTF8Encoding($hadBom)
    [System.IO.File]::WriteAllText($cmakePath, $cmakeContent, $encoding)
}

# -----------------------------------------------------------------------------
# 결과 보고
# -----------------------------------------------------------------------------
Write-Output "타깃 ${Target} (CMake 타깃: $($spec.CMakeTargetName)) 처리 완료:"
if ($wantsHeader) {
    Write-Output "  $($spec.SourceRoot)/$headerRelativePath"
}
if ($wantsSource) {
    Write-Output "  $($spec.SourceRoot)/$sourceRelativePath"
}

if ($cmakeContent -ne $originalContent) {
    $registered = @()
    if ($registeredSource) { $registered += $sourceRelativePath }
    if ($registeredHeader) { $registered += $headerRelativePath }
    Write-Output "  $($spec.CMakeDirectory)/CMakeLists.txt 에 등록: $($registered -join ', ')"
}
else {
    Write-Output "  CMakeLists.txt 변경 없음(이미 등록되어 있다)"
}

# 실행 파일 타깃은 헤더를 CMake에 따로 적지 않는다(FILE_SET HEADERS가 없다).
# 조용히 넘어가면 "등록된 줄 알았는데 아니었다"가 되므로 말해 준다.
if ($wantsHeader -and $spec.Style -eq "add_executable") {
    Write-Warning "'$Target' 은(는) 실행 파일 타깃이라 헤더를 CMakeLists.txt에 등록하지 않는다. 파일만 만들었다(빌드에는 문제없다)."
}
