# 창 3개로 끝말잇기(규칙 선택 · 뜻 · 탈락 때 예시 단어)와 움직임(가위바위보 · 오목 돌 · 윷 던지기 · 말 이동)을 확인한다.
# 실행 (프로젝트 폴더에서):  powershell -NoProfile -ExecutionPolicy Bypass -File tools\uitest_anim.ps1 -outDir <캡처를 저장할 폴더>
# 약 1분 30초 걸린다 (끝말잇기에서 시간 초과로 탈락하는 것을 두 번 기다린다). 키보드 · 마우스는 가져가지 않는다.
param([string]$outDir = "$env:TEMP\mgp_shots")
$exe = Join-Path (Split-Path $PSScriptRoot -Parent) "minigame_gui.exe"
. (Join-Path $PSScriptRoot "uitest_lib.ps1")

$a = LaunchAs 0 0 0 2 0 5      # 색, 머리 장식, 판 테마, 말 모양
$b = LaunchAs 300 100 1 4 1 1
$c = LaunchAs 600 200 2 3 2 4
$wa = $a.MainWindowHandle; $wb = $b.MainWindowHandle; $wc = $c.MainWindowHandle
SetT (Edits $wa)[1] "주안"; SetT (Edits $wb)[1] "동완"; SetT (Edits $wc)[1] "성건"
$code = MakeRoom $wa
"code: $code"
foreach ($w in $wb, $wc) { JoinRoom $w $code }
Start-Sleep -Seconds 5

# ── 끝말잇기: 방장이 어인정을 켜고 시작 → 첫 사람이 단어를 냄 → 나머지는 시간 초과 ──
foreach ($w in $wa, $wb, $wc) { Click $w "끝말잇기" }
CClick $wa 262 386          # 어인정 허용 켜기 (한방 단어 금지는 처음부터 켜져 있음)
Start-Sleep -Milliseconds 300
Shot $wa "word_rules_host.png"
Shot $wb "word_rules_guest.png"
CClick $wa 435 430          # 게임 시작
Start-Sleep -Seconds 2
$log = RLog $wa
$m = [regex]::Match($log, "첫 단어: (\S+)")
$first = $m.Groups[1].Value
"first word: $first"
# 사전에서 끝 글자로 시작하는 두 글자 단어를 하나 골라, 세 창 모두에서 보낸다 (차례인 사람 것만 단어로 처리된다)
$last = $first.Substring($first.Length - 1)
$dictPath = Join-Path (Split-Path $PSScriptRoot -Parent) "data\korean_nouns.txt"
# 뜻이 실린 단어를 고른다 (뜻 자료 data\korean_defs.txt 는 사전 단어의 약 78%만 담고 있다)
$defPath = Join-Path (Split-Path $PSScriptRoot -Parent) "data\korean_defs.txt"
$word = [IO.File]::ReadLines($defPath, [Text.Encoding]::UTF8) | Where-Object { $_.StartsWith($last) -and $_.IndexOf("`t") -eq 2 -and $_.Contains("②") } | ForEach-Object { $_.Substring(0, 2) } | Select-Object -Skip 2 -First 1
"answer: $word"
foreach ($w in $wa, $wb, $wc) { Say $w $word }
Start-Sleep -Milliseconds 250
Shot $wa "word_pop.png"      # 새 단어가 튀어나오는 중
Start-Sleep -Seconds 1
Shot $wb "word_accept.png"   # 낸 단어와 뜻
Start-Sleep -Seconds 16
Shot $wa "word_out.png"      # 시간 초과 탈락 + 낼 수 있었던 단어
Start-Sleep -Seconds 16
Shot $wb "word_end.png"      # 끝난 화면: 우승자 + 마지막 탈락자가 낼 수 있었던 단어

# ── 가위바위보: 흔드는 중과 손을 편 뒤 ──
foreach ($w in $wa, $wb, $wc) { Click $w "가위바위보" }
CClick $wa 308 170; CClick $wb 436 170; CClick $wc 436 170
Start-Sleep -Milliseconds 300
Shot $wa "rps_shake.png"
Start-Sleep -Milliseconds 1800
Shot $wa "rps_open.png"

# ── 오목: 돌이 떨어지는 중 ──
InviteAll $wa "오목" @($wb, $wc)   # 오목은 2명이라 한 사람만 초대된다. 누가 초대됐는지 몰라 둘 다 수락을 눌러 본다
Start-Sleep -Seconds 1
foreach ($w in $wa, $wb, $wc) { CClick $w 229 269 }
Shot $wa "omok_drop.png"
foreach ($w in $wa, $wb, $wc) { CClick $w 256 269 }
Start-Sleep -Seconds 1
Shot $wa "omok_after.png"

# ── 윷놀이: 윷이 날아가는 중 → 결과가 튀어나옴 → 말이 뛰어감 ──
InviteAll $wa "윷놀이" @($wb, $wc)
Start-Sleep -Seconds 1
foreach ($w in $wa, $wb, $wc) { CClick $w 512 246 }      # 차례인 사람만 던져진다
Start-Sleep -Milliseconds 250
Shot $wa "yut_fly1.png"
Start-Sleep -Milliseconds 300
Shot $wb "yut_fly2.png"
Start-Sleep -Milliseconds 650
Shot $wa "yut_pop.png"
Start-Sleep -Seconds 1
# 윷 · 모가 나오면 한 번 더 던져야 하므로, 던지기와 새 말 출발을 번갈아 눌러 본다
foreach ($i in 1..3) {
    foreach ($w in $wa, $wb, $wc) { CClick $w 512 400 }  # 대기 말 새로 출발
    Start-Sleep -Milliseconds 300
    Shot $wa "yut_hop$i.png"
    Start-Sleep -Milliseconds 1700
    foreach ($w in $wa, $wb, $wc) { CClick $w 512 246 }
    Start-Sleep -Milliseconds 2200
}
Shot $wc "yut_after.png"

"--- log A ---"
(RLog $wa) -split "`r" | Where-Object { $_.Trim() } | Select-Object -Last 24
$a.Kill(); $b.Kill(); $c.Kill()
"shots: $outDir"
