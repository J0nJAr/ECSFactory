# 빌드 규칙은 CMake 프리셋이 소유한다. IDE/런처/터미널은 이 진입점을 공유한다.
[CmdletBinding()]
param(
    [ValidateSet('Configure', 'Build', 'Test')][string]$Action = 'Build',
    [ValidateSet('default', 'x64-debug', 'x64-release', 'msvc-debug', 'msvc-release')]
    [string]$Preset = 'default'
)
$ErrorActionPreference = 'Stop'
# 네이티브 빌드 도구와 리디렉션된 로그도 같은 UTF-8 계약을 사용한다.
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
$repoRoot = Split-Path $PSScriptRoot -Parent
try {
    Set-Location -LiteralPath $repoRoot
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw 'CMake 3.23 이상을 설치하고 PATH에 추가하세요.'
    }
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if ($Action -ne 'Configure') {
        & cmake --build --preset $Preset --parallel
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    if ($Action -eq 'Test') {
        & ctest --preset $Preset
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    exit 0
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
