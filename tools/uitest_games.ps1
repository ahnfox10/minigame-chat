# 창 3개를 띄워 방에 모은 뒤 윷놀이 · 레이싱 · 페인트볼 · 격투를 차례로 열고 화면을 캡처한다.
# 실행 (프로젝트 폴더에서):  powershell -NoProfile -ExecutionPolicy Bypass -File tools\uitest_games.ps1 -outDir <캡처를 저장할 폴더>
# 주의: 레이싱/페인트볼/격투 구간에서 실제 키보드 · 마우스를 잠깐 가져간다 (약 15초). 끝날 때 풀어 준다.
param([string]$outDir = "$env:TEMP\mgp_shots")
$exe = Join-Path (Split-Path $PSScriptRoot -Parent) "minigame_gui.exe"
. (Join-Path $PSScriptRoot "uitest_lib.ps1")

$a = LaunchAs 0 0 0 2 0 5      # 색, 머리 장식, 판 테마, 말 모양
$b = LaunchAs 300 100 1 4 1 1
$c = LaunchAs 600 200 2 3 2 2
$wa = $a.MainWindowHandle; $wb = $b.MainWindowHandle; $wc = $c.MainWindowHandle
SetT (Edits $wa)[1] "주안"; SetT (Edits $wb)[1] "동완"; SetT (Edits $wc)[1] "성건"
Click $wa "꾸미기"; Shot $wa "custom.png"; Click $wa "꾸미기"
$code = MakeRoom $wa
"code: $code"
foreach ($w in $wb, $wc) { JoinRoom $w $code }
Start-Sleep -Seconds 5
"titles: " + (Title $wb) + " / " + (Title $wc)

# 윷놀이 (3명): 차례가 누구든 던지기 → 새 말 출발을 몇 번 반복
InviteAll $wa "윷놀이" @($wb, $wc)
foreach ($i in 1..8) { foreach ($w in $wa, $wb, $wc) { CClick $w 512 246; CClick $w 512 400 } }
Start-Sleep -Seconds 1
Shot $wa "yut_a.png"

# 레이싱: A가 3.5초 가속
InviteAll $wa "레이싱" @($wb, $wc)
Shot $wa "race_start_a.png"
Start-Sleep -Seconds 3
Front $wa
Hold 0x57 3500
Shot $wa "race_drive_a.png"
Shot $wb "race_b.png"

# 페인트볼: A가 화면을 클릭해 마우스 조준 → 마우스를 옆으로 → 앞으로 걷기 → Esc로 마우스 풀기
InviteAll $wb "페인트볼" @($wa, $wc)
Start-Sleep -Seconds 4
Front $wa
Shot $wa "fps_before_a.png"
CClick $wa 320 240
Start-Sleep -Milliseconds 300
MoveMouse 160 0
Start-Sleep -Milliseconds 300
Shot $wa "fps_turned_a.png"
Hold 0x57 600
Hold 0x1B 80
Shot $wb "fps_b.png"
ReleaseMouse

# 격투: A가 오른쪽으로 걷다가 펀치
InviteAll $wc "격투" @($wa, $wb)
Start-Sleep -Seconds 4
Front $wa
Hold 0x44 900
Hold 0x4A 100
Shot $wa "fight_a.png"

"--- log A ---"
(RLog $wa) -split "`r" | Where-Object { $_.Trim() } | Select-Object -Last 14
ReleaseMouse
$a.Kill(); $b.Kill(); $c.Kill()
"shots: $outDir"
