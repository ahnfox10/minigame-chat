# 레이싱 주행만 빠르게 확인: 창 2개, A가 4초 가속. 실행법은 uitest_games.ps1과 같음
param([string]$outDir = "$env:TEMP\mgp_shots")
$exe = Join-Path (Split-Path $PSScriptRoot -Parent) "minigame_gui.exe"
. (Join-Path $PSScriptRoot "uitest_lib.ps1")
$a = LaunchAs 0 0 3 2 0 0
$b = LaunchAs 400 150 4 1 3 1
$wa = $a.MainWindowHandle; $wb = $b.MainWindowHandle
$code = MakeRoom $wa
JoinRoom $wb $code
Start-Sleep -Seconds 4
InviteAll $wa "레이싱" @($wb)
Start-Sleep -Seconds 3
Front $wa
Hold 0x57 4000
Shot $wa "race2_a.png"
Shot $wb "race2_b.png"
ReleaseMouse
$a.Kill(); $b.Kill()
