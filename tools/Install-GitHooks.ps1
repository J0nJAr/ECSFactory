<#
.SYNOPSIS
    tools/hooks/ 안의 git 훅을 이 저장소의 .git/hooks/에 설치한다.

.DESCRIPTION
    .git/hooks는 git이 추적하지 않는 디렉터리라 저장소에 커밋할 수 없다.
    그래서 훅 원본은 tools/hooks/에 두고(버전 관리 대상), 이 스크립트로 복사한다.
    새로 clone한 뒤 한 번 실행하면 된다.

    심볼릭 링크 대신 복사를 쓰는 이유: Windows에서 심볼릭 링크는 개발자 모드나
    관리자 권한이 필요해서 "그냥 되는" 방법이 아니다. 대신 훅을 고쳤을 때
    다시 실행해야 한다는 점을 감수한다.

.EXAMPLE
    powershell -File tools/Install-GitHooks.ps1
#>

$ErrorActionPreference = 'Stop'

$repoRoot = (git rev-parse --show-toplevel)
if (-not $repoRoot) {
    throw 'git 저장소 안에서 실행해야 한다.'
}

$sourceDir = Join-Path $repoRoot 'tools/hooks'
$targetDir = Join-Path $repoRoot '.git/hooks'

if (-not (Test-Path $sourceDir)) {
    throw "훅 원본 디렉터리가 없다: $sourceDir"
}

$installed = 0
foreach ($hook in Get-ChildItem -Path $sourceDir -File) {
    $target = Join-Path $targetDir $hook.Name
    Copy-Item -Path $hook.FullName -Destination $target -Force
    Write-Host "설치: $($hook.Name)"
    $installed++
}

Write-Host ""
Write-Host "$installed 개 훅을 설치했다. -> $targetDir"
Write-Host "훅을 수정했다면 이 스크립트를 다시 실행할 것."
