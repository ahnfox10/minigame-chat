# UI 자동 테스트 도우미: 창 찾기, 버튼 누르기, 게임 화면 클릭, 화면 캡처. 사용 예는 uitest_games.ps1
# 쓰기 전에 $exe (실행 파일 경로)와 $outDir (캡처 저장 폴더)를 정해 둘 것
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Drawing;
public static class G {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, string l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, StringBuilder l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, int m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, int f);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int w, int hh, int f);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public static List<IntPtr> Children(IntPtr p) { var l = new List<IntPtr>(); EnumChildWindows(p, (h, x) => { l.Add(h); return true; }, IntPtr.Zero); return l; }
  public static string Text(IntPtr h) { var s = new StringBuilder(65536); SendMessage(h, 0x000D, (IntPtr)65536, s); return s.ToString(); }
  public static string Cls(IntPtr h) { var s = new StringBuilder(256); GetClassName(h, s, 256); return s.ToString(); }
  public static void Shot(IntPtr h, string path) {   // 창 안쪽(제목 표시줄 제외)만
    RECT r; GetClientRect(h, out r);
    using (var bmp = new Bitmap(r.R, r.B)) using (var g = Graphics.FromImage(bmp)) {
      IntPtr dc = g.GetHdc(); PrintWindow(h, dc, 3); g.ReleaseHdc(dc); bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
    }
  }
}
"@
[G]::SetProcessDPIAware() | Out-Null

function Launch($x, $y) { $p = Start-Process $exe -PassThru; Start-Sleep -Milliseconds 1500; $p.Refresh(); [G]::SetWindowPos($p.MainWindowHandle, [IntPtr]::Zero, $x, $y, 0, 0, 0x0005) | Out-Null; return $p }
function Find($w, $cls, $txt) { [G]::Children($w) | Where-Object { [G]::Cls($_) -eq $cls -and ($txt -eq $null -or [G]::Text($_) -eq $txt) } | Select-Object -First 1 }
function Edits($w) { @([G]::Children($w) | Where-Object { [G]::Cls($_) -eq "Edit" }) }
function Click($w, $txt) { $b = Find $w "Button" $txt; if (-not $b) { "!! no button $txt"; return }; [G]::PostMessage($b, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null; Start-Sleep -Milliseconds 500 }
function SetT($h, $t) { [G]::SendMessage($h, 0x000C, [IntPtr]::Zero, $t) | Out-Null }
function CClick($w, $x, $y) { $r = New-Object G+RECT; [G]::GetClientRect($w, [ref]$r) | Out-Null
  $s = [Math]::Min($r.R / 1000.0, $r.B / 570.0); $px = [int]((10 + $x) * $s); $py = [int]((50 + $y) * $s); $lp = [IntPtr](($py -shl 16) -bor $px)
  [G]::PostMessage($w, 0x0201, [IntPtr]1, $lp) | Out-Null; Start-Sleep -Milliseconds 450 }
function Chat($w, $t) { $e = (Edits $w)[1]; SetT $e $t; Click $w "보내기" }
function Log($w) { [G]::Text((Edits $w)[0]) }
function Title($w) { $s = New-Object System.Text.StringBuilder 512; [G]::SendMessage($w, 0x000D, [IntPtr]512, $s) | Out-Null; $s.ToString() }
function Shot($w, $name) { [G]::Shot($w, (Join-Path $outDir $name)); "saved $name" }
function Count($s, $pat) { ([regex]::Matches($s, [regex]::Escape($pat))).Count }


# ── 미니게임 파티 전용 도우미 (이 파일을 고칠 때는 CLAUDE.md의 "테스트" 절도 함께 고칠 것) ──
Add-Type -Name K -Namespace U -MemberDefinition @"
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, int flags, System.IntPtr extra);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);
[DllImport("user32.dll")] public static extern bool GetCursorPos(out System.Drawing.Point p);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern bool ClipCursor(System.IntPtr r);
"@ -ReferencedAssemblies System.Drawing
New-Item -ItemType Directory -Force $outDir | Out-Null
# 채팅 기록 전체 (RichEdit)
function RLog($w) { [G]::Text((Find $w 'RICHEDIT50W' $null)) }
# 채팅 보내기. Edit 순서: [0] 채팅 입력, [1] 닉네임, [2] 방 코드, [3] 방장 IP, [4] 포트
function Say($w, $t) { SetT (Edits $w)[0] $t; Click $w '보내기' }
# 실제 키보드 키를 ms 동안 누름 (가상 키 코드). 실시간 게임은 창이 맨 앞에 있어야 입력을 받으므로 먼저 Front
function Hold($vk, $ms) { [U.K]::keybd_event($vk, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds $ms; [U.K]::keybd_event($vk, 0, 2, [IntPtr]::Zero) }
# 창을 맨 앞으로 가져오고 게임 화면 위쪽(320,60)을 클릭해 키보드 초점을 게임에 둠 (페인트볼에서는 이 클릭이 마우스 조준을 켬)
function Front($w) { [U.K]::SetForegroundWindow($w) | Out-Null; Start-Sleep -Milliseconds 300; CClick $w 320 60 }
# 실제 마우스를 지금 위치에서 (dx, dy)만큼 옮김 (페인트볼 마우스 조준 확인용)
function MoveMouse($dx, $dy) { $p = New-Object System.Drawing.Point; [U.K]::GetCursorPos([ref]$p) | Out-Null; [U.K]::SetCursorPos($p.X + $dx, $p.Y + $dy) | Out-Null }
# 마우스가 게임 화면에 갇혀 있으면 풀어 줌 (테스트가 중간에 죽어도 사용자 마우스가 묶이지 않게 끝에서 꼭 호출)
function ReleaseMouse { [U.K]::ClipCursor([IntPtr]::Zero) | Out-Null }
# 꾸미기 설정을 따로 가진 창을 띄움 (APPDATA를 창마다 다른 폴더로 바꿔서 설정 파일이 섞이지 않게 함)
function LaunchAs($x, $y, $color, $hat, $theme, $piece) {
  $d = Join-Path $outDir "cfg$color"
  New-Item -ItemType Directory -Force "$d\MinigameParty" | Out-Null
  Set-Content -Path "$d\MinigameParty\settings.ini" -Value "[me]`r`ncolor=$color`r`nhat=$hat`r`ntheme=$theme`r`npiece=$piece`r`n" -Encoding ascii
  $old = $env:APPDATA; $env:APPDATA = $d
  $p = Launch $x $y
  $env:APPDATA = $old
  return $p
}
# 방을 만들고 방 코드를 돌려줌 (창 제목 "미니게임 파티 - 방 CODE - 이름"에서 읽음, 최대 25초 기다림)
function MakeRoom($w) {
  Click $w "방 만들기 (2~8명)" | Out-Null
  $sw = [Diagnostics.Stopwatch]::StartNew(); $code = ""
  while ($sw.Elapsed.TotalSeconds -lt 25 -and -not $code) { Start-Sleep -Milliseconds 300; $code = [regex]::Match((Title $w), "방 ([A-C][A-Z0-9]{5})").Groups[1].Value }
  return $code
}
function JoinRoom($w, $code) { SetT (Edits $w)[2] $code; Click $w "코드로 참가" }
# $from 창에서 $tab 게임 탭을 열고 전체 선택 → 초대 보내기, $others 창들이 수락
# 게임 화면 좌표: 전체 선택 (115,415), 초대 보내기 (332,415), 초대 창의 수락 (230,295)
function InviteAll($from, $tab, $others) {
  Click $from $tab
  CClick $from 115 415
  CClick $from 332 415
  Start-Sleep -Seconds 2
  foreach ($o in $others) { CClick $o 230 295 }
  Start-Sleep -Seconds 2
}
