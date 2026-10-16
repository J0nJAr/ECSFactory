# Windows PowerShell 5.1 / Windows Forms. 작업 프로세스를 기다리느라 UI를 막지 않는다.
[CmdletBinding()]
param([switch]$SmokeTest)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()
$script:repoRoot = Split-Path $PSScriptRoot -Parent
$script:runRoot = Join-Path $repoRoot ('out/launcher/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
[void][IO.Directory]::CreateDirectory($runRoot)
$script:children = [Collections.Generic.List[object]]::new()
$script:worker = $null
$script:sequence = 0
$script:pending = $null
$script:sessionDir = $null
$script:busyButtons = [Collections.Generic.List[object]]::new()

function Show-Failure($message) {
    $status.Text = '실패: ' + $message
    [void][Windows.Forms.MessageBox]::Show($form, [string]$message, 'ECSFactory', 'OK', 'Error')
}

# 고정된 프리셋/동작 외에 사용자 문자열을 명령으로 평가하지 않는다.
function Start-Build([string]$action, [string]$after = '') {
    if ($script:worker) { return }
    if (@($script:children | Where-Object { -not $_.Process.HasExited }).Count -gt 0) {
        throw '실행 중인 게임을 먼저 [실행 모두 종료]로 닫아주세요.'
    }
    $script:sequence++
    $script:stdout = Join-Path $runRoot "build-$sequence.log"
    $script:stderr = Join-Path $runRoot "build-$sequence.err.log"
    $script:pending = $after
    $script:activePreset = [string]$preset.SelectedItem
    $script:worker = Start-Process powershell.exe -WindowStyle Hidden -PassThru -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
        ('"' + (Join-Path $PSScriptRoot 'Invoke-Dev.ps1') + '"'),
        '-Action', $action, '-Preset', $activePreset
    ) -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    # PowerShell 5.1의 Start-Process 객체는 종료 전에 핸들을 열어 두어야
    # 프로세스가 빨리 끝나도 ExitCode를 잃지 않는다.
    $null = $script:worker.Handle
    $preset.Enabled = $false
    foreach ($button in $busyButtons) { $button.Enabled = $false }
    $status.Text = "$activePreset · $action 진행 중"
    $output.Clear()
}

function Stop-Games {
    $script:waitingServer = $false
    # 보관한 Process 인스턴스만 종료한다. 이름으로 다른 실행 세션을 찾지 않는다.
    foreach ($child in $script:children) {
        if (-not $child.Process.HasExited) { $child.Process.Kill() }
        $child.Process.Dispose()
    }
    $script:children.Clear()
    $status.Text = '이 런처에서 실행한 게임을 종료했습니다.'
}

function Start-Game([string]$name, [string[]]$arguments, [string]$label) {
    $directory = Join-Path $script:sessionDir $label
    [void][IO.Directory]::CreateDirectory($directory)
    $exe = Join-Path $repoRoot "out/build/$script:activePreset/bin/$name.exe"
    if (-not (Test-Path -LiteralPath $exe)) { throw "실행 파일이 없습니다: $exe" }
    # MinGW 런타임 DLL 검색 경로는 자식 프로세스에만 전달한다.
    $oldPath = $env:PATH
    try {
        if ($script:activePreset -notlike 'msvc-*') { $env:PATH = "C:\msys64\ucrt64\bin;$oldPath" }
        $options = @{
            FilePath = $exe; WorkingDirectory = $directory; PassThru = $true; WindowStyle = 'Hidden'
            RedirectStandardOutput = (Join-Path $directory 'stdout.log')
            RedirectStandardError = (Join-Path $directory 'stderr.log')
        }
        if ($arguments.Count -gt 0) { $options.ArgumentList = $arguments }
        $process = Start-Process @options
        $null = $process.Handle
        $script:children.Add([pscustomobject]@{ Process = $process; Label = $label; Reported = $false })
    } finally { $env:PATH = $oldPath }
}

function Start-Session([string]$mode) {
    $script:sequence++
    $script:sessionDir = Join-Path $runRoot "session-$sequence"
    [void][IO.Directory]::CreateDirectory($sessionDir)
    try {
        if ($mode -eq 'Single') {
            Start-Game 'GameClient' @('--single') 'client-1'
            $status.Text = '싱글플레이 실행 중'
        } else {
            # 다른 서버가 점유한 포트에 실수로 접속하지 않도록 시작 전에 검사한다.
            $probe = [Net.Sockets.Socket]::new([Net.Sockets.AddressFamily]::InterNetwork,
                [Net.Sockets.SocketType]::Dgram, [Net.Sockets.ProtocolType]::Udp)
            try {
                $probe.ExclusiveAddressUse = $true
                $probe.Bind([Net.IPEndPoint]::new([Net.IPAddress]::Any, 6780))
            } finally { $probe.Dispose() }
            Start-Game 'GameServer' @() 'server'
            $script:serverDeadline = [DateTime]::UtcNow.AddSeconds(10)
            $script:waitingServer = $true
            $status.Text = '서버 시작 대기 중'
        }
    } catch { Stop-Games; throw }
}

function Open-Editor([string]$editor) {
    if ($editor -eq 'Code') {
        $command = Get-Command code.cmd -ErrorAction SilentlyContinue
        if (-not $command) { throw 'VS Code를 설치하고 code 명령을 PATH에 추가하세요.' }
        $codeExe = Join-Path (Split-Path (Split-Path $command.Source -Parent) -Parent) 'Code.exe'
        if (-not (Test-Path -LiteralPath $codeExe)) { throw "VS Code 실행 파일을 찾지 못했습니다: $codeExe" }
        Start-Process -FilePath $codeExe -ArgumentList ('"' + $repoRoot + '"') -WindowStyle Normal
        $status.Text = 'VS Code에서 저장소를 열었습니다.'
    } else {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio 2022의 C++ 데스크톱 개발 워크로드를 설치하세요.' }
        $installation = & $vswhere -latest -version '[17.0,18.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not $installation) { throw 'C++ 개발 도구가 있는 Visual Studio 2022를 찾지 못했습니다.' }
        $solution = Join-Path $repoRoot "out/build/$script:activePreset/ECSFactory.sln"
        Start-Process -FilePath (Join-Path $installation 'Common7/IDE/devenv.exe') -ArgumentList ('"' + $solution + '"') -WindowStyle Normal
    }
}

$form = [Windows.Forms.Form]::new()
$form.Text = 'ECSFactory 개발 런처'
$form.Size = [Drawing.Size]::new(850, 650)
$form.MinimumSize = [Drawing.Size]::new(740, 570)
$form.StartPosition = 'CenterScreen'
$form.Font = [Drawing.Font]::new('Malgun Gothic', 10)
$layout = [Windows.Forms.TableLayoutPanel]::new()
$layout.Dock = 'Fill'
$layout.Padding = [Windows.Forms.Padding]::new(18)
$layout.ColumnCount = 1
$layout.RowCount = 6
foreach ($height in @(42, 42, 46, 32)) {
    [void]$layout.RowStyles.Add([Windows.Forms.RowStyle]::new('Absolute', $height))
}
[void]$layout.RowStyles.Add([Windows.Forms.RowStyle]::new('Percent', 100))
[void]$layout.RowStyles.Add([Windows.Forms.RowStyle]::new('Absolute', 34))
$form.Controls.Add($layout)
$heading = [Windows.Forms.Label]::new()
$heading.Text = 'ECSFactory  /  빌드 · 테스트 · 플레이'
$heading.Font = [Drawing.Font]::new('Malgun Gothic', 15, [Drawing.FontStyle]::Bold)
$heading.AutoSize = $true
$layout.Controls.Add($heading, 0, 0)
$configRow = [Windows.Forms.FlowLayoutPanel]::new(); $configRow.Dock = 'Fill'
$preset = [Windows.Forms.ComboBox]::new()
$preset.DropDownStyle = 'DropDownList'; $preset.Width = 180
$preset.Items.AddRange(@('default', 'x64-release', 'msvc-debug', 'msvc-release'))
$preset.SelectedIndex = 0
$configRow.Controls.Add($preset)
$layout.Controls.Add($configRow, 0, 1)
$playRow = [Windows.Forms.FlowLayoutPanel]::new(); $playRow.Dock = 'Fill'
$layout.Controls.Add($playRow, 0, 2)

function Add-Button($row, [string]$text, [scriptblock]$handler, [bool]$busy = $false) {
    $button = [Windows.Forms.Button]::new()
    $button.Text = $text; $button.AutoSize = $true; $button.Height = 32
    $button.Add_Click({ try { & $handler } catch { Show-Failure $_.Exception.Message } }.GetNewClosure())
    $row.Controls.Add($button)
    if ($busy) { $script:busyButtons.Add($button) }
}
Add-Button $configRow '빌드' { Start-Build 'Build' } $true
Add-Button $configRow '전체 테스트' { Start-Build 'Test' } $true
Add-Button $configRow 'VS Code' { Open-Editor 'Code' }
Add-Button $configRow 'Visual Studio' {
    if ([string]$preset.SelectedItem -notlike 'msvc-*') { $preset.SelectedItem = 'msvc-debug' }
    Start-Build 'Configure' 'VisualStudio'
} $true
Add-Button $playRow '싱글플레이' { Start-Build 'Build' 'Single' } $true
Add-Button $playRow '서버 + 클라 2개' { Start-Build 'Build' 'Multi' } $true
Add-Button $playRow '실행 모두 종료' { Stop-Games }
Add-Button $playRow '로그 폴더' { Start-Process explorer.exe -ArgumentList ('"' + $runRoot + '"') -WindowStyle Normal }
$hint = [Windows.Forms.Label]::new(); $hint.AutoSize = $true
$hint.Text = '플레이는 빌드 성공 후 시작합니다. 런처를 닫으면 실행한 게임도 종료됩니다.'
$layout.Controls.Add($hint, 0, 3)
$output = [Windows.Forms.RichTextBox]::new()
$output.Dock = 'Fill'; $output.ReadOnly = $true; $output.WordWrap = $false
$output.Font = [Drawing.Font]::new('Consolas', 10)
$output.BackColor = [Drawing.Color]::FromArgb(25, 29, 36)
$output.ForeColor = [Drawing.Color]::FromArgb(220, 227, 235)
$layout.Controls.Add($output, 0, 4)
$status = [Windows.Forms.Label]::new(); $status.Dock = 'Fill'; $status.TextAlign = 'MiddleLeft'
$status.Text = '준비 · default = MinGW Debug / msvc = Visual Studio 2022'
$layout.Controls.Add($status, 0, 5)

$timer = [Windows.Forms.Timer]::new(); $timer.Interval = 300
$timer.Add_Tick({
    try {
        if ($script:worker) {
            $text = ''
            foreach ($path in @($script:stdout, $script:stderr)) {
                if (Test-Path -LiteralPath $path) { $text += (Get-Content -LiteralPath $path -Encoding UTF8 -Tail 180 | Out-String) }
            }
            if ($output.Text -ne $text) { $output.Text = $text; $output.SelectionStart = $output.TextLength; $output.ScrollToCaret() }
            if ($script:worker.HasExited) {
                $script:worker.WaitForExit()
                $code = $script:worker.ExitCode
                $script:worker.Dispose(); $script:worker = $null
                $preset.Enabled = $true
                foreach ($button in $busyButtons) { $button.Enabled = $true }
                $after = $script:pending; $script:pending = $null
                if ($code -ne 0) { $status.Text = "작업 실패 (종료 코드 $code) · 로그를 확인하세요." }
                elseif ($after -eq 'VisualStudio') { Open-Editor 'VisualStudio'; $status.Text = 'Visual Studio에서 솔루션을 열었습니다.' }
                elseif ($after) { Start-Session $after }
                else { $status.Text = '완료' }
            }
        }
        if ($script:waitingServer) {
            $server = $script:children[0].Process
            if ($server.HasExited) { throw '서버가 종료되었습니다. 로그 폴더에서 server 로그를 확인하세요.' }
            # 엔진의 파일 로그는 Info를 버퍼링하지만 stdout은 매 줄 flush한다.
            $serverLog = Join-Path $script:sessionDir 'server/stdout.log'
            if ((Test-Path -LiteralPath $serverLog) -and
                (Select-String -LiteralPath $serverLog -SimpleMatch 'failed to bind' -Quiet)) {
                throw '서버가 UDP 6780 포트를 열지 못했습니다.'
            }
            if ((Test-Path -LiteralPath $serverLog) -and
                (Select-String -LiteralPath $serverLog -SimpleMatch 'Dedicated server listening' -Quiet)) {
                $script:waitingServer = $false
                Start-Game 'GameClient' @('--join', '127.0.0.1') 'client-1'
                Start-Game 'GameClient' @('--join', '127.0.0.1') 'client-2'
                $status.Text = '서버 + 클라이언트 2개 실행 중'
            } elseif ([DateTime]::UtcNow -gt $script:serverDeadline) { throw '서버 시작 시간 초과. server 로그를 확인하세요.' }
        }
        foreach ($child in $script:children) {
            if ($child.Process.HasExited -and -not $child.Reported) {
                $child.Reported = $true
                $child.Process.WaitForExit()
                $status.Text = "$($child.Label) 종료 (코드 $($child.Process.ExitCode)) · 로그 폴더에서 확인"
            }
        }
        if (-not $script:worker -and $script:children.Count -gt 0) {
            $text = ''
            foreach ($child in $script:children) {
                $text += "--- $($child.Label) ---`r`n"
                foreach ($file in @('stdout.log', 'stderr.log')) {
                    $path = Join-Path $script:sessionDir "$($child.Label)/$file"
                    if (Test-Path -LiteralPath $path) { $text += (Get-Content -LiteralPath $path -Encoding UTF8 -Tail 25 | Out-String) }
                }
            }
            $text = $text -replace '\x1b\[[0-9;]*m', ''
            if ($output.Text -ne $text) { $output.Text = $text; $output.SelectionStart = $output.TextLength; $output.ScrollToCaret() }
        }
    } catch {
        $script:waitingServer = $false
        Stop-Games
        Show-Failure $_.Exception.Message
    }
})
$form.Add_FormClosing({
    param($sender, $eventArgs)
    if ($script:worker) {
        $eventArgs.Cancel = $true
        $status.Text = '빌드/테스트가 끝난 뒤 닫아주세요.'
        return
    }
    $timer.Stop()
    Stop-Games
})
$timer.Start()
try {
    if ($SmokeTest) {
        # 자동 검증에서도 컨트롤을 생성하고 실제 메시지 루프를 한 번 처리한다.
        $form.Show(); [Windows.Forms.Application]::DoEvents(); $form.Close()
        Write-Output 'Launcher UI smoke test passed.'
    } else { [void]$form.ShowDialog() }
} finally { $timer.Dispose(); $form.Dispose() }
