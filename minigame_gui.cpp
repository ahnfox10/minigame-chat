// 미니게임 채팅 GUI 버전 (Win32, 1:1 TCP)
// 게임: 가위바위보, 주사위, 틱택토, 윷놀이, 실시간 격투 + 채팅 명령(숫자야구, 업다운)
// 빌드: build.bat

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <unordered_set>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ───────────────────────── 전역 ─────────────────────────
enum { WM_NET = WM_APP + 1, WM_CONNRESULT, WM_NETCLOSED, WM_RELAYREADY };
enum { V_RPS, V_DICE, V_TTT, V_OMOK, V_YUT, V_FIGHT, V_WORD, V_GUD, V_COUNT };
static const wchar_t* VIEW_NAMES[V_COUNT] = {L"가위바위보", L"주사위", L"틱택토", L"오목", L"윷놀이", L"격투 대전", L"끝말잇기", L"업다운"};
// 모드별 게임 탭: 1:1 방 / 단체 방
static const int TABS_1[] = {V_RPS, V_DICE, V_TTT, V_OMOK, V_YUT, V_FIGHT};
static const int TABS_G[] = {V_WORD, V_RPS, V_DICE, V_GUD, V_TTT, V_OMOK, V_YUT, V_FIGHT};
static bool isPairView(int v) { return v == V_TTT || v == V_OMOK || v == V_YUT || v == V_FIGHT; }
enum { ID_GAME0 = 100, ID_SEND = 200, ID_HOST, ID_JOIN, ID_INPUT, ID_ONLINE, ID_CODEJOIN, ID_GROUP, ID_ENDMATCH, ID_HOME, ID_FULL };
enum { TIMER_FRAME = 1, TIMER_JOIN = 2, TIMER_PING = 3 };

// 온라인(방 코드) 모드: 공개 MQTT 중계 서버를 통해 메시지를 주고받는다.
struct Broker { const char* host; int port; };
static const Broker BROKERS[] = {{"broker.emqx.io", 1883}, {"broker.hivemq.com", 1883}, {"test.mosquitto.org", 1883}};
static const int NBROKER = 3;
static const char* CODE_ABC = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static const std::string TOPIC_BASE = "mgchat-rhkwptq/v1/";

const int CX = 10, CY = 50, CW = 640, CH = 480;      // 게임 화면(논리 좌표)
static int g_btnW = 101, g_btnStep = 107;   // 탭 버튼 크기 (모드에 따라 바뀜)
static const int* g_tabs = TABS_1;
static int g_tabCount = 6;

static HINSTANCE g_inst;
static HWND g_hwnd, hLog, hInput, hSend, hGameBtn[V_COUNT], hStrip, hStripBtn, hHomeBtn, hFullBtn;
static HWND hLblName, hName, hLblIp, hIp, hPort, hHost, hJoin, hStatus, hOnline, hLblCode, hCode, hCodeJoin, hGroup;
static double g_scale = 1.0;
static HFONT g_uiFont, g_logFont, g_tabFont;
static HBRUSH g_whiteBrush;

static SOCKET g_sock = INVALID_SOCKET;
static std::mutex g_sendMx;
static bool g_connected = false, g_busy = false, g_isHost = false, g_ended = false;
static bool g_relay = false;              // true = 방 코드(중계 서버) 모드
static std::string g_code, g_peerTopic;
static int g_joinTries = 0;
static bool g_group = false;              // true = 단체 방 (끝말잇기, 최대 8명)
static std::string g_pid, g_hostPid;      // 단체 방에서 나/방장을 구분하는 무작위 ID
static std::string g_matchPid;            // 단체 방에서 2인 게임을 함께 하는 상대
// 둘이 동시에 게임을 시작했을 때 누가 먼저 할지 정하는 규칙 (양쪽이 같은 답을 내야 함)
static bool tieFirst() { return g_group ? g_pid < g_matchPid : g_isHost; }
// 2인 게임이 끝났을 때 호출 (res: 1 승, -1 패, 0 무). 단체 방이면 결과를 방 전체에 알림
static void matchResult(const std::wstring& game, int res);
static std::wstring g_myName, g_peer = L"상대";
static int g_view = V_RPS;
static std::mt19937 g_rng{std::random_device{}()};

static const COLORREF COL_WIN = RGB(226, 230, 240), COL_BG = RGB(248, 249, 252), COL_TEXT = RGB(40, 44, 60),
                      COL_SUB = RGB(120, 125, 140), COL_ME = RGB(55, 105, 230), COL_OP = RGB(225, 65, 80),
                      COL_ACC = RGB(255, 160, 40), COL_WINC = RGB(30, 160, 80);

static int S(double v) { return (int)std::lround(v * g_scale); }
// 화면 배율: g_scale = g_baseScale(DPI) × g_zoom(전체 화면 확대). 전체 화면이면 가운데 정렬 여백 g_offX/Y
static double g_baseScale = 1.0, g_zoom = 1.0;
static int g_offX = 0, g_offY = 0;
static bool g_full = false;
static WINDOWPLACEMENT g_prevPlace{sizeof(WINDOWPLACEMENT)};
static const DWORD WND_STYLE = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
static int PX(double v) { return g_offX + S(v); }   // 논리 좌표 → 창 픽셀 좌표
static int PY(double v) { return g_offY + S(v); }
// 컨트롤의 논리 위치를 기억해 두었다가 배율이 바뀌면 다시 배치
struct CtlInfo { int x, y, w, h, font; };   // font: 0 기본, 1 채팅 로그, 2 탭
static std::map<HWND, CtlInfo> g_ctl;
static void placeCtl(HWND h) {
    auto it = g_ctl.find(h);
    if (it == g_ctl.end()) return;
    const CtlInfo& c = it->second;
    MoveWindow(h, PX(c.x), PY(c.y), S(c.w), S(c.h), TRUE);
    SendMessageW(h, WM_SETFONT, (WPARAM)(c.font == 1 ? g_logFont : c.font == 2 ? g_tabFont : g_uiFont), TRUE);
}
// 연결 세대 번호: 방에서 나가거나 대기를 취소하면 1 증가 → 예전 연결 스레드가 보낸 메시지는 무시
static std::atomic<unsigned> g_gen{1};
static SOCKET g_listen = INVALID_SOCKET;   // IP 방 만들기 대기 소켓

// ───────────────────────── 문자열 ─────────────────────────
static std::wstring W(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::string U8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> v;
    std::string cur;
    for (char c : s) {
        if (c == d) { v.push_back(cur); cur.clear(); }
        else cur += c;
    }
    v.push_back(cur);
    return v;
}
static std::wstring trimW(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::wstring getText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, 0);
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(n);
    return s;
}
static std::wstring I(int v) { return std::to_wstring(v); }

// ───────────────────────── 로그 / 네트워크 ─────────────────────────
static void logLine(const std::wstring& s) {
    int len = GetWindowTextLengthW(hLog);
    SendMessageW(hLog, EM_SETSEL, len, len);
    std::wstring t = (len ? L"\r\n" : L"") + s;
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)t.c_str());
    SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
}
static void sys(const std::wstring& s) { logLine(L"[시스템] " + s); }
static void gmsg(const std::wstring& s) { logLine(L"[게임] " + s); }

static bool sendAll(SOCKET s, const std::string& data) {
    const char* p = data.data();
    int left = (int)data.size();
    while (left > 0) {
        int n = send(s, p, left, 0);
        if (n <= 0) return false;
        p += n;
        left -= n;
    }
    return true;
}
static void rawSend(const std::string& data) {
    std::lock_guard<std::mutex> lk(g_sendMx);
    if (g_sock != INVALID_SOCKET) sendAll(g_sock, data);
}

// ── MQTT 3.1.1 최소 구현 (QoS 0) ──
static std::string mqttStr(const std::string& s) {
    std::string r;
    r += (char)((s.size() >> 8) & 0xFF);
    r += (char)(s.size() & 0xFF);
    return r + s;
}
static std::string mqttPacket(unsigned char hdr, const std::string& body) {
    std::string p(1, (char)hdr);
    size_t n = body.size();
    do {
        unsigned char b = n % 128;
        n /= 128;
        if (n) b |= 0x80;
        p += (char)b;
    } while (n);
    return p + body;
}
static bool recvAll(SOCKET s, char* b, int n) {
    while (n > 0) {
        int r = recv(s, b, n, 0);
        if (r <= 0) return false;
        b += r;
        n -= r;
    }
    return true;
}
static bool mqttRead(SOCKET s, unsigned char& hdr, std::string& body) {
    char c;
    if (!recvAll(s, &c, 1)) return false;
    hdr = (unsigned char)c;
    size_t len = 0, mul = 1;
    for (int i = 0; i < 4; i++) {
        if (!recvAll(s, &c, 1)) return false;
        len += (c & 0x7F) * mul;
        mul *= 128;
        if (!(c & 0x80)) break;
    }
    body.assign(len, 0);
    return len == 0 || recvAll(s, &body[0], (int)len);
}
static SOCKET tcpConnect(const char* host, int port, int ms) {
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, std::to_string(port).c_str(), &hints, &res) != 0) return INVALID_SOCKET;
    SOCKET out = INVALID_SOCKET;
    for (auto p = res; p && out == INVALID_SOCKET; p = p->ai_next) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
        connect(s, p->ai_addr, (int)p->ai_addrlen);
        fd_set w, e;
        FD_ZERO(&w); FD_SET(s, &w);
        FD_ZERO(&e); FD_SET(s, &e);
        timeval tv{ms / 1000, (ms % 1000) * 1000};
        if (select(0, nullptr, &w, &e, &tv) > 0 && FD_ISSET(s, &w)) {
            nb = 0;
            ioctlsocket(s, FIONBIO, &nb);
            out = s;
        } else {
            closesocket(s);
        }
    }
    freeaddrinfo(res);
    return out;
}
// 중계 서버에 접속하고 내 주소(토픽)를 구독한다. 성공하면 소켓 반환.
// my = 구독할 토픽, willTopic/willMsg = 연결이 갑자기 끊기면 서버가 대신 보내줄 메시지
static SOCKET mqttOpen(int bi, const std::string& my, const std::string& willTopic, const std::string& willMsg) {
    SOCKET s = tcpConnect(BROKERS[bi].host, BROKERS[bi].port, 5000);
    if (s == INVALID_SOCKET) return s;
    DWORD to = 6000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
    std::random_device rd;
    char id[40];
    snprintf(id, sizeof(id), "mgc-%08x%08x", rd(), rd());
    std::string body = mqttStr("MQTT");
    body += (char)4;      // 프로토콜 레벨 3.1.1
    body += (char)0x06;   // clean session + will(상대에게 BYE 자동 전송)
    body += (char)0;
    body += (char)60;     // keep alive 60초
    body += mqttStr(id) + mqttStr(willTopic) + mqttStr(willMsg);
    unsigned char h;
    std::string b;
    bool ok = sendAll(s, mqttPacket(0x10, body)) && mqttRead(s, h, b) && (h >> 4) == 2 && b.size() >= 2 && b[1] == 0;
    if (ok) {
        std::string sub;
        sub += (char)0;
        sub += (char)1;
        sub += mqttStr(my);
        sub += (char)0;
        ok = sendAll(s, mqttPacket(0x82, sub)) && mqttRead(s, h, b) && (h >> 4) == 9 && b.size() >= 3 &&
             (unsigned char)b[2] != 0x80;
    }
    if (!ok) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    to = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
    BOOL nd = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nd, sizeof(nd));
    return s;
}

static void sendMsg(const std::string& type, const std::string& payload = "") {
    if (g_sock == INVALID_SOCKET) return;
    std::string msg = type + "|" + payload;
    std::replace(msg.begin(), msg.end(), '\n', ' ');
    std::replace(msg.begin(), msg.end(), '\r', ' ');
    if (g_group) msg = g_pid + "|" + msg;   // 단체 방: 보낸 사람 ID를 앞에 붙임
    if (g_relay) rawSend(mqttPacket(0x30, mqttStr(g_peerTopic) + msg));
    else rawSend(msg + "\n");
}

static void invalidateCanvas() {
    RECT r{PX(CX), PY(CY), PX(CX + CW), PY(CY + CH)};
    InvalidateRect(g_hwnd, &r, FALSE);
}
static void setView(int v) {
    g_view = v;
    RECT r{0, 0, PX(CX + CW), PY(CY)};
    InvalidateRect(g_hwnd, &r, FALSE);
    invalidateCanvas();
}

// ───────────────────────── 그리기 도구 ─────────────────────────
static std::map<int, HFONT> g_fonts;
static HFONT font(int size, bool bold = false, bool emoji = false) {
    int key = size * 4 + (bold ? 1 : 0) + (emoji ? 2 : 0);
    auto it = g_fonts.find(key);
    if (it != g_fonts.end()) return it->second;
    HFONT f = CreateFontW(-size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                          CLEARTYPE_QUALITY, 0, emoji ? L"Segoe UI Emoji" : L"Malgun Gothic");
    g_fonts[key] = f;
    return f;
}
static void text(HDC dc, int x, int y, int w, int h, const std::wstring& s, int size, COLORREF c, bool bold = false,
                 UINT fmt = DT_CENTER | DT_VCENTER | DT_SINGLELINE, bool emoji = false) {
    SelectObject(dc, font(size, bold, emoji));
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    RECT r{x, y, x + w, y + h};
    DrawTextW(dc, s.c_str(), -1, &r, fmt | DT_NOPREFIX);
}
static void fillRect(HDC dc, int x, int y, int w, int h, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    RECT r{x, y, x + w, y + h};
    FillRect(dc, &r, b);
    DeleteObject(b);
}
static void roundRect(HDC dc, int x, int y, int w, int h, COLORREF fill, COLORREF border, int rad = 14, int pw = 2) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, pw, border);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, x, y, x + w, y + h, rad, rad);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}
static void circle(HDC dc, int cx, int cy, int r, COLORREF fill, COLORREF border, int pw = 2) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, pw, border);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}
static void ring(HDC dc, int cx, int cy, int r, COLORREF c, int pw) {
    HPEN p = CreatePen(PS_SOLID, pw, c);
    HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(p);
}
static void line(HDC dc, int x1, int y1, int x2, int y2, COLORREF c, int w) {
    HPEN p = CreatePen(PS_SOLID, w, c);
    HGDIOBJ op = SelectObject(dc, p);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, op);
    DeleteObject(p);
}
static COLORREF darker(COLORREF c, double f = 0.75) {
    return RGB((int)(GetRValue(c) * f), (int)(GetGValue(c) * f), (int)(GetBValue(c) * f));
}

struct Btn {
    int x, y, w, h;
    bool hit(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
static void button(HDC dc, const Btn& b, const std::wstring& s, bool enabled = true, COLORREF col = COL_ME,
                   int size = 18) {
    COLORREF f = enabled ? col : RGB(190, 192, 200);
    roundRect(dc, b.x, b.y, b.w, b.h, f, darker(f), 16, 2);
    text(dc, b.x, b.y, b.w, b.h, s, size, RGB(255, 255, 255), true);
}
static void title(HDC dc, const std::wstring& t, const std::wstring& sub) {
    text(dc, 20, 10, 400, 32, t, 24, COL_TEXT, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    text(dc, 20, 40, 600, 22, sub, 15, COL_SUB, false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

// ═════════════════════════ 가위바위보 ═════════════════════════
static struct { int my = -1, peer = -1, win = 0, lose = 0, draw = 0, lastMy = -1, lastPeer = -1, lastRes = 0; } R;
static const wchar_t* RPS_N[3] = {L"가위", L"바위", L"보"};
static const wchar_t* RPS_E[3] = {L"✌", L"✊", L"✋"};
static Btn rpsBtn(int i) { return {45 + i * 190, 110, 170, 170}; }

static void rpsResolve() {
    int r = (R.my - R.peer + 3) % 3;
    R.lastRes = r == 0 ? 0 : r == 1 ? 1 : -1;
    if (R.lastRes == 0) R.draw++;
    else if (R.lastRes == 1) R.win++;
    else R.lose++;
    R.lastMy = R.my;
    R.lastPeer = R.peer;
    gmsg(std::wstring(L"가위바위보: 나 ") + RPS_N[R.my] + L" vs " + g_peer + L" " + RPS_N[R.peer] + L" → " +
         (R.lastRes == 0 ? L"무승부" : R.lastRes == 1 ? L"승리!" : L"패배"));
    R.my = R.peer = -1;
}
static void rpsPick(int c) {
    if (R.my >= 0) return;
    R.my = c;
    sendMsg("RPS", std::to_string(c));
    if (R.peer >= 0) rpsResolve();
    invalidateCanvas();
}
static void drawRps(HDC dc) {
    std::wstring sub = R.my >= 0    ? L"선택 완료! " + g_peer + L"를 기다리는 중..."
                       : R.peer >= 0 ? g_peer + L"는 이미 냈어요. 하나를 고르세요!"
                                     : L"하나를 고르세요. 둘 다 내면 결과가 공개됩니다.";
    title(dc, L"가위바위보", sub);
    for (int i = 0; i < 3; i++) {
        Btn b = rpsBtn(i);
        bool sel = R.my == i;
        roundRect(dc, b.x, b.y, b.w, b.h, sel ? RGB(255, 236, 200) : RGB(255, 255, 255), sel ? COL_ACC : RGB(200, 205, 220),
                  24, sel ? 4 : 2);
        text(dc, b.x, b.y + 15, b.w, 95, RPS_E[i], 64, COL_TEXT, false, DT_CENTER | DT_VCENTER | DT_SINGLELINE, true);
        text(dc, b.x, b.y + 115, b.w, 40, RPS_N[i], 24, COL_TEXT, true);
    }
    if (R.lastMy >= 0) {
        text(dc, 0, 300, CW, 40,
             std::wstring(L"나  ") + RPS_E[R.lastMy] + L" " + RPS_N[R.lastMy] + L"     vs     " + RPS_E[R.lastPeer] + L" " +
                 RPS_N[R.lastPeer] + L"  " + g_peer,
             22, COL_TEXT);
        COLORREF c = R.lastRes == 1 ? COL_WINC : R.lastRes == -1 ? COL_OP : COL_ACC;
        text(dc, 0, 345, CW, 60, R.lastRes == 1 ? L"승리!" : R.lastRes == -1 ? L"패배..." : L"무승부!", 44, c, true);
    }
    text(dc, 0, 420, CW, 30, L"전적  " + I(R.win) + L"승  " + I(R.lose) + L"패  " + I(R.draw) + L"무", 18, COL_SUB, true);
}

// ═════════════════════════ 주사위 ═════════════════════════
static struct { int my1 = -1, my2 = -1, p1 = -1, p2 = -1, l[4] = {0, 0, 0, 0}, res = 0; bool has = false; } D;
static const Btn diceBtn{240, 395, 160, 56};

static void diceResolve() {
    int a = D.my1 + D.my2, b = D.p1 + D.p2;
    D.l[0] = D.my1; D.l[1] = D.my2; D.l[2] = D.p1; D.l[3] = D.p2;
    D.has = true;
    D.res = a > b ? 1 : a < b ? -1 : 0;
    gmsg(L"주사위: 나 " + I(a) + L" vs " + g_peer + L" " + I(b) + L" → " +
         (D.res == 1 ? L"승리!" : D.res == -1 ? L"패배" : L"무승부"));
    D.my1 = D.my2 = D.p1 = D.p2 = -1;
}
static void diceRoll() {
    if (D.my1 >= 0) return;
    std::uniform_int_distribution<int> d(1, 6);
    D.my1 = d(g_rng);
    D.my2 = d(g_rng);
    sendMsg("DICE", std::to_string(D.my1) + "|" + std::to_string(D.my2));
    if (D.p1 >= 0) diceResolve();
    invalidateCanvas();
}
static void dieFace(HDC dc, int x, int y, int s, int v, COLORREF border) {
    roundRect(dc, x, y, s, s, RGB(255, 255, 255), border, 18, 3);
    if (v <= 0) {
        text(dc, x, y, s, s, L"?", s / 2, COL_SUB, true);
        return;
    }
    static const int P[7][7][2] = {{},
                                   {{1, 1}},
                                   {{0, 0}, {2, 2}},
                                   {{0, 0}, {1, 1}, {2, 2}},
                                   {{0, 0}, {2, 0}, {0, 2}, {2, 2}},
                                   {{0, 0}, {2, 0}, {1, 1}, {0, 2}, {2, 2}},
                                   {{0, 0}, {2, 0}, {0, 1}, {2, 1}, {0, 2}, {2, 2}}};
    int r = s / 11;
    for (int i = 0; i < v; i++) {
        int px = x + s / 4 + P[v][i][0] * s / 4, py = y + s / 4 + P[v][i][1] * s / 4;
        circle(dc, px, py, r, v == 1 ? COL_OP : COL_TEXT, v == 1 ? COL_OP : COL_TEXT, 1);
    }
}
static void drawDice(HDC dc) {
    title(dc, L"주사위 대결", L"주사위 2개를 굴려 합이 큰 사람이 이깁니다.");
    int a1 = -1, a2 = -1, b1 = -1, b2 = -1;
    std::wstring st;
    if (D.my1 >= 0) { a1 = D.my1; a2 = D.my2; st = g_peer + L"를 기다리는 중..."; }
    else if (D.p1 >= 0) st = g_peer + L"가 굴렸어요! 굴리기를 누르세요.";
    else if (D.has) { a1 = D.l[0]; a2 = D.l[1]; b1 = D.l[2]; b2 = D.l[3]; }
    text(dc, 30, 85, 280, 30, L"나 (" + g_myName + L")", 20, COL_ME, true);
    text(dc, 330, 85, 280, 30, g_peer, 20, COL_OP, true);
    dieFace(dc, 50, 125, 110, a1, COL_ME);
    dieFace(dc, 180, 125, 110, a2, COL_ME);
    dieFace(dc, 350, 125, 110, b1, COL_OP);
    dieFace(dc, 480, 125, 110, b2, COL_OP);
    if (a1 > 0) text(dc, 30, 245, 280, 34, L"합계 " + I(a1 + a2), 22, COL_TEXT, true);
    if (b1 > 0) text(dc, 330, 245, 280, 34, L"합계 " + I(b1 + b2), 22, COL_TEXT, true);
    if (!st.empty()) text(dc, 0, 300, CW, 40, st, 20, COL_SUB, true);
    else if (D.has) {
        COLORREF c = D.res == 1 ? COL_WINC : D.res == -1 ? COL_OP : COL_ACC;
        text(dc, 0, 295, CW, 70, D.res == 1 ? L"승리!" : D.res == -1 ? L"패배..." : L"무승부!", 44, c, true);
    }
    button(dc, diceBtn, L"🎲 굴리기", D.my1 < 0, COL_ME, 20);
}

// ═════════════════════════ 틱택토 ═════════════════════════
static struct { bool on = false; char b[9]; char my = 'X', turn = 'X'; int moves = 0; std::wstring result; } T;
static const Btn tttNew{490, 420, 130, 44};

static void tttReset(char my) {
    T.on = true;
    for (int i = 0; i < 9; i++) T.b[i] = 0;
    T.my = my;
    T.turn = 'X';
    T.moves = 0;
    T.result.clear();
}
static char tttWinner() {
    static const int L[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};
    for (auto& l : L)
        if (T.b[l[0]] && T.b[l[0]] == T.b[l[1]] && T.b[l[1]] == T.b[l[2]]) return T.b[l[0]];
    return 0;
}
static void tttAfter() {
    char w = tttWinner();
    if (w) {
        T.result = w == T.my ? L"승리!" : L"패배...";
        gmsg(L"틱택토 " + T.result);
        T.on = false;
        matchResult(L"틱택토", w == T.my ? 1 : -1);
    } else if (T.moves == 9) {
        T.result = L"무승부!";
        gmsg(L"틱택토 무승부");
        T.on = false;
        matchResult(L"틱택토", 0);
    }
    invalidateCanvas();
}
static void tttStart() {
    tttReset('X');
    sendMsg("TTT_START");
    gmsg(L"틱택토 시작! 당신은 X (선공)");
    invalidateCanvas();
}
static void tttMove(int p) {
    if (!T.on || T.turn != T.my || T.b[p]) return;
    T.b[p] = T.my;
    T.moves++;
    T.turn = T.my == 'X' ? 'O' : 'X';
    sendMsg("TTT_MOVE", std::to_string(p));
    tttAfter();
}
static void drawTtt(HDC dc) {
    std::wstring sub;
    if (T.on) sub = std::wstring(L"당신은 ") + (T.my == 'X' ? L"X" : L"O") + L" · " + (T.turn == T.my ? L"당신 차례입니다!" : g_peer + L"의 차례입니다.");
    else sub = L"새 게임을 누르면 시작합니다. (시작한 사람이 X, 선공)";
    title(dc, L"틱택토", sub);
    int gx = 170, gy = 75, cs = 100;
    roundRect(dc, gx - 8, gy - 8, cs * 3 + 16, cs * 3 + 16, RGB(255, 255, 255), RGB(210, 214, 228), 20, 2);
    for (int i = 1; i < 3; i++) {
        line(dc, gx + i * cs, gy + 6, gx + i * cs, gy + cs * 3 - 6, RGB(170, 175, 195), 4);
        line(dc, gx + 6, gy + i * cs, gx + cs * 3 - 6, gy + i * cs, RGB(170, 175, 195), 4);
    }
    for (int i = 0; i < 9; i++) {
        int x = gx + (i % 3) * cs, y = gy + (i / 3) * cs;
        bool mine = T.b[i] == T.my;
        if (T.b[i] == 'X') {
            COLORREF c = mine ? COL_ME : COL_OP;
            line(dc, x + 25, y + 25, x + cs - 25, y + cs - 25, c, 10);
            line(dc, x + cs - 25, y + 25, x + 25, y + cs - 25, c, 10);
        } else if (T.b[i] == 'O') {
            ring(dc, x + cs / 2, y + cs / 2, 28, mine ? COL_ME : COL_OP, 9);
        }
    }
    if (!T.result.empty()) {
        COLORREF c = T.result == L"승리!" ? COL_WINC : T.result == L"패배..." ? COL_OP : COL_ACC;
        text(dc, 20, 400, 300, 70, T.result, 40, c, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    button(dc, tttNew, L"새 게임", true, COL_ME);
}

// ═════════════════════════ 오목 ═════════════════════════
const int OM_N = 15, OM_X = 40, OM_Y = 80, OM_S = 27;   // 15줄, 칸 간격 27
static struct {
    bool on = false;
    char b[OM_N * OM_N];   // 0 빈칸, 1 흑, 2 백
    char my = 1, turn = 1;
    int moves = 0, last = -1;
    std::wstring result;
} O;
static const Btn omNew{452, 372, 170, 44}, omResign{452, 424, 170, 38};

static void omReset(char my) {
    O.on = true;
    memset(O.b, 0, sizeof(O.b));
    O.my = my;
    O.turn = 1;
    O.moves = 0;
    O.last = -1;
    O.result.clear();
}
static bool omFive(int p) {
    char c = O.b[p];
    int x = p % OM_N, y = p / OM_N;
    static const int D[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
    for (auto& d : D) {
        int n = 1;
        for (int s = -1; s <= 1; s += 2) {
            int cx = x + d[0] * s, cy = y + d[1] * s;
            while (cx >= 0 && cx < OM_N && cy >= 0 && cy < OM_N && O.b[cy * OM_N + cx] == c) {
                n++;
                cx += d[0] * s;
                cy += d[1] * s;
            }
        }
        if (n >= 5) return true;
    }
    return false;
}
static void omPlace(int p, char c) {
    O.b[p] = c;
    O.moves++;
    O.last = p;
    O.turn = c == 1 ? 2 : 1;
    if (omFive(p)) {
        O.result = c == O.my ? L"승리!" : L"패배...";
        O.on = false;
        gmsg(L"오목 " + O.result);
        matchResult(L"오목", c == O.my ? 1 : -1);
    } else if (O.moves == OM_N * OM_N) {
        O.result = L"무승부!";
        O.on = false;
        matchResult(L"오목", 0);
    }
    invalidateCanvas();
}
static void omStart() {
    omReset(1);
    sendMsg("OMOK_START");
    gmsg(L"오목 시작! 당신은 흑돌 (선공)");
    invalidateCanvas();
}
static void omClick(int x, int y) {
    if (omNew.hit(x, y)) { omStart(); return; }
    if (omResign.hit(x, y) && O.on) {
        O.on = false;
        O.result = L"기권패";
        sendMsg("OMOK_QUIT");
        gmsg(L"오목을 기권했습니다.");
        invalidateCanvas();
        return;
    }
    if (!O.on || O.turn != O.my) return;
    int ix = (int)std::lround((x - OM_X) / (double)OM_S), iy = (int)std::lround((y - OM_Y) / (double)OM_S);
    if (ix < 0 || ix >= OM_N || iy < 0 || iy >= OM_N) return;
    int p = iy * OM_N + ix;
    if (O.b[p]) return;
    sendMsg("OMOK_MOVE", std::to_string(p));
    omPlace(p, O.my);
}
static void omStone(HDC dc, int cx, int cy, int r, char c) {
    if (c == 1) circle(dc, cx, cy, r, RGB(35, 35, 40), RGB(10, 10, 10), 1);
    else circle(dc, cx, cy, r, RGB(250, 250, 250), RGB(140, 140, 140), 1);
}
static void drawOmok(HDC dc) {
    std::wstring sub;
    if (O.on) sub = std::wstring(L"당신은 ") + (O.my == 1 ? L"흑돌" : L"백돌") + L" · " + (O.turn == O.my ? std::wstring(L"당신 차례!") : g_peer + L"의 차례");
    else sub = L"새 게임을 누르면 시작합니다. (시작한 사람이 흑돌, 선공) · 5목을 먼저 만들면 승리";
    title(dc, L"오목", sub);
    int E = (OM_N - 1) * OM_S;
    roundRect(dc, OM_X - 16, OM_Y - 16, E + 32, E + 32, RGB(232, 192, 125), RGB(180, 135, 70), 14, 2);
    for (int i = 0; i < OM_N; i++) {
        line(dc, OM_X, OM_Y + i * OM_S, OM_X + E, OM_Y + i * OM_S, RGB(120, 82, 40), 1);
        line(dc, OM_X + i * OM_S, OM_Y, OM_X + i * OM_S, OM_Y + E, RGB(120, 82, 40), 1);
    }
    static const int STAR[5][2] = {{3, 3}, {11, 3}, {3, 11}, {11, 11}, {7, 7}};
    for (auto& s : STAR) circle(dc, OM_X + s[0] * OM_S, OM_Y + s[1] * OM_S, 3, RGB(120, 82, 40), RGB(120, 82, 40), 1);
    for (int p = 0; p < OM_N * OM_N; p++) {
        if (!O.b[p]) continue;
        int cx = OM_X + (p % OM_N) * OM_S, cy = OM_Y + (p / OM_N) * OM_S;
        omStone(dc, cx, cy, 12, O.b[p]);
        if (p == O.last) circle(dc, cx, cy, 4, COL_OP, COL_OP, 1);
    }
    // 오른쪽 패널
    omStone(dc, 466, 92, 11, O.my);
    text(dc, 484, 80, 150, 24, L"나 (" + g_myName + L")", 15, COL_ME, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    omStone(dc, 466, 124, 11, O.my == 1 ? 2 : 1);
    text(dc, 484, 112, 150, 24, g_peer, 15, COL_OP, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (O.on) {
        bool mine = O.turn == O.my;
        text(dc, 452, 160, 175, 34, mine ? L"▶ 내 차례" : L"▶ 상대 차례", 20, mine ? COL_ME : COL_OP, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        text(dc, 452, 196, 175, 22, I(O.moves) + L"수 진행", 14, COL_SUB, false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    if (!O.result.empty()) {
        COLORREF c = O.result.find(L"승리") != std::wstring::npos ? COL_WINC : O.result == L"무승부!" ? COL_ACC : COL_OP;
        text(dc, 452, 240, 175, 90, O.result, 30, c, true, DT_CENTER | DT_WORDBREAK);
    }
    button(dc, omNew, L"새 게임", true, COL_ME);
    button(dc, omResign, L"기권", O.on, RGB(150, 155, 170), 15);
}

// ═════════════════════════ 윷놀이 ═════════════════════════
// 판 위치: 0=출발점(오른쪽 아래), 1~19 바깥 둘레(반시계), 20~24 오른쪽위→가운데(22)→왼쪽아래,
//          25~28 왼쪽위→가운데→출발점.  -1=대기, 99=완주
static const int HOME = -1, DONE = 99;
static const wchar_t* YUT_N[6] = {L"", L"도", L"개", L"걸", L"윷", L"모"};
static struct {
    bool on = false;
    int mySeat = 0, turn = 0, winner = -1, actions = 0;
    int pos[2][4];
    int throwsLeft = 0;
    std::vector<int> pending;
    int sel = -1;
    int last = 0;
    int sticks[4] = {0, 0, 0, 0};   // 1 = 배(평평한 면)
    std::wstring note;
} Y;
static const Btn yutMainBtn{400, 222, 225, 48}, yutHomeBtn{400, 380, 225, 40}, yutQuitBtn{560, 14, 66, 28};
static Btn yutChip(int i) { return {400 + (i % 4) * 57, 305 + (i / 4) * 40, 52, 34}; }

static int yutWalk(int p, int steps) {
    int prev = -1;
    if (p == HOME) p = 0;
    for (int i = 0; i < steps; i++) {
        int n;
        if (i == 0 && p == 5) n = 20;            // 모서리에서 출발하면 지름길
        else if (i == 0 && p == 10) n = 25;
        else if (i == 0 && p == 22) n = 27;      // 가운데에 멈췄다 출발하면 출발점 방향
        else if (p == 22) n = (prev == 26) ? 27 : 23;
        else if (p == 19 || p == 28) return DONE; // 출발점 도착 = 완주
        else if (p == 24) n = 15;
        else if (p == 26) n = 22;
        else n = p + 1;
        prev = p;
        p = n;
    }
    return p;
}
static POINT yutXY(int p) {
    const POINT BR{355, 400}, TR{355, 100}, TL{55, 100}, BL{55, 400}, C{205, 250};
    auto lerp = [](POINT a, POINT b, double t) {
        return POINT{(LONG)std::lround(a.x + (b.x - a.x) * t), (LONG)std::lround(a.y + (b.y - a.y) * t)};
    };
    if (p >= 0 && p < 20) {
        POINT c[5] = {BR, TR, TL, BL, BR};
        return lerp(c[p / 5], c[p / 5 + 1], (p % 5) / 5.0);
    }
    switch (p) {
        case 20: return lerp(TR, C, 1 / 3.0);
        case 21: return lerp(TR, C, 2 / 3.0);
        case 22: return C;
        case 23: return lerp(C, BL, 1 / 3.0);
        case 24: return lerp(C, BL, 2 / 3.0);
        case 25: return lerp(TL, C, 1 / 3.0);
        case 26: return lerp(TL, C, 2 / 3.0);
        case 27: return lerp(C, BR, 1 / 3.0);
        case 28: return lerp(C, BR, 2 / 3.0);
    }
    return BR;
}
static void yutReset(int mySeat) {
    Y.on = true;
    Y.mySeat = mySeat;
    Y.turn = 0;
    Y.winner = -1;
    Y.actions = 0;
    for (auto& s : Y.pos)
        for (int& p : s) p = HOME;
    Y.throwsLeft = 1;
    Y.pending.clear();
    Y.sel = -1;
    Y.last = 0;
    Y.note.clear();
}
static std::wstring seatName(int seat) { return seat == Y.mySeat ? L"나" : g_peer; }

static void yutApplyThrow(int res, const std::string& st) {
    Y.actions++;
    Y.last = res;
    for (int i = 0; i < 4; i++) Y.sticks[i] = (i < (int)st.size() && st[i] == '1') ? 1 : 0;
    Y.throwsLeft--;
    Y.pending.push_back(res);
    std::wstring who = seatName(Y.turn);
    if (res >= 4) {
        Y.throwsLeft++;
        Y.note = who + L": " + YUT_N[res] + L"! 한 번 더 던집니다";
    } else {
        Y.note = who + L": " + YUT_N[res];
    }
    gmsg(L"윷놀이 " + Y.note);
    Y.sel = -1;
    invalidateCanvas();
}
static void yutApplyMove(int piece, int pi) {
    if (pi < 0 || pi >= (int)Y.pending.size() || piece < 0 || piece > 3) return;
    int seat = Y.turn;
    int start = Y.pos[seat][piece];
    if (start == DONE) return;
    Y.actions++;
    int steps = Y.pending[pi];
    Y.pending.erase(Y.pending.begin() + pi);
    int dest = yutWalk(start, steps);
    int moved = 0;
    for (int k = 0; k < 4; k++) {
        if ((start == HOME && k == piece) || (start != HOME && Y.pos[seat][k] == start)) {
            Y.pos[seat][k] = dest;
            moved++;
        }
    }
    std::wstring who = seatName(seat);
    std::wstring msg = who + L" " + YUT_N[steps] + (steps == 4 ? L"으로 이동" : L"로 이동") + (moved > 1 ? L" (말 " + I(moved) + L"개 업고)" : L"");
    if (dest == DONE) msg += L" → 완주!";
    if (dest != DONE) {
        int caught = 0;
        for (int& p : Y.pos[1 - seat])
            if (p == dest) { p = HOME; caught++; }
        if (caught) {
            Y.throwsLeft++;
            msg += L" → 상대 말 " + I(caught) + L"개 잡음! 한 번 더!";
        }
    }
    Y.note = msg;
    gmsg(L"윷놀이 " + msg);
    bool all = true;
    for (int p : Y.pos[seat]) all = all && p == DONE;
    if (all) {
        Y.winner = seat;
        Y.on = false;
        gmsg(seat == Y.mySeat ? L"윷놀이 승리! 🎉" : L"윷놀이 패배...");
        matchResult(L"윷놀이", seat == Y.mySeat ? 1 : -1);
    } else if (Y.throwsLeft == 0 && Y.pending.empty()) {
        Y.turn = 1 - Y.turn;
        Y.throwsLeft = 1;
    }
    Y.sel = -1;
    invalidateCanvas();
}
static void yutStart() {
    yutReset(0);
    sendMsg("YUT_START");
    gmsg(L"윷놀이 시작! 내가 먼저 던집니다.");
    invalidateCanvas();
}
static void yutThrow() {
    if (!Y.on || Y.turn != Y.mySeat || Y.throwsLeft <= 0) return;
    std::string st;
    int flats = 0;
    for (int i = 0; i < 4; i++) {
        int f = std::uniform_int_distribution<int>(0, 1)(g_rng);
        flats += f;
        st += f ? '1' : '0';
    }
    int res = flats == 0 ? 5 : flats;
    sendMsg("YUT_THROW", std::to_string(res) + "|" + st);
    yutApplyThrow(res, st);
}
static void yutMove(int piece) {
    int sel = Y.sel >= 0 ? Y.sel : (Y.pending.size() == 1 ? 0 : -1);
    if (sel < 0) {
        Y.note = L"먼저 오른쪽에서 사용할 결과(도/개/걸...)를 고르세요";
        invalidateCanvas();
        return;
    }
    sendMsg("YUT_MOVE", std::to_string(piece) + "|" + std::to_string(sel));
    yutApplyMove(piece, sel);
}
static void yutClick(int x, int y) {
    if (yutQuitBtn.hit(x, y) && Y.on) {
        Y.on = false;
        sendMsg("YUT_QUIT");
        gmsg(L"윷놀이를 그만뒀습니다.");
        invalidateCanvas();
        return;
    }
    if (!Y.on) {
        if (yutMainBtn.hit(x, y)) yutStart();
        return;
    }
    if (Y.turn != Y.mySeat) return;
    if (Y.throwsLeft > 0) {
        if (yutMainBtn.hit(x, y)) yutThrow();
        return;
    }
    for (int i = 0; i < (int)Y.pending.size(); i++)
        if (yutChip(i).hit(x, y)) { Y.sel = i; invalidateCanvas(); return; }
    int me = Y.mySeat;
    if (yutHomeBtn.hit(x, y)) {
        for (int k = 0; k < 4; k++)
            if (Y.pos[me][k] == HOME) { yutMove(k); return; }
        return;
    }
    for (int k = 0; k < 4; k++) {
        int p = Y.pos[me][k];
        if (p < 0 || p > 28) continue;
        POINT c = yutXY(p);
        if ((x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) <= 20 * 20) { yutMove(k); return; }
    }
}
static void drawStick(HDC dc, int x, int y, bool flat) {
    if (flat) {
        roundRect(dc, x, y, 34, 86, RGB(252, 238, 205), RGB(160, 110, 60), 16, 2);
        for (int i = 0; i < 3; i++) {
            int cy = y + 20 + i * 23;
            line(dc, x + 11, cy - 6, x + 23, cy + 6, RGB(120, 70, 30), 2);
            line(dc, x + 23, cy - 6, x + 11, cy + 6, RGB(120, 70, 30), 2);
        }
    } else {
        roundRect(dc, x, y, 34, 86, RGB(160, 100, 50), RGB(110, 65, 30), 16, 2);
    }
}
static void drawYut(HDC dc) {
    bool myTurn = Y.on && Y.turn == Y.mySeat;
    std::wstring sub;
    if (!Y.on) sub = Y.winner >= 0 ? L"게임 종료" : L"새 게임을 누르면 시작합니다. (시작한 사람이 먼저)";
    else if (!myTurn) sub = g_peer + L"의 차례입니다.";
    else if (Y.throwsLeft > 0) sub = L"내 차례! 윷을 던지세요.";
    else sub = L"결과를 고르고, 움직일 내 말(또는 대기 말)을 클릭하세요.";
    title(dc, L"윷놀이", sub);
    if (Y.on) button(dc, yutQuitBtn, L"그만", true, RGB(150, 155, 170), 14);

    // 판
    roundRect(dc, 28, 73, 354, 354, RGB(244, 226, 190), RGB(190, 150, 100), 24, 3);
    POINT BR = yutXY(0), TR = yutXY(5), TL = yutXY(10), BL = yutXY(15);
    COLORREF lc = RGB(170, 125, 80);
    line(dc, BR.x, BR.y, TR.x, TR.y, lc, 3);
    line(dc, TR.x, TR.y, TL.x, TL.y, lc, 3);
    line(dc, TL.x, TL.y, BL.x, BL.y, lc, 3);
    line(dc, BL.x, BL.y, BR.x, BR.y, lc, 3);
    line(dc, TR.x, TR.y, BL.x, BL.y, lc, 3);
    line(dc, TL.x, TL.y, BR.x, BR.y, lc, 3);
    for (int p = 0; p <= 28; p++) {
        POINT c = yutXY(p);
        bool big = p % 5 == 0 && p < 20 || p == 22;
        circle(dc, c.x, c.y, big ? 17 : 11, big ? RGB(255, 250, 235) : RGB(255, 255, 255), lc, 2);
    }
    text(dc, BR.x - 50, BR.y + 30, 100, 20, L"▲ 출발/도착", 13, RGB(130, 90, 50), true);

    // 말
    for (int s = 0; s < 2; s++) {
        COLORREF c = s == Y.mySeat ? COL_ME : COL_OP;
        std::map<int, int> cnt;
        for (int p : Y.pos[s])
            if (p >= 0 && p <= 28) cnt[p]++;
        for (auto& kv : cnt) {
            POINT pt = yutXY(kv.first);
            bool selectable = s == Y.mySeat && myTurn && Y.throwsLeft == 0;
            if (selectable) ring(dc, pt.x, pt.y, 17, COL_ACC, 4);
            circle(dc, pt.x, pt.y, 13, c, darker(c), 2);
            if (kv.second > 1) text(dc, pt.x - 13, pt.y - 13, 26, 26, I(kv.second), 15, RGB(255, 255, 255), true);
        }
    }

    // 오른쪽 패널
    std::wstring turnTxt = !Y.on ? L"" : myTurn ? L"▶ 내 차례" : L"▶ " + g_peer + L" 차례";
    text(dc, 400, 60, 225, 30, turnTxt, 20, myTurn ? COL_ME : COL_OP, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    for (int i = 0; i < 4; i++) drawStick(dc, 404 + i * 44, 95, Y.last ? Y.sticks[i] == 1 : (i % 2 == 0));
    if (Y.last) text(dc, 585, 95, 45, 86, YUT_N[Y.last], 26, COL_TEXT, true);
    text(dc, 400, 186, 230, 30, Y.note, 13, COL_SUB, false, DT_LEFT | DT_WORDBREAK);

    if (!Y.on) button(dc, yutMainBtn, L"새 게임", true, COL_ME, 20);
    else button(dc, yutMainBtn, L"윷 던지기", myTurn && Y.throwsLeft > 0, COL_ME, 20);

    text(dc, 400, 278, 225, 24, L"남은 이동:", 14, COL_SUB, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    int autoSel = Y.sel >= 0 ? Y.sel : (Y.pending.size() == 1 ? 0 : -1);
    for (int i = 0; i < (int)Y.pending.size() && i < 8; i++) {
        Btn b = yutChip(i);
        bool sel = i == autoSel && myTurn && Y.throwsLeft == 0;
        roundRect(dc, b.x, b.y, b.w, b.h, sel ? COL_ACC : RGB(255, 255, 255), sel ? darker(COL_ACC) : RGB(190, 195, 210), 12, 2);
        text(dc, b.x, b.y, b.w, b.h, YUT_N[Y.pending[i]], 18, sel ? RGB(255, 255, 255) : COL_TEXT, true);
    }

    int myHome = 0, myDone = 0, opHome = 0, opDone = 0;
    for (int p : Y.pos[Y.mySeat]) { myHome += p == HOME; myDone += p == DONE; }
    for (int p : Y.pos[1 - Y.mySeat]) { opHome += p == HOME; opDone += p == DONE; }
    if (Y.on || Y.winner >= 0) {
        bool canHome = myTurn && Y.throwsLeft == 0 && myHome > 0;
        button(dc, yutHomeBtn, L"대기 말 " + I(myHome) + L"개  ▶ 새로 출발", canHome, COL_ME, 15);
        text(dc, 400, 425, 230, 22, L"완주  나 " + I(myDone) + L"/4   ·   " + g_peer + L" " + I(opDone) + L"/4", 14, COL_TEXT, true,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        text(dc, 400, 447, 230, 22, g_peer + L" 대기 말 " + I(opHome) + L"개", 14, COL_SUB, false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    if (Y.winner >= 0) {
        bool w = Y.winner == Y.mySeat;
        roundRect(dc, 90, 190, 240, 80, RGB(255, 255, 255), w ? COL_WINC : COL_OP, 20, 3);
        text(dc, 90, 190, 240, 80, w ? L"승리! 🎉" : L"패배...", 36, w ? COL_WINC : COL_OP, true);
    }
}

// ═════════════════════════ 격투 ═════════════════════════
const float GROUND = 400.f;
struct Fighter {
    float x = 160, y = GROUND, vy = 0, kb = 0;
    int facing = 1, hp = 100, act = 0, actT = 0, stun = 0;
    bool guard = false;
};
struct AtkInfo { int dur, a0, a1, range, dmg; };
static const AtkInfo ATK[3] = {{0, 0, 0, 0, 0}, {16, 4, 8, 58, 7}, {26, 9, 14, 84, 12}};
static struct FightState {
    bool on = false;
    int countdown = 0, goT = 0, result = 0, tick = 0, fxT = 0;
    float fxX = 0, fxY = 0;
    float ox = 0, oy = GROUND;   // 화면에 그릴 상대 위치 (네트워크 지연을 부드럽게 보정)
    bool hitDone = false, pW = false, pP = false, pK = false;
    Fighter me, op;
} F;
static const Btn fightBtn{230, 250, 180, 54};

static void fightReset(bool left) {
    F = FightState();
    F.on = true;
    F.countdown = 180;
    F.me.x = left ? 160.f : 480.f;
    F.op.x = left ? 480.f : 160.f;
    F.me.facing = left ? 1 : -1;
    F.op.facing = -F.me.facing;
    F.ox = F.op.x;
    F.oy = GROUND;
}
static void fightStart() {
    fightReset(true);
    sendMsg("FIGHT_START");
    gmsg(L"격투 대전 시작! 게임 화면을 클릭한 뒤 키보드로 조작하세요.");
    SetFocus(g_hwnd);
    invalidateCanvas();
}
static bool keyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
static void fightSendState() {
    char buf[128];
    snprintf(buf, sizeof(buf), "%d|%d|%d|%d|%d|%d|%d|%d", (int)F.me.x, (int)F.me.y, F.me.facing, F.me.act, F.me.actT,
             F.me.guard ? 1 : 0, F.me.hp, F.me.stun);
    sendMsg("FS", buf);
}
static void fightTick() {
    F.tick++;
    if (F.fxT > 0) F.fxT--;
    if (F.goT > 0) F.goT--;
    if (F.countdown > 0 && --F.countdown == 0) F.goT = 45;
    F.ox += (F.op.x - F.ox) * 0.35f;
    F.oy += (F.op.y - F.oy) * 0.5f;
    Fighter& m = F.me;
    bool canInput = F.countdown == 0 && F.result == 0 && GetForegroundWindow() == g_hwnd && GetFocus() != hInput;
    bool L = false, Rt = false, J = false, G = false, P = false, K = false;
    if (canInput) {
        L = keyDown('A') || keyDown(VK_LEFT);
        Rt = keyDown('D') || keyDown(VK_RIGHT);
        J = keyDown('W') || keyDown(VK_UP);
        G = keyDown('S') || keyDown(VK_DOWN);
        P = keyDown('J') || keyDown('Z');
        K = keyDown('K') || keyDown('X');
    }
    bool onGround = m.y >= GROUND - 0.5f;
    if (m.stun > 0) {
        m.stun--;
        m.guard = false;
    } else {
        m.guard = G && onGround && m.act == 0;
        if (m.act == 0 && !m.guard) {
            if (L) m.x -= 4.5f;
            if (Rt) m.x += 4.5f;
            if (J && !F.pW && onGround) m.vy = -13.5f;
            if (P && !F.pP) { m.act = 1; m.actT = 0; F.hitDone = false; }
            else if (K && !F.pK) { m.act = 2; m.actT = 0; F.hitDone = false; }
        }
    }
    F.pW = J; F.pP = P; F.pK = K;
    if (m.act == 0 && m.stun == 0) m.facing = F.op.x >= m.x ? 1 : -1;
    m.vy += 0.75f;
    m.y += m.vy;
    if (m.y >= GROUND) { m.y = GROUND; m.vy = 0; }
    m.x += m.kb;
    m.kb *= 0.8f;
    if (std::fabs(m.kb) < 0.1f) m.kb = 0;
    m.x = std::min(610.f, std::max(30.f, m.x));
    if (m.act) {
        const AtkInfo& a = ATK[m.act];
        m.actT++;
        if (!F.hitDone && F.result == 0 && m.actT >= a.a0 && m.actT <= a.a1) {
            float dx = (F.op.x - m.x) * m.facing;
            if (dx > 0 && dx < a.range && std::fabs(F.op.y - m.y) < 70) {
                F.hitDone = true;
                sendMsg("FHIT", std::to_string(a.dmg) + "|" + std::to_string(m.facing));
                F.fxT = 10;
                F.fxX = F.op.x;
                F.fxY = F.op.y - 60;
            }
        }
        if (m.actT >= a.dur) { m.act = 0; m.actT = 0; }
    }
    if (F.tick % 2 == 0) fightSendState();
}
static void fightOnHit(int dmg, int dir) {
    if (!F.on || F.result) return;
    bool blocked = F.me.guard && F.me.facing == -dir;
    int d = blocked ? std::max(1, dmg / 5) : dmg;
    F.me.hp -= d;
    F.me.kb = dir * (blocked ? 3.f : 7.f);
    if (!blocked) { F.me.stun = 16; F.me.act = 0; }
    F.fxT = 10;
    F.fxX = F.me.x;
    F.fxY = F.me.y - 60;
    if (F.me.hp <= 0) {
        F.me.hp = 0;
        F.result = -1;
        sendMsg("FKO");
        gmsg(L"격투 패배... K.O.");
    }
    fightSendState();
}
static void drawFighter(HDC dc, const Fighter& f, COLORREF col, const std::wstring& name) {
    int x = (int)f.x, y = (int)f.y, d = f.facing;
    bool air = f.y < GROUND - 1;
    int lean = f.stun > 0 ? -d * 10 : 0;
    int hipY = y - 42, shY = y - 78, neckY = y - 86;
    POINT hip{x, hipY}, neck{x + lean, neckY};
    // 다리
    POINT backFoot{x - d * 16, y}, frontFoot{x + d * 16, y}, backKnee{x - d * 10, y - 20}, frontKnee{x + d * 12, y - 22};
    if (air) { backFoot = {x - d * 12, y - 8}; frontFoot = {x + d * 16, y - 14}; backKnee = {x - d * 4, y - 22}; frontKnee = {x + d * 18, y - 30}; }
    bool kicking = f.act == 2 && f.actT >= 6 && f.actT <= 17;
    if (kicking) { frontKnee = {x + d * 30, y - 48}; frontFoot = {x + d * 64, y - 54}; backFoot = {x - d * 14, y}; }
    // 팔
    POINT sh{neck.x, shY};
    POINT fElb{x + d * 16 + lean, shY + 18}, fHand{x + d * 30 + lean, shY + 2};
    POINT bElb{x - d * 6 + lean, shY + 20}, bHand{x + d * 12 + lean, shY + 8};
    bool punching = f.act == 1 && f.actT >= 3 && f.actT <= 10;
    if (punching) { fElb = {x + d * 26, shY + 2}; fHand = {x + d * 54, shY + 2}; }
    if (f.guard) { fElb = {x + d * 16, shY + 10}; fHand = {x + d * 20, shY - 18}; bElb = {x + d * 8, shY + 12}; bHand = {x + d * 14, shY - 12}; }
    if (f.stun > 0) { fElb = {x - d * 8 + lean, shY - 6}; fHand = {x - d * 26 + lean, shY - 18}; }

    COLORREF dk = darker(col, 0.7);
    // 그림자
    HBRUSH sb = CreateSolidBrush(RGB(95, 140, 70));
    HGDIOBJ ob = SelectObject(dc, sb), op = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, x - 28, (int)GROUND - 5, x + 28, (int)GROUND + 6);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(sb);

    line(dc, hip.x, hip.y, backKnee.x, backKnee.y, dk, 8);
    line(dc, backKnee.x, backKnee.y, backFoot.x, backFoot.y, dk, 8);
    line(dc, sh.x, sh.y, bElb.x, bElb.y, dk, 7);
    line(dc, bElb.x, bElb.y, bHand.x, bHand.y, dk, 7);
    line(dc, hip.x, hip.y, neck.x, neck.y, col, 12);
    line(dc, hip.x, hip.y, frontKnee.x, frontKnee.y, col, 8);
    line(dc, frontKnee.x, frontKnee.y, frontFoot.x, frontFoot.y, col, 8);
    line(dc, sh.x, sh.y, fElb.x, fElb.y, col, 7);
    line(dc, fElb.x, fElb.y, fHand.x, fHand.y, col, 7);
    circle(dc, fHand.x, fHand.y, 7, RGB(250, 250, 250), dk, 2);
    circle(dc, bHand.x, bHand.y, 6, RGB(230, 230, 230), dk, 2);
    circle(dc, neck.x, neck.y - 14, 14, RGB(255, 222, 190), dk, 2);
    // 눈 + 머리띠
    line(dc, neck.x - 13, neck.y - 20, neck.x + 13, neck.y - 20, col, 4);
    circle(dc, neck.x + d * 6, neck.y - 14, 2, COL_TEXT, COL_TEXT, 1);
    if (f.guard) text(dc, x - 40, y - 160, 80, 18, L"🛡 방어", 13, dk, true);
    text(dc, x - 60, y - 142, 120, 18, name, 14, dk, true);
}
static void hpBar(HDC dc, int x, int y, int w, int hp, COLORREF c, bool rightAlign, const std::wstring& name) {
    roundRect(dc, x, y, w, 22, RGB(255, 255, 255), RGB(80, 85, 100), 10, 2);
    int fw = (w - 6) * std::max(0, hp) / 100;
    COLORREF bc = hp > 50 ? RGB(60, 200, 100) : hp > 25 ? RGB(250, 190, 40) : RGB(235, 60, 60);
    if (fw > 0) fillRect(dc, rightAlign ? x + 3 + (w - 6 - fw) : x + 3, y + 3, fw, 16, bc);
    text(dc, x, y + 24, w, 20, name + L"  " + I(std::max(0, hp)), 14, c, true,
         (rightAlign ? DT_RIGHT : DT_LEFT) | DT_VCENTER | DT_SINGLELINE);
}
static void drawFight(HDC dc) {
    // 배경
    for (int i = 0; i < 20; i++) fillRect(dc, 0, i * 20, CW, 20, RGB(170 + i * 3, 200 + i * 2, 245));
    fillRect(dc, 0, (int)GROUND, CW, CH - (int)GROUND, RGB(120, 170, 90));
    fillRect(dc, 0, (int)GROUND, CW, 6, RGB(95, 140, 70));
    circle(dc, 560, 70, 30, RGB(255, 240, 160), RGB(255, 220, 120), 2);

    hpBar(dc, 15, 12, 270, F.on ? F.me.hp : 100, COL_ME, false, L"나 (" + g_myName + L")");
    hpBar(dc, 355, 12, 270, F.on ? F.op.hp : 100, COL_OP, true, g_peer);
    text(dc, 285, 10, 70, 28, L"VS", 22, COL_TEXT, true);

    if (F.on) {
        Fighter od = F.op;
        od.x = F.ox;
        od.y = F.oy;
        drawFighter(dc, od, COL_OP, g_peer);
        drawFighter(dc, F.me, COL_ME, L"나");
        if (F.fxT > 0) {
            int r = 10 + (10 - F.fxT) * 3;
            ring(dc, (int)F.fxX, (int)F.fxY, r, RGB(255, 210, 40), 5);
            circle(dc, (int)F.fxX, (int)F.fxY, 8, RGB(255, 250, 200), RGB(255, 180, 0), 2);
        }
        if (F.countdown > 0)
            text(dc, 0, 140, CW, 100, I(F.countdown / 60 + 1), 80, COL_TEXT, true);
        else if (F.goT > 0)
            text(dc, 0, 140, CW, 100, L"FIGHT!", 70, COL_OP, true);
        if (F.result) {
            roundRect(dc, 170, 120, 300, 110, RGB(255, 255, 255), F.result > 0 ? COL_WINC : COL_OP, 24, 4);
            text(dc, 170, 125, 300, 70, F.result > 0 ? L"승리! K.O." : L"패배...", 42, F.result > 0 ? COL_WINC : COL_OP, true);
            text(dc, 170, 190, 300, 30, L"다시 하려면 아래 버튼", 15, COL_SUB);
            button(dc, fightBtn, L"다시 대전", true, COL_OP, 20);
        }
        if (GetFocus() == hInput && F.result == 0)
            text(dc, 0, 60, CW, 24, L"⌨ 채팅창에 입력 중 — 조작하려면 게임 화면을 클릭하세요 (Esc)", 15, COL_OP, true);
    } else {
        roundRect(dc, 150, 90, 340, 140, RGB(255, 255, 255), RGB(200, 205, 220), 24, 2);
        text(dc, 150, 100, 340, 40, L"🥊 격투 대전", 26, COL_TEXT, true);
        text(dc, 150, 145, 340, 70, L"먼저 HP를 0으로 만드는 사람이 승리!\n방어 중에는 데미지가 크게 줄어듭니다.", 15, COL_SUB, false,
             DT_CENTER | DT_WORDBREAK);
        button(dc, fightBtn, L"대전 시작", true, COL_OP, 20);
    }
    text(dc, 0, (int)GROUND + 20, CW, 24, L"A/D 이동   W 점프   S 방어   J 펀치   K 발차기   (방향키 / Z X도 가능)", 15,
         RGB(255, 255, 255), true);
    text(dc, 0, (int)GROUND + 46, CW, 22, L"게임 화면을 클릭해야 키보드가 게임에 들어갑니다 · Enter = 채팅", 13,
         RGB(235, 245, 230));
}

// ═════════════════════════ 숫자야구 / 업다운 (채팅 명령) ═════════════════════════
static struct { int bb = 0; std::string secret; bool peerReady = false, myTurn = false, iStarted = false; int tries = 0; } B;
static struct { int ud = 0, answer = 0, tries = 0; } UD;

static bool validBB(const std::string& s) {
    if (s.size() != 3) return false;
    for (char c : s)
        if (c < '1' || c > '9') return false;
    return s[0] != s[1] && s[1] != s[2] && s[0] != s[2];
}
static void bbBegin() {
    B.bb = 2;
    B.tries = 0;
    B.myTurn = B.iStarted;
    gmsg(L"숫자야구 시작! " + (B.myTurn ? std::wstring(L"당신 먼저 /g 123") : g_peer + L"가 먼저 추측합니다."));
}
static void bbStartLocal(bool iStarted) {
    B.bb = 1;
    B.secret.clear();
    B.peerReady = false;
    B.iStarted = iStarted;
    gmsg(L"숫자야구! 상대가 맞힐 비밀 숫자를 정하세요: /secret 123 (1~9, 서로 다른 3자리)");
}

static void printHelp() {
    logLine(L"──── 도움말 ────");
    logLine(L"· 위쪽 버튼으로 게임 선택");
    logLine(L"· 입력 + Enter = 채팅,  Esc = 게임 화면으로");
    logLine(L"· 숫자야구: /bb → /secret 123 → /g 456");
    logLine(L"· 업다운: /ud 로 출제 → 상대는 /u 50");
    logLine(L"· /quitgame = 숫자야구·업다운 중단");
    logLine(L"───────────────");
}

static void handleCommand(const std::wstring& wline) {
    std::string line = U8(wline);
    std::vector<std::string> w;
    {
        std::string cur;
        for (char c : line) {
            if (c == ' ' || c == '\t') { if (!cur.empty()) w.push_back(cur); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) w.push_back(cur);
    }
    std::string cmd = w[0], arg = w.size() > 1 ? w[1] : "";
    if (cmd == "/help" || cmd == "/?") printHelp();
    else if (cmd == "/bb" || cmd == "/야구") {
        if (B.bb) { gmsg(L"이미 숫자야구 진행 중입니다."); return; }
        bbStartLocal(true);
        sendMsg("BB_START");
    } else if (cmd == "/secret") {
        if (B.bb != 1) { gmsg(L"/bb 로 먼저 숫자야구를 시작하세요."); return; }
        if (!B.secret.empty()) { gmsg(L"이미 정했습니다: " + W(B.secret)); return; }
        if (!validBB(arg)) { gmsg(L"1~9 사이 서로 다른 숫자 3자리를 입력하세요. 예) /secret 375"); return; }
        B.secret = arg;
        sendMsg("BB_READY");
        gmsg(L"비밀 숫자 " + W(arg) + L" 설정 완료!");
        if (B.peerReady) bbBegin();
        else gmsg(g_peer + L"가 숫자를 정하길 기다리는 중...");
    } else if (cmd == "/g") {
        if (B.bb != 2) { gmsg(L"진행 중인 숫자야구가 없습니다."); return; }
        if (!B.myTurn) { gmsg(g_peer + L"의 차례입니다."); return; }
        if (!validBB(arg)) { gmsg(L"1~9 사이 서로 다른 숫자 3자리를 입력하세요."); return; }
        B.myTurn = false;
        B.tries++;
        sendMsg("BB_GUESS", arg);
    } else if (cmd == "/ud" || cmd == "/업다운") {
        if (UD.ud) { gmsg(L"이미 업다운 진행 중입니다."); return; }
        int n = atoi(arg.c_str());
        if (n < 1 || n > 100) n = std::uniform_int_distribution<int>(1, 100)(g_rng);
        UD.ud = 1;
        UD.answer = n;
        UD.tries = 0;
        sendMsg("UD_START");
        gmsg(L"업다운 출제! 정답은 " + I(n) + L" 입니다. " + g_peer + L"가 맞히길 기다리는 중...");
    } else if (cmd == "/u") {
        if (UD.ud != 2) { gmsg(L"맞힐 업다운 문제가 없습니다."); return; }
        int g = atoi(arg.c_str());
        if (g < 1 || g > 100) { gmsg(L"1~100 사이 숫자를 입력하세요."); return; }
        sendMsg("UD_GUESS", std::to_string(g));
    } else if (cmd == "/quitgame") {
        if (B.bb) { B.bb = 0; sendMsg("BB_QUIT"); }
        if (UD.ud) { UD.ud = 0; sendMsg("UD_QUIT"); }
        gmsg(L"숫자야구/업다운을 중단했습니다.");
    } else {
        gmsg(L"알 수 없는 명령어입니다. /help");
    }
}

// ═════════════════════════ 받은 메시지 ═════════════════════════
static void onConnected();
static void onDisconnected(const std::wstring& why);
static void relayFail(const std::wstring& why);
static void copyToClipboard(const std::wstring& s);

// ═════════════════════════ 단체 방: 끝말잇기 ═════════════════════════
// 방장 프로그램이 심판: 참가자 명단, 단어 검사, 제한 시간, 탈락을 결정해서 모두에게 알린다.
// 참가자는 방장이 보낸 결과만 화면에 반영한다.
const int GROUP_MAX = 8;
struct GPlayer {
    std::string pid;
    std::wstring name;
    int score = 0;
    bool alive = false;
};
static std::vector<GPlayer> GP;   // 참가자 (게임 중에는 이 순서가 차례 순서)
static struct {
    bool on = false;
    std::wstring last, msg, winner;
    std::string cur;              // 지금 차례인 사람
    ULONGLONG deadline = 0;
    int turnMs = 15000;
    std::vector<std::pair<std::wstring, std::wstring>> hist;   // (이름, 단어)
    std::vector<std::wstring> used;
} K;
static const Btn grpStartBtn{335, 405, 200, 52}, grpCopyBtn{520, 14, 106, 30};
static const wchar_t* START_WORDS[] = {L"사과", L"기차", L"바다", L"나무", L"하늘", L"고양이", L"컴퓨터", L"학교", L"자동차",
                                       L"축구", L"우산", L"연필", L"사탕", L"토끼", L"가방", L"구름", L"시계", L"바나나",
                                       L"호랑이", L"피아노", L"딸기", L"수박", L"모자", L"거울", L"지구", L"도서관", L"비행기"};

static GPlayer* findP(const std::string& pid) {
    for (auto& p : GP)
        if (p.pid == pid) return &p;
    return nullptr;
}
static std::wstring pname(const std::string& pid) {
    if (pid == g_pid) return g_myName;
    GPlayer* p = findP(pid);
    return p ? p->name : L"?";
}
static int aliveCount() {
    int n = 0;
    for (auto& p : GP) n += p.alive;
    return n;
}
static std::string nextAlive(const std::string& cur) {
    int n = (int)GP.size(), start = -1;
    for (int i = 0; i < n; i++)
        if (GP[i].pid == cur) start = i;
    for (int k = 1; k <= n; k++) {
        const GPlayer& p = GP[(start + k + n) % n];
        if (p.alive && p.pid != cur) return p.pid;
    }
    return "";
}
static std::wstring safeName(std::wstring s) {
    for (wchar_t& c : s)
        if (c == L'|' || c == L',' || c == L';') c = L' ';
    return s;
}
static void grpSendRoster() {
    std::string s;
    for (auto& p : GP) {
        if (!s.empty()) s += ";";
        s += p.pid + "," + std::to_string(p.score) + "," + (p.alive ? "1" : "0") + "," + U8(p.name);
    }
    sendMsg("ROSTER", s);
}
static void grpParseRoster(const std::string& s) {
    GP.clear();
    for (auto& e : split(s, ';')) {
        auto f = split(e, ',');
        if (f.size() < 4) continue;
        GPlayer p;
        p.pid = f[0];
        p.score = atoi(f[1].c_str());
        p.alive = f[2] == "1";
        p.name = W(f[3]);
        GP.push_back(p);
    }
}

// 두음법칙: 녀→여, 라→나, 리→이 ...
static wchar_t dueum(wchar_t c) {
    if (c < 0xAC00 || c > 0xD7A3) return c;
    int s = c - 0xAC00, cho = s / (21 * 28), jung = (s / 28) % 21, jong = s % 28;
    bool yi = jung == 2 || jung == 6 || jung == 7 || jung == 12 || jung == 17 || jung == 20;   // ㅑㅕㅖㅛㅠㅣ
    if (cho == 5) cho = yi ? 11 : 2;        // ㄹ → ㅇ 또는 ㄴ
    else if (cho == 2 && yi) cho = 11;      // ㄴ → ㅇ
    else return c;
    return (wchar_t)(0xAC00 + (cho * 21 + jung) * 28 + jong);
}
// 끝말잇기 사전: exe에 넣은 표준국어대사전 명사 목록 (data/korean_nouns.txt, CC0)
static std::unordered_set<std::wstring>* g_dict = nullptr;
static const std::unordered_set<std::wstring>* dict() {
    static bool tried = false;
    if (tried) return g_dict;
    tried = true;
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(1), RT_RCDATA);
    if (!r) return nullptr;   // 사전 없이 빌드된 경우 검사 생략
    HGLOBAL h = LoadResource(nullptr, r);
    DWORD n = SizeofResource(nullptr, r);
    const char* p = h ? (const char*)LockResource(h) : nullptr;
    if (!p || !n) return nullptr;
    std::wstring all = W(std::string(p, n));
    g_dict = new std::unordered_set<std::wstring>();
    g_dict->reserve(200000);
    size_t s = 0;
    while (s < all.size()) {
        size_t e = all.find(L'\n', s);
        if (e == std::wstring::npos) e = all.size();
        if (e > s) g_dict->insert(all.substr(s, e - s));
        s = e + 1;
    }
    return g_dict;
}
static std::wstring checkWord(const std::wstring& w) {   // 빈 문자열이면 통과
    if (w.size() < 2 || w.size() > 12) return L"2~12글자 단어만 쓸 수 있어요";
    for (wchar_t c : w)
        if (c < 0xAC00 || c > 0xD7A3) return L"완성된 한글만 쓸 수 있어요";
    wchar_t lc = K.last.back();
    if (w[0] != lc && w[0] != dueum(lc)) return std::wstring(L"'") + lc + L"'(으)로 시작해야 해요";
    for (auto& u : K.used)
        if (u == w) return L"이미 나온 단어예요";
    if (const auto* d = dict())
        if (!d->count(w)) return L"사전에 없는 단어예요";
    return L"";
}

// ── 화면 반영 (방장·참가자 공통) ──
static void kTurnNotice() {
    if (K.on && K.cur == g_pid) {
        gmsg(L"내 차례! 채팅창에 '" + std::wstring(1, K.last.back()) + L"'(으)로 시작하는 단어를 입력하세요");
        SetFocus(hInput);
        FlashWindow(g_hwnd, TRUE);
    }
}
static void kSetTurn(const std::string& cur, int ms) {
    K.cur = cur;
    K.turnMs = ms;
    K.deadline = GetTickCount64() + ms;
}
static void kApplyStart(const std::wstring& word, const std::string& cur, int ms) {
    K.on = true;
    K.last = word;
    K.used = {word};
    K.hist = {{L"시작 단어", word}};
    K.winner.clear();
    K.msg = L"시작 단어는 '" + word + L"'";
    kSetTurn(cur, ms);
    logLine(L"[끝말잇기] 게임 시작! 첫 단어: " + word);
    kTurnNotice();
}
static void kApplyAccept(const std::string& pid, const std::wstring& word, const std::string& next, int ms, int score) {
    if (GPlayer* p = findP(pid)) p->score = score;
    K.last = word;
    K.used.push_back(word);
    K.hist.push_back({pname(pid), word});
    K.msg = pname(pid) + L"  +" + I((int)word.size() * 10) + L"점";
    logLine(L"[끝말잇기] " + pname(pid) + L": " + word);
    kSetTurn(next, ms);
    kTurnNotice();
}
static void kApplyDeny(const std::string& pid, const std::wstring& word, const std::wstring& why) {
    K.msg = pname(pid) + L"의 '" + word + L"' 거절 — " + why;
    if (pid == g_pid) gmsg(L"'" + word + L"' 거절: " + why + L" (시간 안에 다시 입력하세요)");
}
static void kApplyOut(const std::string& pid, const std::string& next, int ms, const std::wstring& why) {
    if (GPlayer* p = findP(pid)) p->alive = false;
    K.msg = pname(pid) + L" 탈락! (" + why + L")";
    logLine(L"[끝말잇기] " + K.msg);
    if (!next.empty()) {
        kSetTurn(next, ms);
        kTurnNotice();
    }
}
static void kApplyEnd(const std::string& winPid, int score) {
    K.on = false;
    K.cur.clear();
    if (GPlayer* p = findP(winPid)) p->score = score;
    K.winner = winPid.empty() ? L"없음" : pname(winPid);
    gmsg(L"끝말잇기 종료! 우승: " + K.winner + (winPid.empty() ? L"" : L" (+50점)"));
}

// ── 방장 전용: 판정하고 모두에게 알림 ──
static void hostStartK() {
    if (GP.size() < 2 || K.on) return;
    dict();   // 사전을 미리 불러 둠 (첫 단어 검사 때 멈칫하지 않도록)
    std::shuffle(GP.begin(), GP.end(), g_rng);
    for (auto& p : GP) p.alive = true;
    int n = (int)(sizeof(START_WORDS) / sizeof(START_WORDS[0]));
    std::wstring w = START_WORDS[std::uniform_int_distribution<int>(0, n - 1)(g_rng)];
    grpSendRoster();
    sendMsg("KSTART", U8(w) + "|" + GP[0].pid + "|15000");
    kApplyStart(w, GP[0].pid, 15000);
}
static void hostWord(const std::string& pid, const std::wstring& w) {
    if (!K.on || pid != K.cur) return;
    std::wstring why = checkWord(w);
    if (!why.empty()) {
        sendMsg("DENY", pid + "|" + U8(w) + "|" + U8(why));
        kApplyDeny(pid, w, why);
        return;
    }
    GPlayer* p = findP(pid);
    int score = (p ? p->score : 0) + (int)w.size() * 10;
    int ms = std::max(6000, K.turnMs - 500);   // 단어가 이어질수록 제한 시간이 줄어듦
    std::string next = nextAlive(pid);
    sendMsg("ACCEPT", pid + "|" + U8(w) + "|" + next + "|" + std::to_string(ms) + "|" + std::to_string(score));
    kApplyAccept(pid, w, next, ms, score);
}
static void hostOut(const std::string& pid, const std::wstring& why) {
    if (!K.on) return;
    GPlayer* p = findP(pid);
    if (!p || !p->alive) return;
    p->alive = false;
    std::string next = K.cur;
    int ms = (int)std::max<LONGLONG>(1000, (LONGLONG)K.deadline - (LONGLONG)GetTickCount64());
    if (pid == K.cur) { next = aliveCount() >= 2 ? nextAlive(pid) : ""; ms = K.turnMs; }
    if (aliveCount() < 2) next = "";
    sendMsg("OUT", pid + "|" + next + "|" + std::to_string(ms) + "|" + U8(why));
    kApplyOut(pid, next, ms, why);
    if (aliveCount() < 2) {
        std::string win;
        for (auto& q : GP)
            if (q.alive) win = q.pid;
        GPlayer* wp = findP(win);
        int score = wp ? wp->score + 50 : 0;
        sendMsg("KEND", win + "|" + std::to_string(score));
        kApplyEnd(win, score);
    }
    grpSendRoster();
}
static void groupHostTick() {
    if (g_isHost && K.on && !K.cur.empty() && GetTickCount64() > K.deadline + 300) hostOut(K.cur, L"시간 초과");
}
static void groupSys(const std::wstring& s) {   // 방장이 모두에게 알림
    sendMsg("SYS", U8(s));
    sys(s);
}
static void submitWord(const std::wstring& w) {
    if (g_isHost) hostWord(g_pid, w);
    else sendMsg("WORD", U8(w));
}

// ── 단체 방: 공동 점수판 (방장만 점수를 바꾸고 명단으로 알림) ──
static void hostAward(const std::vector<std::string>& pids, int pts) {
    if (!g_isHost || pids.empty()) return;
    for (auto& id : pids)
        if (GPlayer* p = findP(id)) p->score += pts;
    grpSendRoster();
}
template <class M> static bool everyoneIn(const M& m) {
    if (GP.size() < 2) return false;
    for (auto& p : GP)
        if (!m.count(p.pid)) return false;
    return true;
}
static bool inList(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}
static std::wstring groupSub() {
    return L"단체 방 " + W(g_code) + L" · " + I((int)GP.size()) + L"/" + I(GROUP_MAX) + L"명" + (g_isHost ? L" · 내가 방장" : L"");
}

// ── 다 같이 가위바위보: 모두 내면 공개. 손 모양이 2가지만 나왔을 때 이긴 쪽 전원 +10점 ──
static struct {
    std::map<std::string, int> picks, last;
    std::vector<std::string> winners;
    bool has = false;
} GR;
static Btn grpsBtn(int i) { return {250 + i * 128, 104, 116, 130}; }
static void grpsCheck() {
    if (!everyoneIn(GR.picks)) return;
    std::set<int> kinds;
    GR.last.clear();
    for (auto& p : GP) {
        kinds.insert(GR.picks[p.pid]);
        GR.last[p.pid] = GR.picks[p.pid];
    }
    GR.winners.clear();
    if (kinds.size() == 2) {
        int a = *kinds.begin(), b = *kinds.rbegin();
        int w = (a == 0 && b == 1) ? 1 : (a == 1 && b == 2) ? 2 : 0;   // 바위>가위, 보>바위, 가위>보
        for (auto& p : GP)
            if (GR.picks[p.pid] == w) GR.winners.push_back(p.pid);
    }
    GR.has = true;
    GR.picks.clear();
    std::wstring s;
    for (auto& p : GP) s += (s.empty() ? L"" : L", ") + pname(p.pid) + L" " + RPS_N[GR.last[p.pid]];
    std::wstring w;
    for (auto& id : GR.winners) w += (w.empty() ? L"" : L", ") + pname(id);
    logLine(L"[가위바위보] " + s + L" → " + (w.empty() ? std::wstring(L"무승부!") : L"승리: " + w + L" (+10점)"));
    hostAward(GR.winners, 10);
}
static void grpsPick(int c) {
    if (GR.picks.count(g_pid) || GP.size() < 2) return;
    GR.picks[g_pid] = c;
    sendMsg("GRPS", std::to_string(c));
    grpsCheck();
}

// ── 다 같이 주사위: 모두 굴리면 합이 가장 큰 사람(들) +10점 ──
static struct {
    std::map<std::string, std::pair<int, int>> rolls, last;
    std::vector<std::string> winners;
    bool has = false;
} GD;
static const Btn gdiceBtn{250, 100, 170, 56};
static void gdiceCheck() {
    if (!everyoneIn(GD.rolls)) return;
    int best = -1;
    GD.last.clear();
    for (auto& p : GP) {
        auto r = GD.rolls[p.pid];
        GD.last[p.pid] = r;
        best = std::max(best, r.first + r.second);
    }
    GD.winners.clear();
    for (auto& p : GP)
        if (GD.last[p.pid].first + GD.last[p.pid].second == best) GD.winners.push_back(p.pid);
    GD.has = true;
    GD.rolls.clear();
    std::wstring w;
    for (auto& id : GD.winners) w += (w.empty() ? L"" : L", ") + pname(id);
    logLine(L"[주사위] 최고 " + I(best) + L" — 승리: " + w + L" (+10점)");
    hostAward(GD.winners, 10);
}
static void gdiceRoll() {
    if (GD.rolls.count(g_pid) || GP.size() < 2) return;
    std::uniform_int_distribution<int> d(1, 6);
    int a = d(g_rng), b = d(g_rng);
    GD.rolls[g_pid] = {a, b};
    sendMsg("GDICE", std::to_string(a) + "|" + std::to_string(b));
    gdiceCheck();
}

// ── 다 같이 업다운: 한 명이 출제, 나머지가 채팅창에 숫자 입력. 가장 먼저 맞힌 사람 +10점 ──
struct GuEntry {
    std::wstring name;
    int n;
    std::string ans;
};
static struct {
    bool on = false;
    std::string setter;
    int answer = 0, lastAnswer = 0;
    std::wstring winner;
    std::vector<GuEntry> log;
} GU;
static const Btn gudBtn{315, 100, 240, 54};
static void gudApply(const std::string& pid, int n, const std::string& ans) {
    GU.log.push_back({pname(pid), n, ans});
    if (ans == "OK") {
        GU.on = false;
        GU.winner = pname(pid);
        GU.lastAnswer = n;
        logLine(L"[업다운] " + pname(pid) + L" 정답! 숫자는 " + I(n) + L" (+10점)");
        if (g_isHost) hostAward({pid}, 10);
    }
}
static void gudAnswer(const std::string& pid, int n) {   // 출제자만 호출
    std::string ans = n < GU.answer ? "UP" : n > GU.answer ? "DOWN" : "OK";
    sendMsg("GUD_ANS", pid + "|" + std::to_string(n) + "|" + ans);
    gudApply(pid, n, ans);
}
static void gudStart() {
    if (GU.on || GP.size() < 2) return;
    GU.on = true;
    GU.setter = g_pid;
    GU.answer = std::uniform_int_distribution<int>(1, 100)(g_rng);
    GU.log.clear();
    GU.winner.clear();
    sendMsg("GUD_START");
    logLine(L"[업다운] 문제를 냈습니다! 정답은 " + I(GU.answer) + L" (다른 사람들이 맞히는 중)");
}

// ── 2인 게임 대결: 신청 → 수락 → 둘이 플레이, 결과는 방 전체에 알림 ──
static std::string g_chalTo, g_chalFrom;   // 내가 신청한 상대 / 나에게 신청한 사람
static int g_chalView = -1, g_chalInView = -1;
static ULONGLONG g_chalAt = 0, g_chalInAt = 0;
static std::map<std::string, std::string> g_pairs;   // 대결 중인 사람 → 상대 (다른 사람들 것 포함)
static const Btn chalYes{150, 262, 160, 46}, chalNo{330, 262, 160, 46};
static HBRUSH g_winBrush;

static void updateStrip() {
    std::wstring s;
    if (g_ended) s = L"연결이 끊어졌습니다.";
    else if (!g_connected) s = L"";
    else if (!g_group)
        s = L"1:1 상대: " + g_peer + (g_relay ? L"    ·    방 " + W(g_code) + L" (중계 서버)" : std::wstring(L"    ·    IP 직접 연결"));
    else if (!g_matchPid.empty())
        s = L"▶ 대결 상대: " + g_peer + L"   ·   틱택토·오목·윷놀이·격투 함께 가능";
    else
        s = L"단체 방 " + W(g_code) + L"   ·   2인 게임: 탭 → 상대 선택 → 대결 신청";
    SetWindowTextW(hStrip, s.c_str());
    ShowWindow(hStripBtn, g_group && !g_matchPid.empty() && !g_ended ? SW_SHOW : SW_HIDE);
}
static void layoutTabs() {
    g_tabs = g_group ? TABS_G : TABS_1;
    g_tabCount = g_group ? (int)(sizeof(TABS_G) / sizeof(int)) : (int)(sizeof(TABS_1) / sizeof(int));
    g_btnW = g_group ? 76 : 101;
    g_btnStep = g_group ? 80 : 107;
    for (int i = 0; i < V_COUNT; i++) {
        if (i < g_tabCount) {
            SetWindowTextW(hGameBtn[i], VIEW_NAMES[g_tabs[i]]);
            g_ctl[hGameBtn[i]] = {CX + i * g_btnStep, 8, g_btnW, 34, g_group ? 2 : 0};
            placeCtl(hGameBtn[i]);
            ShowWindow(hGameBtn[i], SW_SHOW);
        } else {
            ShowWindow(hGameBtn[i], SW_HIDE);
        }
    }
}
static void pairReset() {
    T.on = false;
    T.result.clear();
    memset(T.b, 0, sizeof(T.b));
    O.on = false;
    O.result.clear();
    memset(O.b, 0, sizeof(O.b));
    O.moves = 0;
    O.last = -1;
    Y.on = false;
    Y.winner = -1;
    Y.last = 0;
    Y.pending.clear();
    Y.note.clear();
    F.on = false;
    B.bb = 0;
    UD.ud = 0;
}
static void setMatch(const std::string& pid) {
    g_matchPid = pid;
    g_peer = pname(pid);
    pairReset();
    updateStrip();
}
static void endMatchLocal(const std::wstring& why) {
    if (g_matchPid.empty()) return;
    g_pairs.erase(g_pid);
    g_pairs.erase(g_matchPid);
    g_matchPid.clear();
    g_peer = L"상대";
    pairReset();
    updateStrip();
    sys(why);
    invalidateCanvas();
}
static void startPairGame(int view) {
    switch (view) {
        case V_TTT: tttStart(); break;
        case V_OMOK: omStart(); break;
        case V_YUT: yutStart(); break;
        case V_FIGHT: fightStart(); break;
    }
}
static void sendChallenge(const std::string& pid, int view) {
    if (pid == g_matchPid) { setView(view); return; }
    if (!g_matchPid.empty()) {
        sendMsg("MEND", g_matchPid);
        endMatchLocal(L"이전 대결을 끝냈습니다.");
    }
    g_chalTo = pid;
    g_chalView = view;
    g_chalAt = GetTickCount64();
    sendMsg("CHAL", pid + "|" + std::to_string(view));
    sys(pname(pid) + L" 님에게 " + VIEW_NAMES[view] + L" 대결을 신청했습니다. 응답을 기다리는 중...");
}
static void acceptChal() {
    std::string from = g_chalFrom;
    int view = g_chalInView;
    g_chalFrom.clear();
    if (!findP(from) || view < 0 || view >= V_COUNT) return;
    if (!g_matchPid.empty() && g_matchPid != from) {
        sendMsg("MEND", g_matchPid);
        endMatchLocal(L"이전 대결을 끝냈습니다.");
    }
    sendMsg("CHAL_OK", from + "|" + std::to_string(view));
    g_pairs[from] = g_pid;
    g_pairs[g_pid] = from;
    setMatch(from);
    setView(view);
    sys(pname(from) + L" 님과 " + VIEW_NAMES[view] + L" 대결 시작!");
    SetFocus(g_hwnd);
}
static void declineChal() {
    sendMsg("CHAL_NO", g_chalFrom + "|거절했어요");
    g_chalFrom.clear();
}
static void matchResult(const std::wstring& game, int res) {
    if (!g_group || g_matchPid.empty()) return;
    std::wstring t;
    if (res == 1) {
        t = game + L": " + g_myName + L" 승리! (vs " + g_peer + L") +20점";
        sendMsg("MRES", "win|" + U8(t));
        if (g_isHost) hostAward({g_pid}, 20);
    } else if (res == 0 && g_pid < g_matchPid) {   // 무승부는 한쪽만 알림
        t = game + L": " + g_myName + L" vs " + g_peer + L" 무승부";
        sendMsg("MRES", "draw|" + U8(t));
    }
    if (!t.empty()) logLine(L"[대결] " + t);
}
static bool isPairMsg(const std::string& t) {
    for (const char* p : {"TTT_", "OMOK_", "YUT_", "BB_", "UD_"})
        if (t.rfind(p, 0) == 0) return true;
    return t == "FIGHT_START" || t == "FS" || t == "FHIT" || t == "FKO";
}

// 명단이 바뀌었을 때 (누가 들어오거나 나감)
static void onRosterChanged() {
    if (!g_matchPid.empty() && !findP(g_matchPid)) endMatchLocal(L"대결 상대가 방을 나갔습니다.");
    if (!g_chalTo.empty() && !findP(g_chalTo)) g_chalTo.clear();
    if (!g_chalFrom.empty() && !findP(g_chalFrom)) g_chalFrom.clear();
    for (auto it = g_pairs.begin(); it != g_pairs.end();)
        it = (findP(it->first) && findP(it->second)) ? std::next(it) : g_pairs.erase(it);
    if (GU.on && !findP(GU.setter)) {
        GU.on = false;
        logLine(L"[업다운] 출제자가 나가서 문제가 취소됐습니다.");
    }
    grpsCheck();
    gdiceCheck();
}
static void groupTick() {
    groupHostTick();
    ULONGLONG now = GetTickCount64();
    if (!g_chalTo.empty() && now - g_chalAt > 30000) {
        sys(pname(g_chalTo) + L" 님이 응답하지 않아 대결 신청을 취소했습니다.");
        g_chalTo.clear();
        invalidateCanvas();
    }
    if (!g_chalFrom.empty() && now - g_chalInAt > 30000) {
        g_chalFrom.clear();
        invalidateCanvas();
    }
}
static void printGroupHelp() {
    logLine(L"──── 단체 방 도움말 ────");
    logLine(L"· 끝말잇기·가위바위보·주사위·업다운: 방 전체가 함께");
    logLine(L"· 틱택토·오목·윷놀이·격투: 탭에서 상대를 골라 대결 신청");
    logLine(L"· 업다운 탭: 출제되면 채팅창에 숫자만 입력");
    logLine(L"· 대결 중에는 /bb (숫자야구), /ud (업다운) 명령도 가능");
    logLine(L"· 승리하면 공동 점수판에 점수가 쌓여요");
    logLine(L"──────────────────");
}

// ── 단체 방 메시지 처리 ──  형식: 보낸사람ID|종류|내용
static void handlePeer(const std::string& line);
static void handleGroupNet(const std::string& line) {
    size_t p1 = line.find('|');
    if (p1 == std::string::npos) return;
    size_t p2 = line.find('|', p1 + 1);
    std::string from = line.substr(0, p1);
    std::string type = line.substr(p1 + 1, p2 == std::string::npos ? std::string::npos : p2 - p1 - 1);
    std::string payload = p2 == std::string::npos ? "" : line.substr(p2 + 1);
    if (from == g_pid) return;   // 내가 보낸 메시지가 되돌아온 것
    auto f = split(payload, '|');
    auto fs = [&](size_t i) { return i < f.size() ? f[i] : std::string(); };
    auto fi = [&](size_t i) { return i < f.size() ? atoi(f[i].c_str()) : 0; };

    // 모두 공통 (방장 포함)
    if (g_connected && !g_ended) {
        if (!g_matchPid.empty() && from == g_matchPid && isPairMsg(type)) {   // 대결 상대의 2인 게임 메시지
            handlePeer(type + "|" + payload);
            return;
        }
        if (type == "CHAL") {
            int v = fi(1);
            if (fs(0) == g_pid && findP(from) && v >= 0 && v < V_COUNT) {
                g_chalFrom = from;
                g_chalInView = v;
                g_chalInAt = GetTickCount64();
                logLine(L"[대결] " + pname(from) + L" 님이 " + VIEW_NAMES[v] + L" 대결을 신청했습니다! 게임 화면에서 수락/거절하세요.");
                FlashWindow(g_hwnd, TRUE);
            }
            return;
        }
        if (type == "CHAL_OK") {
            std::string ch = fs(0);
            int v = fi(1);
            if (v < 0 || v >= V_COUNT) return;
            if (ch == g_pid && g_chalTo == from) {
                g_chalTo.clear();
                if (!g_matchPid.empty() && g_matchPid != from) {
                    sendMsg("MEND", g_matchPid);
                    endMatchLocal(L"이전 대결을 끝냈습니다.");
                }
                g_pairs[from] = g_pid;
                g_pairs[g_pid] = from;
                setMatch(from);
                setView(v);
                sys(pname(from) + L" 님이 수락! " + VIEW_NAMES[v] + L" 대결 시작!");
                startPairGame(v);
            } else if (ch != g_pid) {
                g_pairs[from] = ch;
                g_pairs[ch] = from;
                logLine(L"[대결] " + pname(ch) + L" vs " + pname(from) + L" — " + VIEW_NAMES[v] + L" 대결 시작!");
            }
            return;
        }
        if (type == "CHAL_NO") {
            if (fs(0) == g_pid && g_chalTo == from) {
                g_chalTo.clear();
                sys(pname(from) + L" 님이 대결 신청을 거절했습니다.");
            }
            return;
        }
        if (type == "MEND") {
            std::string o = fs(0);
            g_pairs.erase(from);
            g_pairs.erase(o);
            if (o == g_pid && g_matchPid == from) endMatchLocal(pname(from) + L" 님이 대결을 끝냈습니다.");
            return;
        }
        if (type == "MRES") {
            logLine(L"[대결] " + W(fs(1)));
            if (g_isHost && fs(0) == "win") hostAward({from}, 20);
            return;
        }
        if (type == "GRPS") { GR.picks[from] = fi(0); grpsCheck(); return; }
        if (type == "GDICE") { GD.rolls[from] = {fi(0), fi(1)}; gdiceCheck(); return; }
        if (type == "GUD_START") {
            if (GU.on && GU.setter < from) return;   // 동시에 냈으면 ID가 작은 쪽 문제로 진행
            if (GU.on && GU.setter == g_pid) sys(L"동시에 문제를 내서 " + pname(from) + L" 님 문제로 진행합니다.");
            GU.on = true;
            GU.setter = from;
            GU.answer = 0;
            GU.log.clear();
            GU.winner.clear();
            logLine(L"[업다운] " + pname(from) + L" 님이 1~100 숫자 문제를 냈습니다! 업다운 탭에서 채팅창에 숫자를 입력하세요.");
            return;
        }
        if (type == "GUD_GUESS") { if (GU.on && GU.setter == g_pid) gudAnswer(from, fi(0)); return; }
        if (type == "GUD_ANS") { if (GU.on && from == GU.setter) gudApply(fs(0), fi(1), fs(2)); return; }
    }

    if (g_isHost) {
        if (type == "JOIN_REQ") {
            if (findP(from)) { grpSendRoster(); return; }   // 재전송된 요청
            if ((int)GP.size() >= GROUP_MAX) { sendMsg("REJECT", from + "|방이 꽉 찼어요 (최대 8명)"); return; }
            GPlayer np;
            np.pid = from;
            np.name = safeName(W(payload));
            if (np.name.empty()) np.name = L"플레이어";
            GP.push_back(np);
            grpSendRoster();
            groupSys(np.name + L" 님이 들어왔습니다." + (K.on ? L" (끝말잇기는 다음 판부터)" : L""));
            onRosterChanged();
        } else if (type == "LEAVE") {
            GPlayer* p = findP(from);
            if (!p) return;
            std::wstring nm = p->name;
            hostOut(from, L"나감");
            GP.erase(std::remove_if(GP.begin(), GP.end(), [&](const GPlayer& q) { return q.pid == from; }), GP.end());
            grpSendRoster();
            groupSys(nm + L" 님이 나갔습니다.");
            onRosterChanged();
        } else if (type == "WORD") {
            hostWord(from, W(payload));
        } else if (type == "CHAT" && findP(from)) {
            logLine(L"[" + pname(from) + L"] " + W(payload));
        }
        return;
    }

    // 참가자
    if (type == "ROSTER") {
        g_hostPid = from;
        grpParseRoster(payload);
        if (!g_connected && findP(g_pid)) {
            KillTimer(g_hwnd, TIMER_JOIN);
            onConnected();
        }
        onRosterChanged();
        return;
    }
    if (!g_connected) {
        if (type == "REJECT" && fs(0) == g_pid) relayFail(L"입장할 수 없어요: " + W(fs(1)));
        return;
    }
    if (type == "CHAT") { logLine(L"[" + pname(from) + L"] " + W(payload)); return; }
    if (from != g_hostPid) return;   // 아래는 방장만 보낼 수 있는 메시지
    if (type == "SYS") sys(W(payload));
    else if (type == "KSTART") kApplyStart(W(fs(0)), fs(1), fi(2));
    else if (type == "ACCEPT") kApplyAccept(fs(0), W(fs(1)), fs(2), fi(3), fi(4));
    else if (type == "DENY") kApplyDeny(fs(0), W(fs(1)), W(fs(2)));
    else if (type == "OUT") kApplyOut(fs(0), fs(1), fi(2), W(fs(3)));
    else if (type == "KEND") kApplyEnd(fs(0), fi(1));
    else if (type == "CLOSE" || type == "LEAVE") onDisconnected(L"방장이 방을 닫았습니다.");
}

// ── 단체 방 화면 ──
static void drawPlayerPanel(HDC dc, int view) {
    roundRect(dc, 15, 75, 205, 395, RGB(255, 255, 255), RGB(210, 214, 228), 18, 2);
    text(dc, 28, 82, 180, 24, L"참가자 · 점수", 15, COL_SUB, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    int y = 110;
    for (auto& p : GP) {
        bool cur = view == V_WORD && K.on && p.pid == K.cur;
        bool out = view == V_WORD && K.on && !p.alive;
        if (cur) roundRect(dc, 22, y - 2, 191, 40, RGB(255, 236, 200), COL_ACC, 12, 2);
        COLORREF nc = out ? RGB(175, 178, 188) : p.pid == g_pid ? COL_ME : COL_TEXT;
        std::wstring nm = (p.pid == g_hostPid ? L"★ " : L"") + p.name + (p.pid == g_pid ? L" (나)" : L"");
        std::wstring mark;
        if (view == V_RPS && GR.picks.count(p.pid)) mark = L"✓ ";
        if (view == V_DICE && GD.rolls.count(p.pid)) mark = L"✓ ";
        if (view == V_GUD && GU.on && p.pid == GU.setter) mark = L"출제 ";
        text(dc, 30, y, 105, 36, nm, 15, nc, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        text(dc, 132, y, 76, 36, out ? std::wstring(L"탈락") : mark + I(p.score) + L"점", 13, mark.empty() ? nc : COL_WINC,
             !mark.empty(), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        y += 44;
    }
}
static void drawWord(HDC dc) {
    title(dc, L"끝말잇기", groupSub());
    button(dc, grpCopyBtn, L"코드 복사", true, RGB(150, 155, 170), 14);
    drawPlayerPanel(dc, V_WORD);
    int rx = 240, rw = 390;
    if (K.on) {
        text(dc, rx, 76, rw, 22, L"현재 단어", 14, COL_SUB, true);
        roundRect(dc, rx, 100, rw, 90, RGB(255, 255, 255), RGB(210, 214, 228), 18, 2);
        text(dc, rx, 100, rw, 90, K.last, 42, COL_TEXT, true);
        wchar_t lc = K.last.back(), d = dueum(lc);
        std::wstring need = std::wstring(L"'") + lc + L"'" + (d != lc ? std::wstring(L" 또는 '") + d + L"'" : L"") +
                            L"(으)로 시작하는 단어";
        text(dc, rx, 196, rw, 26, need, 17, darker(COL_ACC, 0.85), true);
        bool mine = K.cur == g_pid;
        text(dc, rx, 226, rw, 30, mine ? L"▶ 내 차례! 채팅창에 단어를 입력하세요" : L"▶ " + pname(K.cur) + L" 차례", 18,
             mine ? COL_ME : COL_TEXT, true);
        LONGLONG rem = std::max<LONGLONG>(0, (LONGLONG)K.deadline - (LONGLONG)GetTickCount64());
        int bw = (int)std::min<LONGLONG>(rw - 4, (rw - 4) * rem / std::max(1, K.turnMs));
        roundRect(dc, rx, 262, rw, 18, RGB(235, 237, 243), RGB(210, 214, 228), 10, 1);
        if (bw > 0) fillRect(dc, rx + 2, 264, bw, 14, rem < 4000 ? COL_OP : COL_WINC);
        text(dc, rx, 282, rw, 20, L"남은 시간 " + I((int)((rem + 999) / 1000)) + L"초", 13, COL_SUB);
        text(dc, rx, 304, rw, 22, K.msg, 14, COL_TEXT, false, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        text(dc, rx, 332, rw, 20, L"지나온 단어", 13, COL_SUB, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int hy = 354, n = 0;
        for (int i = (int)K.hist.size() - 1; i >= 0 && n < 5; i--, n++) {
            text(dc, rx + 8, hy, rw - 16, 22, K.hist[i].second + L"    · " + K.hist[i].first, 15, n == 0 ? COL_TEXT : COL_SUB,
                 n == 0, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            hy += 23;
        }
    } else {
        roundRect(dc, rx, 76, rw, 118, RGB(255, 255, 255), RGB(210, 214, 228), 18, 2);
        text(dc, rx, 82, rw, 22, L"방 코드 (친구들에게 알려주세요)", 14, COL_SUB, true);
        text(dc, rx, 104, rw, 80, W(g_code), 50, COL_ME, true);
        if (!K.winner.empty()) text(dc, rx, 200, rw, 40, L"우승: " + K.winner, 26, COL_WINC, true);
        text(dc, rx + 10, 248, rw - 20, 150,
             L"· 차례가 되면 앞 단어의 끝 글자로 시작하는 단어를 채팅창에 입력\n"
             L"· 두음법칙 허용 (예: 력 → 역, 라 → 나)\n"
             L"· 제한 시간 안에 못 내면 탈락, 마지막 1명이 우승 (+50점)\n"
             L"· 단어 글자 수 × 10점, 이어질수록 제한 시간 감소\n"
             L"· 표준국어대사전 명사만 인정 (약 19만 개)",
             14, COL_TEXT, false, DT_LEFT | DT_WORDBREAK);
        if (g_isHost) button(dc, grpStartBtn, GP.size() >= 2 ? L"게임 시작" : L"2명 이상 필요", GP.size() >= 2, COL_ME, 20);
        else text(dc, rx, 405, rw, 50, L"방장이 시작하길 기다리는 중...", 17, COL_SUB, true);
    }
}
static void drawGRps(HDC dc) {
    int n = 0;
    for (auto& p : GP) n += (int)GR.picks.count(p.pid);
    bool picked = GR.picks.count(g_pid) > 0;
    title(dc, L"가위바위보 (다 같이)", groupSub());
    drawPlayerPanel(dc, V_RPS);
    int rx = 240, rw = 390;
    std::wstring cnt = L"(" + I(n) + L"/" + I((int)GP.size()) + L"명 선택 완료)";
    text(dc, rx, 76, rw, 24, picked ? L"선택 완료! 다른 사람을 기다리는 중 " + cnt : L"하나를 고르세요 " + cnt, 15,
         picked ? COL_SUB : COL_TEXT, true);
    for (int i = 0; i < 3; i++) {
        Btn b = grpsBtn(i);
        bool sel = picked && GR.picks[g_pid] == i;
        roundRect(dc, b.x, b.y, b.w, b.h, sel ? RGB(255, 236, 200) : RGB(255, 255, 255), sel ? COL_ACC : RGB(200, 205, 220), 20,
                  sel ? 4 : 2);
        text(dc, b.x, b.y + 8, b.w, 80, RPS_E[i], 48, COL_TEXT, false, DT_CENTER | DT_VCENTER | DT_SINGLELINE, true);
        text(dc, b.x, b.y + 90, b.w, 32, RPS_N[i], 20, COL_TEXT, true);
    }
    if (GR.has) {
        bool draw = GR.winners.empty();
        text(dc, rx, 248, rw, 22, L"지난 판 결과", 14, COL_SUB, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        text(dc, rx, 248, rw, 22, draw ? L"무승부!" : L"이긴 사람 +10점", 14, draw ? COL_ACC : COL_WINC, true,
             DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        int y = 274;
        for (auto& p : GP) {
            if (!GR.last.count(p.pid)) continue;
            bool win = inList(GR.winners, p.pid);
            int c = GR.last[p.pid];
            text(dc, rx + 10, y, 200, 22, pname(p.pid), 15, win ? COL_WINC : COL_TEXT, win, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            text(dc, rx + 210, y, 170, 22, std::wstring(RPS_N[c]) + (win ? L"  승리" : L""), 15, win ? COL_WINC : COL_TEXT, win,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            y += 24;
        }
    }
    if (GP.size() < 2) text(dc, rx, 440, rw, 24, L"2명 이상 있어야 할 수 있어요", 14, COL_OP, true);
}
static void drawGDice(HDC dc) {
    int n = 0;
    for (auto& p : GP) n += (int)GD.rolls.count(p.pid);
    bool rolled = GD.rolls.count(g_pid) > 0;
    title(dc, L"주사위 (다 같이)", groupSub());
    drawPlayerPanel(dc, V_DICE);
    int rx = 240, rw = 390;
    text(dc, rx, 72, rw, 24, L"모두 굴리면 합이 가장 큰 사람 +10점  (" + I(n) + L"/" + I((int)GP.size()) + L"명 굴림)", 14,
         COL_SUB, true);
    button(dc, gdiceBtn, L"🎲 굴리기", !rolled && GP.size() >= 2, COL_ME, 20);
    if (rolled) {
        dieFace(dc, 440, 96, 64, GD.rolls[g_pid].first, COL_ME);
        dieFace(dc, 516, 96, 64, GD.rolls[g_pid].second, COL_ME);
    }
    if (GD.has) {
        text(dc, rx, 176, rw, 22, L"지난 판 결과", 14, COL_SUB, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int y = 202;
        for (auto& p : GP) {
            if (!GD.last.count(p.pid)) continue;
            bool win = inList(GD.winners, p.pid);
            auto r = GD.last[p.pid];
            text(dc, rx + 10, y, 190, 24, pname(p.pid), 15, win ? COL_WINC : COL_TEXT, win, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            text(dc, rx + 200, y, 180, 24,
                 I(r.first) + L" + " + I(r.second) + L" = " + I(r.first + r.second) + (win ? L"  승리" : L""), 15,
                 win ? COL_WINC : COL_TEXT, win, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            y += 26;
        }
    }
    if (GP.size() < 2) text(dc, rx, 440, rw, 24, L"2명 이상 있어야 할 수 있어요", 14, COL_OP, true);
}
static void drawGud(HDC dc) {
    title(dc, L"업다운 (다 같이)", groupSub());
    drawPlayerPanel(dc, V_GUD);
    int rx = 240, rw = 390;
    if (!GU.on) {
        button(dc, gudBtn, L"문제 내기 (1~100 랜덤)", GP.size() >= 2, COL_ME, 17);
        if (!GU.winner.empty())
            text(dc, rx, 170, rw, 34, L"지난 판: " + GU.winner + L" 정답! (" + I(GU.lastAnswer) + L")", 20, COL_WINC, true);
        text(dc, rx + 10, 220, rw - 20, 120,
             L"· 한 명이 문제를 내면 나머지가 채팅창에 숫자만 입력해서 맞혀요\n"
             L"· 출제자 프로그램이 자동으로 UP / DOWN을 알려줘요\n"
             L"· 가장 먼저 맞힌 사람 +10점",
             15, COL_TEXT, false, DT_LEFT | DT_WORDBREAK);
        return;
    }
    bool setter = GU.setter == g_pid;
    if (setter) {
        text(dc, rx, 76, rw, 30, L"내가 낸 문제 — 정답은 " + I(GU.answer), 20, COL_ME, true);
        text(dc, rx, 108, rw, 24, L"다른 사람들이 맞히는 중이에요...", 15, COL_SUB, true);
    } else {
        text(dc, rx, 76, rw, 30, pname(GU.setter) + L" 님이 낸 1~100 숫자를 맞혀보세요!", 18, COL_TEXT, true);
        text(dc, rx, 108, rw, 24, L"채팅창에 숫자만 입력하고 Enter", 15, COL_ME, true);
    }
    int lo = 1, hi = 100;
    for (auto& e : GU.log) {
        if (e.ans == "UP") lo = std::max(lo, e.n + 1);
        if (e.ans == "DOWN") hi = std::min(hi, e.n - 1);
    }
    roundRect(dc, rx + 70, 140, rw - 140, 56, RGB(255, 255, 255), RGB(210, 214, 228), 16, 2);
    text(dc, rx + 70, 140, rw - 140, 56, L"범위  " + I(lo) + L" ~ " + I(hi), 26, COL_TEXT, true);
    text(dc, rx, 210, rw, 22, L"추측 기록", 13, COL_SUB, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    int y = 234, n = 0;
    for (int i = (int)GU.log.size() - 1; i >= 0 && n < 9; i--, n++) {
        auto& e = GU.log[i];
        std::wstring a = e.ans == "UP" ? L"UP ▲" : e.ans == "DOWN" ? L"DOWN ▼" : L"정답!";
        text(dc, rx + 10, y, rw - 20, 24, e.name + L":  " + I(e.n) + L"  →  " + a, 15, n == 0 ? COL_TEXT : COL_SUB, n == 0,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        y += 25;
    }
}
static Btn pickBtn(int i) { return {40 + (i % 2) * 290, 96 + (i / 2) * 72, 270, 60}; }
static std::vector<const GPlayer*> others() {
    std::vector<const GPlayer*> v;
    for (auto& p : GP)
        if (p.pid != g_pid) v.push_back(&p);
    return v;
}
static void drawPicker(HDC dc, int view) {
    title(dc, VIEW_NAMES[view], L"2인 게임이에요. 대결할 상대를 골라 신청하세요. (승리 +20점, 결과는 방 전체에 알림)");
    auto ov = others();
    if (ov.empty()) {
        text(dc, 0, 180, CW, 40, L"다른 참가자가 없어요. 방 코드 " + W(g_code) + L" 를 친구에게 보내세요!", 18, COL_SUB, true);
        return;
    }
    for (int i = 0; i < (int)ov.size(); i++) {
        const GPlayer& p = *ov[i];
        Btn b = pickBtn(i);
        bool pending = g_chalTo == p.pid;
        bool busy = g_pairs.count(p.pid) > 0;
        roundRect(dc, b.x, b.y, b.w, b.h, pending ? RGB(255, 236, 200) : RGB(255, 255, 255), pending ? COL_ACC : RGB(200, 205, 220),
                  16, 2);
        text(dc, b.x + 16, b.y + 6, b.w - 32, 26, p.name, 18, COL_TEXT, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        std::wstring s = pending ? L"신청 중... (응답 기다리는 중)"
                         : busy  ? L"다른 대결 중 (vs " + pname(g_pairs[p.pid]) + L") · 그래도 신청 가능"
                                 : L"클릭해서 " + std::wstring(VIEW_NAMES[view]) + L" 대결 신청";
        text(dc, b.x + 16, b.y + 32, b.w - 32, 22, s, 13, pending ? darker(COL_ACC) : COL_SUB, false,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}
static void drawChalOverlay(HDC dc) {
    if (g_chalFrom.empty()) return;
    roundRect(dc, 110, 150, 420, 176, RGB(255, 255, 255), COL_ACC, 24, 4);
    text(dc, 110, 168, 420, 34, L"⚔ 대결 신청", 22, COL_TEXT, true);
    std::wstring v = g_chalInView >= 0 && g_chalInView < V_COUNT ? VIEW_NAMES[g_chalInView] : L"";
    text(dc, 110, 206, 420, 40, pname(g_chalFrom) + L" 님이 " + v + L" 대결을 신청했어요!", 17, COL_TEXT, false);
    button(dc, chalYes, L"수락", true, COL_WINC, 18);
    button(dc, chalNo, L"거절", true, RGB(150, 155, 170), 18);
}
static void drawPairView(HDC dc, int view) {
    switch (view) {
        case V_TTT: drawTtt(dc); break;
        case V_OMOK: drawOmok(dc); break;
        case V_YUT: drawYut(dc); break;
        case V_FIGHT: drawFight(dc); break;
    }
}
static void pairClick(int view, int x, int y) {
    switch (view) {
        case V_TTT:
            if (tttNew.hit(x, y)) tttStart();
            else if (x >= 170 && x < 470 && y >= 75 && y < 375) tttMove((y - 75) / 100 * 3 + (x - 170) / 100);
            break;
        case V_OMOK: omClick(x, y); break;
        case V_YUT: yutClick(x, y); break;
        case V_FIGHT:
            if ((!F.on || F.result) && fightBtn.hit(x, y)) fightStart();
            break;
    }
}
static void drawGroupCanvas(HDC dc) {
    switch (g_view) {
        case V_WORD: drawWord(dc); break;
        case V_RPS: drawGRps(dc); break;
        case V_DICE: drawGDice(dc); break;
        case V_GUD: drawGud(dc); break;
        default:
            if (!g_matchPid.empty()) drawPairView(dc, g_view);
            else drawPicker(dc, g_view);
    }
    drawChalOverlay(dc);
}
static void groupClick(int x, int y) {
    if (!g_chalFrom.empty()) {   // 대결 신청 창이 떠 있으면 그것부터
        if (chalYes.hit(x, y)) acceptChal();
        else if (chalNo.hit(x, y)) declineChal();
        invalidateCanvas();
        return;
    }
    switch (g_view) {
        case V_WORD:
            if (grpCopyBtn.hit(x, y)) {
                copyToClipboard(W(g_code));
                sys(L"방 코드 " + W(g_code) + L" 를 클립보드에 복사했습니다.");
            } else if (g_isHost && !K.on && grpStartBtn.hit(x, y)) {
                hostStartK();
            }
            break;
        case V_RPS:
            for (int i = 0; i < 3; i++)
                if (grpsBtn(i).hit(x, y)) grpsPick(i);
            break;
        case V_DICE:
            if (gdiceBtn.hit(x, y)) gdiceRoll();
            break;
        case V_GUD:
            if (gudBtn.hit(x, y)) gudStart();
            break;
        default:
            if (!g_matchPid.empty()) pairClick(g_view, x, y);
            else {
                auto ov = others();
                for (int i = 0; i < (int)ov.size(); i++)
                    if (pickBtn(i).hit(x, y)) sendChallenge(ov[i]->pid, g_view);
            }
    }
    invalidateCanvas();
}

static void handlePeer(const std::string& line) {
    size_t bar = line.find('|');
    std::string type = line.substr(0, bar);
    std::string payload = bar == std::string::npos ? "" : line.substr(bar + 1);
    auto f = split(payload, '|');
    auto fi = [&](size_t i) { return i < f.size() ? atoi(f[i].c_str()) : 0; };

    // 방 코드 모드 접속 절차
    if (type == "JOIN") {
        if (g_relay && g_isHost && !g_connected) { sendMsg("JOINED"); onConnected(); }
        return;
    }
    if (type == "JOINED") {
        if (g_relay && !g_isHost && !g_connected) { KillTimer(g_hwnd, TIMER_JOIN); onConnected(); }
        return;
    }
    if (!g_connected) return;
    if (type == "BYE" && g_relay) {
        onDisconnected(g_peer + L" 님이 나갔습니다.");
        return;
    }

    if (type == "HELLO") {
        g_peer = payload.empty() ? L"상대" : W(payload);
        sys(g_peer + L" 님과 연결되었습니다! 위쪽 버튼으로 게임을 고르세요. (/help)");
        updateStrip();
    } else if (type == "CHAT") {
        logLine(L"[" + g_peer + L"] " + W(payload));
    } else if (type == "BYE") {
        sys(g_peer + L" 님이 나갔습니다.");
    }
    // 가위바위보
    else if (type == "RPS") {
        R.peer = fi(0);
        if (R.my >= 0) rpsResolve();
        else gmsg(g_peer + L"가 가위바위보를 냈습니다!");
    }
    // 주사위
    else if (type == "DICE") {
        D.p1 = fi(0);
        D.p2 = fi(1);
        if (D.my1 >= 0) diceResolve();
        else gmsg(g_peer + L"가 주사위를 굴렸습니다!");
    }
    // 틱택토
    else if (type == "TTT_START") {
        if (T.on && T.moves == 0) T.my = tieFirst() ? 'X' : 'O';   // 동시에 시작하면 호스트가 X
        else tttReset('O');
        gmsg(g_peer + L"가 틱택토를 시작했습니다!");
        setView(V_TTT);
    } else if (type == "TTT_MOVE") {
        int p = fi(0);
        if (T.on && p >= 0 && p < 9 && !T.b[p] && T.turn != T.my) {
            T.b[p] = T.turn;
            T.moves++;
            T.turn = T.my;
            tttAfter();
        }
    } else if (type == "TTT_QUIT") {
        T.on = false;
    }
    // 오목
    else if (type == "OMOK_START") {
        if (O.on && O.moves == 0) O.my = tieFirst() ? 1 : 2;   // 동시에 시작하면 호스트가 흑
        else omReset(2);
        gmsg(g_peer + L"가 오목을 시작했습니다! 당신은 " + (O.my == 1 ? L"흑돌" : L"백돌"));
        setView(V_OMOK);
    } else if (type == "OMOK_MOVE") {
        int p = fi(0);
        if (O.on && p >= 0 && p < OM_N * OM_N && !O.b[p] && O.turn != O.my) omPlace(p, O.turn);
    } else if (type == "OMOK_QUIT") {
        if (O.on) {
            O.on = false;
            O.result = L"상대 기권!\n승리";
            matchResult(L"오목", 1);
            gmsg(g_peer + L"가 오목을 기권했습니다. 승리!");
        }
    }
    // 윷놀이
    else if (type == "YUT_START") {
        if (Y.on && Y.actions == 0) Y.mySeat = tieFirst() ? 0 : 1;
        else yutReset(1);
        gmsg(g_peer + L"가 윷놀이를 시작했습니다! " + g_peer + L"가 먼저 던집니다.");
        setView(V_YUT);
    } else if (type == "YUT_THROW") {
        if (Y.on && Y.turn != Y.mySeat && Y.throwsLeft > 0) yutApplyThrow(fi(0), f.size() > 1 ? f[1] : "");
    } else if (type == "YUT_MOVE") {
        if (Y.on && Y.turn != Y.mySeat) yutApplyMove(fi(0), fi(1));
    } else if (type == "YUT_QUIT") {
        if (Y.on) { Y.on = false; gmsg(g_peer + L"가 윷놀이를 그만뒀습니다."); invalidateCanvas(); }
    }
    // 격투
    else if (type == "FIGHT_START") {
        bool simultaneous = F.on && F.countdown > 120 && F.result == 0;
        fightReset(simultaneous ? tieFirst() : false);
        gmsg(g_peer + L"가 격투 대전을 신청했습니다! 게임 화면을 클릭하고 준비하세요.");
        setView(V_FIGHT);
    } else if (type == "FS") {
        if (F.on && f.size() >= 8) {
            F.op.x = (float)fi(0);
            F.op.y = (float)fi(1);
            F.op.facing = fi(2);
            F.op.act = fi(3);
            F.op.actT = fi(4);
            F.op.guard = fi(5) != 0;
            F.op.hp = fi(6);
            F.op.stun = fi(7);
        }
    } else if (type == "FHIT") {
        fightOnHit(fi(0), fi(1));
    } else if (type == "FKO") {
        if (F.on && F.result == 0) {
            F.result = 1;
            F.op.hp = 0;
            gmsg(L"격투 승리! K.O. 🎉");
            matchResult(L"격투", 1);
        }
    }
    // 숫자야구
    else if (type == "BB_START") {
        if (B.bb == 1) B.iStarted = tieFirst();
        else { gmsg(g_peer + L"가 숫자야구를 시작했습니다!"); bbStartLocal(false); }
    } else if (type == "BB_READY") {
        B.peerReady = true;
        if (!B.secret.empty()) bbBegin();
        else gmsg(g_peer + L"가 비밀 숫자를 정했습니다. /secret 으로 정하세요.");
    } else if (type == "BB_GUESS" && B.bb == 2 && validBB(payload)) {
        int s = 0, b = 0;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                if (payload[i] == B.secret[j]) (i == j ? s : b)++;
        sendMsg("BB_RESULT", payload + "|" + std::to_string(s) + "|" + std::to_string(b));
        std::wstring r = s == 0 && b == 0 ? L"OUT" : I(s) + L"S " + I(b) + L"B";
        gmsg(g_peer + L"의 추측 " + W(payload) + L" → " + r);
        if (s == 3) { gmsg(L"숫자야구 패배... 내 숫자는 " + W(B.secret)); B.bb = 0; }
        else { B.myTurn = true; gmsg(L"당신 차례! /g 123"); }
    } else if (type == "BB_RESULT" && B.bb == 2 && f.size() >= 3) {
        int s = fi(1), b = fi(2);
        std::wstring r = s == 0 && b == 0 ? L"OUT" : I(s) + L"S " + I(b) + L"B";
        gmsg(L"내 추측 " + W(f[0]) + L" → " + r);
        if (s == 3) { gmsg(L"숫자야구 승리! 🎉 " + I(B.tries) + L"번 만에 맞혔습니다."); B.bb = 0; }
    } else if (type == "BB_QUIT") {
        if (B.bb) { B.bb = 0; gmsg(g_peer + L"가 숫자야구를 중단했습니다."); }
    }
    // 업다운
    else if (type == "UD_START") {
        UD.ud = 2;
        gmsg(g_peer + L"가 1~100 숫자를 냈습니다! /u 50 처럼 맞혀보세요.");
    } else if (type == "UD_GUESS" && UD.ud == 1) {
        int g = fi(0);
        UD.tries++;
        std::string ans = g < UD.answer ? "UP" : g > UD.answer ? "DOWN" : "CORRECT";
        sendMsg("UD_ANS", std::to_string(g) + "|" + ans + "|" + std::to_string(UD.tries));
        gmsg(g_peer + L"의 추측 " + I(g) + L" → " + W(ans));
        if (ans == "CORRECT") { gmsg(g_peer + L"가 " + I(UD.tries) + L"번 만에 맞혔습니다!"); UD.ud = 0; }
    } else if (type == "UD_ANS" && UD.ud == 2 && f.size() >= 3) {
        if (f[1] == "CORRECT") { gmsg(L"정답! 🎉 " + W(f[0]) + L" — " + W(f[2]) + L"번 만에 맞혔습니다."); UD.ud = 0; }
        else gmsg(W(f[0]) + L" → " + (f[1] == "UP" ? L"UP ▲" : L"DOWN ▼"));
    } else if (type == "UD_QUIT") {
        if (UD.ud) { UD.ud = 0; gmsg(g_peer + L"가 업다운을 중단했습니다."); }
    }
    invalidateCanvas();
}
// 받은 메시지 한 줄: 단체 방이면 handleGroupNet, 1:1이면 handlePeer
static void handleNet(const std::string& line) {
    if (g_group) handleGroupNet(line);
    else handlePeer(line);
    invalidateCanvas();
}

// ═════════════════════════ 연결 ═════════════════════════
static void recvLoop(SOCKET s, unsigned gen) {
    std::string buf;
    char tmp[4096];
    for (;;) {
        int n = recv(s, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        buf.append(tmp, n);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string* line = new std::string(buf.substr(0, pos));
            buf.erase(0, pos + 1);
            if (line->empty() || !PostMessageW(g_hwnd, WM_NET, gen, (LPARAM)line)) delete line;
        }
    }
    PostMessageW(g_hwnd, WM_NETCLOSED, gen, 0);
}

static std::wstring localIPs() {
    std::wstring r;
    char hn[256];
    if (gethostname(hn, sizeof(hn)) != 0) return r;
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(hn, nullptr, &hints, &res) != 0) return r;
    for (auto p = res; p; p = p->ai_next) {
        char ip[64];
        inet_ntop(AF_INET, &((sockaddr_in*)p->ai_addr)->sin_addr, ip, sizeof(ip));
        if (!r.empty()) r += L", ";
        r += W(ip);
    }
    freeaddrinfo(res);
    return r;
}

static void mqttRecvLoop(SOCKET s, unsigned gen) {
    unsigned char h;
    std::string b;
    while (mqttRead(s, h, b)) {
        if ((h >> 4) != 3 || b.size() < 2) continue;   // PUBLISH 만 처리
        size_t off = 2 + (((unsigned char)b[0] << 8) | (unsigned char)b[1]);
        if ((h >> 1) & 3) off += 2;
        if (off >= b.size()) continue;
        std::string* line = new std::string(b.substr(off));
        if (!PostMessageW(g_hwnd, WM_NET, gen, (LPARAM)line)) delete line;
    }
    PostMessageW(g_hwnd, WM_NETCLOSED, gen, 0);
}

static void setConnUiEnabled(bool en) {
    for (HWND h : {hName, hIp, hPort, hHost, hJoin, hOnline, hCode, hCodeJoin, hGroup}) EnableWindow(h, en);
}
static void copyToClipboard(const std::wstring& s) {
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
    if (mem) {
        memcpy(GlobalLock(mem), s.c_str(), (s.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(mem);
        SetClipboardData(CF_UNICODETEXT, mem);
    }
    CloseClipboard();
}
static void startOnlineHost() {
    g_relay = true;
    g_isHost = true;
    g_busy = true;
    setConnUiEnabled(false);
    SetWindowTextW(hStatus, L"중계 서버에 연결 중...");
    unsigned gen = g_gen;
    std::thread([gen]() {
        std::random_device rd;
        for (int bi = 0; bi < NBROKER && gen == g_gen; bi++) {
            std::string code(1, (char)('1' + bi));   // 1:1 방 코드는 숫자로 시작
            for (int i = 0; i < 5; i++) code += CODE_ABC[rd() % 32];
            std::string peer = TOPIC_BASE + code + "/c";
            SOCKET s = mqttOpen(bi, TOPIC_BASE + code + "/h", peer, "BYE|");
            if (s != INVALID_SOCKET) {
                if (gen != g_gen) { closesocket(s); return; }   // 그 사이 취소됨
                g_code = code;
                g_peerTopic = peer;
                g_sock = s;
                std::thread(mqttRecvLoop, s, gen).detach();
                PostMessageW(g_hwnd, WM_RELAYREADY, 1, 1);
                return;
            }
        }
        if (gen == g_gen) PostMessageW(g_hwnd, WM_RELAYREADY, 0, 1);
    }).detach();
}
static void startOnlineJoin() {
    std::wstring wc = trimW(getText(hCode));
    std::string code;
    for (wchar_t c : wc)
        if (c != L' ' && c != L'-') code += (char)towupper(c);
    bool one = code.size() == 6 && code[0] >= '1' && code[0] < '1' + NBROKER;     // 1:1 방
    bool grp = code.size() == 6 && code[0] >= 'A' && code[0] < 'A' + NBROKER;     // 단체 방
    if (!one && !grp) {
        SetWindowTextW(hStatus, L"방 코드 6자리를 정확히 입력하세요.\n(예: 1K7F3Q 또는 AK7F3Q)");
        return;
    }
    g_relay = true;
    g_group = grp;
    g_isHost = false;
    g_busy = true;
    setConnUiEnabled(false);
    SetWindowTextW(hStatus, (L"방 " + W(code) + L" 를 찾는 중...\n(취소: 오른쪽 위 '메인 화면으로')").c_str());
    unsigned gen = g_gen;
    std::thread([code, grp, gen]() {
        std::string sub, peer, will;
        if (grp) {
            sub = peer = TOPIC_BASE + code + "/g";
            will = g_pid + "|LEAVE|";
        } else {
            sub = TOPIC_BASE + code + "/c";
            peer = TOPIC_BASE + code + "/h";
            will = "BYE|";
        }
        SOCKET s = mqttOpen(grp ? code[0] - 'A' : code[0] - '1', sub, peer, will);
        if (gen != g_gen) {   // 그 사이 취소됨
            if (s != INVALID_SOCKET) closesocket(s);
            return;
        }
        if (s != INVALID_SOCKET) {
            g_code = code;
            g_peerTopic = peer;
            g_sock = s;
            std::thread(mqttRecvLoop, s, gen).detach();
        }
        PostMessageW(g_hwnd, WM_RELAYREADY, s != INVALID_SOCKET, grp ? 3 : 0);
    }).detach();
}
static void startGroupHost() {
    g_relay = true;
    g_group = true;
    g_isHost = true;
    g_busy = true;
    setConnUiEnabled(false);
    SetWindowTextW(hStatus, L"중계 서버에 연결 중...");
    unsigned gen = g_gen;
    std::thread([gen]() {
        std::random_device rd;
        for (int bi = 0; bi < NBROKER && gen == g_gen; bi++) {
            std::string code(1, (char)('A' + bi));   // 단체 방 코드는 A~C로 시작
            for (int i = 0; i < 5; i++) code += CODE_ABC[rd() % 32];
            std::string topic = TOPIC_BASE + code + "/g";
            SOCKET s = mqttOpen(bi, topic, topic, g_pid + "|LEAVE|");
            if (s != INVALID_SOCKET) {
                if (gen != g_gen) { closesocket(s); return; }
                g_code = code;
                g_peerTopic = topic;
                g_sock = s;
                std::thread(mqttRecvLoop, s, gen).detach();
                PostMessageW(g_hwnd, WM_RELAYREADY, 1, 2);
                return;
            }
        }
        if (gen == g_gen) PostMessageW(g_hwnd, WM_RELAYREADY, 0, 2);
    }).detach();
}
static void relayFail(const std::wstring& why) {
    KillTimer(g_hwnd, TIMER_JOIN);
    KillTimer(g_hwnd, TIMER_PING);
    SOCKET s = g_sock;
    g_sock = INVALID_SOCKET;
    if (s != INVALID_SOCKET) closesocket(s);
    g_busy = false;
    g_relay = false;
    g_group = false;
    setConnUiEnabled(true);
    SetWindowTextW(hStatus, why.c_str());
}
// kind: 0 = 1:1 참가, 1 = 1:1 방장, 2 = 단체 방장, 3 = 단체 참가
static void onRelayReady(bool ok, int kind) {
    if (!ok) {
        relayFail(L"중계 서버에 연결할 수 없어요.\n인터넷 연결을 확인하거나, 같은 와이파이라면\nIP로 직접 연결해 보세요.");
        return;
    }
    SetTimer(g_hwnd, TIMER_PING, 20000, nullptr);
    if (kind == 2) {
        GPlayer me;
        me.pid = g_pid;
        me.name = g_myName;
        GP = {me};
        g_hostPid = g_pid;
        copyToClipboard(W(g_code));
        onConnected();
        sys(L"방 코드 " + W(g_code) + L" 가 클립보드에 복사됐습니다. 친구들에게 보내주세요!");
    } else if (kind == 3) {
        g_joinTries = 0;
        sendMsg("JOIN_REQ", U8(g_myName));
        SetTimer(g_hwnd, TIMER_JOIN, 700, nullptr);
    } else if (kind == 1) {
        std::wstring code = W(g_code);
        SetWindowTextW(hCode, code.c_str());
        copyToClipboard(code);
        SetWindowTextW(hStatus, (L"방 코드:  " + code + L"\n(클립보드에 복사됨 — 친구에게 보내주세요)\n친구가 들어오길 기다리는 중...\n(취소: 오른쪽 위 '메인 화면으로')").c_str());
        SetWindowTextW(g_hwnd, (L"미니게임 채팅 - 방 " + code).c_str());
    } else {
        g_joinTries = 0;
        sendMsg("JOIN", U8(g_myName));
        SetTimer(g_hwnd, TIMER_JOIN, 700, nullptr);
    }
}
static void startHost(int port) {
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL yes = TRUE;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons((u_short)port);
    if (bind(ls, (sockaddr*)&a, sizeof(a)) == SOCKET_ERROR || listen(ls, 1) == SOCKET_ERROR) {
        closesocket(ls);
        SetWindowTextW(hStatus, (L"포트 " + I(port) + L"를 열 수 없습니다. (이미 사용 중?)").c_str());
        return;
    }
    g_isHost = true;
    g_busy = true;
    setConnUiEnabled(false);
    SetWindowTextW(hStatus, (L"방을 만들었습니다! (포트 " + I(port) + L")\n상대에게 알려줄 IP: " + localIPs() +
                             L"\n같은 PC 테스트는 127.0.0.1\n상대 접속을 기다리는 중... (취소: '메인 화면으로')").c_str());
    g_listen = ls;
    unsigned gen = g_gen;
    std::thread([ls, gen]() {
        SOCKET s = accept(ls, nullptr, nullptr);
        if (gen != g_gen) {   // 취소됨 (대기 소켓은 취소할 때 이미 닫힘)
            if (s != INVALID_SOCKET) closesocket(s);
            return;
        }
        g_listen = INVALID_SOCKET;
        closesocket(ls);
        if (s != INVALID_SOCKET) g_sock = s;
        PostMessageW(g_hwnd, WM_CONNRESULT, s != INVALID_SOCKET, 0);
    }).detach();
}
static void startJoin(const std::wstring& ipW, int port) {
    g_isHost = false;
    g_busy = true;
    setConnUiEnabled(false);
    SetWindowTextW(hStatus, (ipW + L":" + I(port) + L" 에 접속 중...").c_str());
    std::string ip = U8(ipW);
    unsigned gen = g_gen;
    std::thread([ip, port, gen]() {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        bool ok = false;
        SOCKET s = INVALID_SOCKET;
        if (getaddrinfo(ip.c_str(), std::to_string(port).c_str(), &hints, &res) == 0) {
            s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            ok = connect(s, res->ai_addr, (int)res->ai_addrlen) == 0;
            freeaddrinfo(res);
            if (!ok) closesocket(s);
        }
        if (gen != g_gen) {   // 취소됨
            if (ok) closesocket(s);
            return;
        }
        if (ok) g_sock = s;
        PostMessageW(g_hwnd, WM_CONNRESULT, ok, 1);
    }).detach();
}
static void onDisconnected(const std::wstring& why) {
    if (g_ended) return;
    g_ended = true;
    sys(why);
    sys(L"연결이 끊어졌습니다. 오른쪽 위 '메인 화면으로'를 눌러 다시 접속할 수 있어요.");
    EnableWindow(hInput, FALSE);
    EnableWindow(hSend, FALSE);
    F.on = false;
    K.on = false;
    g_chalFrom.clear();
    KillTimer(g_hwnd, TIMER_PING);
    SOCKET s = g_sock;
    g_sock = INVALID_SOCKET;
    if (s != INVALID_SOCKET) closesocket(s);
    updateStrip();
    RECT r;
    GetClientRect(g_hwnd, &r);
    InvalidateRect(g_hwnd, &r, FALSE);
}
static void onConnected() {
    g_connected = true;
    g_busy = false;
    for (HWND h : {hLblName, hName, hLblIp, hIp, hPort, hHost, hJoin, hStatus, hOnline, hLblCode, hCode, hCodeJoin, hGroup})
        ShowWindow(h, SW_HIDE);
    EnableWindow(hInput, TRUE);
    EnableWindow(hSend, TRUE);
    layoutTabs();
    updateStrip();
    if (g_group) {
        g_view = V_WORD;
        sys(L"단체 방 " + W(g_code) + L" 에 들어왔습니다! 위쪽 탭에서 게임을 고르세요.");
        printGroupHelp();
        SetWindowTextW(g_hwnd, (L"단체 방 " + W(g_code) + L" - " + g_myName).c_str());
        RECT r;
        GetClientRect(g_hwnd, &r);
        InvalidateRect(g_hwnd, &r, FALSE);
        SetFocus(hInput);
        return;
    }
    sendMsg("HELLO", U8(g_myName));
    if (!g_relay) {
        BOOL nd = TRUE;
        setsockopt(g_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nd, sizeof(nd));
        SOCKET s = g_sock;
        std::thread(recvLoop, s, (unsigned)g_gen).detach();
    }
    sys(g_relay ? L"방 " + W(g_code) + L" 연결 성공! (중계 서버 경유)" : std::wstring(L"연결 성공!"));
    printHelp();
    SetWindowTextW(g_hwnd, (L"미니게임 채팅 - " + g_myName).c_str());
    RECT r;
    GetClientRect(g_hwnd, &r);
    InvalidateRect(g_hwnd, &r, FALSE);
    SetFocus(hInput);
}

// 모든 연결과 게임 상태를 지우고 첫 접속 화면으로 돌아감
static void goHome(const std::wstring& status) {
    g_gen++;
    KillTimer(g_hwnd, TIMER_JOIN);
    KillTimer(g_hwnd, TIMER_PING);
    if (g_listen != INVALID_SOCKET) {
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
    }
    SOCKET s = g_sock;
    g_sock = INVALID_SOCKET;
    if (s != INVALID_SOCKET) {
        shutdown(s, SD_BOTH);
        closesocket(s);
    }
    g_connected = g_busy = g_isHost = g_ended = g_relay = g_group = false;
    g_code.clear();
    g_peerTopic.clear();
    g_matchPid.clear();
    g_hostPid.clear();
    g_peer = L"상대";
    R = decltype(R)();
    D = decltype(D)();
    T = decltype(T)();
    O = decltype(O)();
    Y = decltype(Y)();
    F = FightState();
    B = decltype(B)();
    UD = decltype(UD)();
    K = decltype(K)();
    GP.clear();
    GR = decltype(GR)();
    GD = decltype(GD)();
    GU = decltype(GU)();
    g_pairs.clear();
    g_chalTo.clear();
    g_chalFrom.clear();
    g_view = V_RPS;
    for (HWND h : hGameBtn) ShowWindow(h, SW_HIDE);
    for (HWND h : {hLblName, hName, hLblIp, hIp, hPort, hHost, hJoin, hStatus, hOnline, hLblCode, hCode, hCodeJoin, hGroup})
        ShowWindow(h, SW_SHOW);
    setConnUiEnabled(true);
    SetWindowTextW(hCode, L"");
    EnableWindow(hInput, FALSE);
    EnableWindow(hSend, FALSE);
    SetWindowTextW(hStatus, status.c_str());
    SetWindowTextW(g_hwnd, L"미니게임 채팅");
    updateStrip();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}
static void leaveRoom() {
    if (g_sock != INVALID_SOCKET && g_connected && !g_ended) {
        sendMsg(g_group ? (g_isHost ? "CLOSE" : "LEAVE") : "BYE");   // 상대에게 나간다고 알림
        if (g_relay) rawSend(std::string("\xE0\x00", 2));             // MQTT DISCONNECT
    }
    logLine(L"────────────────");
    sys(L"방에서 나와 메인 화면으로 돌아왔습니다.");
    goHome(L"방에서 나왔습니다.\n새 방을 만들거나 코드로 참가하세요.");
}
// 배율이 바뀌면 글꼴을 새 크기로 만들고 모든 컨트롤을 다시 배치
static void applyLayout() {
    HFONT o1 = g_uiFont, o2 = g_logFont, o3 = g_tabFont;
    auto mkf = [](int px) {
        return CreateFontW(-S(px), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Malgun Gothic");
    };
    g_uiFont = mkf(15);
    g_logFont = mkf(14);
    g_tabFont = mkf(13);
    for (auto& kv : g_ctl) placeCtl(kv.first);
    DeleteObject(o1);
    DeleteObject(o2);
    DeleteObject(o3);
    InvalidateRect(g_hwnd, nullptr, TRUE);
}
static void setFullscreen(bool on) {
    if (on == g_full) return;
    g_full = on;
    if (on) {
        GetWindowPlacement(g_hwnd, &g_prevPlace);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        RECT m = mi.rcMonitor;
        int mw = m.right - m.left, mh = m.bottom - m.top;
        g_zoom = std::min(mw / (1000.0 * g_baseScale), mh / (570.0 * g_baseScale));
        g_scale = g_baseScale * g_zoom;
        g_offX = (mw - S(1000)) / 2;
        g_offY = (mh - S(570)) / 2;
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN);
        SetWindowPos(g_hwnd, HWND_TOP, m.left, m.top, mw, mh, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        g_zoom = 1.0;
        g_scale = g_baseScale;
        g_offX = g_offY = 0;
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, WND_STYLE | WS_VISIBLE);
        SetWindowPlacement(g_hwnd, &g_prevPlace);
        SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    SetWindowTextW(hFullBtn, on ? L"창 모드로 (F11)" : L"전체 화면 (F11)");
    applyLayout();
}

static void sendChat() {
    std::wstring t = trimW(getText(hInput));
    SetWindowTextW(hInput, L"");
    if (t.empty() || !g_connected || g_ended) return;
    if (g_group) {
        if (K.on && K.cur == g_pid && t[0] != L'/') {   // 내 차례에는 입력이 곧 단어
            std::wstring w;
            for (wchar_t c : t)
                if (c != L' ' && c != L'|') w += c;
            logLine(L"[나] " + w + L"  (제출)");
            submitWord(w);
        } else if (t[0] == L'/') {
            if (t == L"/help" || t == L"/?") printGroupHelp();
            else if (!g_matchPid.empty()) handleCommand(t);   // 대결 상대와 숫자야구 등
            else gmsg(L"채팅 명령(숫자야구 등)은 2인 대결 중일 때 쓸 수 있어요. /help");
        } else if (g_view == V_GUD && GU.on && GU.setter != g_pid &&
                   t.find_first_not_of(L"0123456789") == std::wstring::npos && t.size() <= 3) {
            int n = _wtoi(t.c_str());   // 업다운 추측
            if (n >= 1 && n <= 100) {
                sendMsg("GUD_GUESS", std::to_string(n));
                logLine(L"[나] " + I(n) + L" 추측");
            } else {
                gmsg(L"1~100 사이 숫자를 입력하세요.");
            }
        } else {
            std::replace(t.begin(), t.end(), L'|', L'/');
            sendMsg("CHAT", U8(t));
            logLine(L"[나] " + t);
        }
        invalidateCanvas();
        return;
    }
    if (t[0] == L'/') handleCommand(t);
    else {
        sendMsg("CHAT", U8(t));
        logLine(L"[나] " + t);
    }
    invalidateCanvas();
}

static LRESULT CALLBACK InputProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_KEYDOWN && w == VK_RETURN) { sendChat(); return 0; }
    if (m == WM_KEYDOWN && w == VK_ESCAPE) { SetFocus(g_hwnd); invalidateCanvas(); return 0; }
    if (m == WM_CHAR && (w == L'\r' || w == 27)) return 0;
    if (m == WM_SETFOCUS || m == WM_KILLFOCUS) invalidateCanvas();
    return DefSubclassProc(h, m, w, l);
}
static LRESULT CALLBACK ConnEditProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR ref) {
    if (m == WM_KEYDOWN && w == VK_RETURN) { SendMessageW(g_hwnd, WM_COMMAND, (WPARAM)ref, 0); return 0; }
    if (m == WM_CHAR && w == L'\r') return 0;
    return DefSubclassProc(h, m, w, l);
}

// ═════════════════════════ 화면 ═════════════════════════
static void drawConnect(HDC dc) {
    text(dc, 0, 18, CW, 50, L"🎮 미니게임 채팅", 34, COL_TEXT, true);
    text(dc, 0, 68, CW, 26, L"가위바위보 · 주사위 · 틱택토 · 오목 · 윷놀이 · 격투 · 업다운 · 끝말잇기 — 1:1도, 단체(8명)도!", 14, COL_SUB);
    roundRect(dc, 125, 104, 390, 370, RGB(255, 255, 255), RGB(205, 210, 225), 24, 2);
    line(dc, 150, 268, 490, 268, RGB(225, 228, 238), 1);
    fillRect(dc, 205, 258, 230, 20, RGB(255, 255, 255));
    text(dc, 205, 258, 230, 20, L"같은 와이파이라면 IP로 직접 연결", 13, COL_SUB);
}
static void drawCanvas(HDC dc) {
    fillRect(dc, 0, 0, CW, CH, COL_BG);
    if (!g_connected) { drawConnect(dc); return; }
    if (g_group) { drawGroupCanvas(dc); return; }
    switch (g_view) {
        case V_RPS: drawRps(dc); break;
        case V_DICE: drawDice(dc); break;
        case V_TTT: drawTtt(dc); break;
        case V_OMOK: drawOmok(dc); break;
        case V_YUT: drawYut(dc); break;
        case V_FIGHT: drawFight(dc); break;
    }
}
static void canvasClick(int x, int y) {
    if (!g_connected) return;
    if (g_group) { groupClick(x, y); return; }
    switch (g_view) {
        case V_RPS:
            for (int i = 0; i < 3; i++)
                if (rpsBtn(i).hit(x, y)) rpsPick(i);
            break;
        case V_DICE:
            if (diceBtn.hit(x, y)) diceRoll();
            break;
        case V_TTT:
            if (tttNew.hit(x, y)) tttStart();
            else if (x >= 170 && x < 470 && y >= 75 && y < 375) tttMove((y - 75) / 100 * 3 + (x - 170) / 100);
            break;
        case V_OMOK: omClick(x, y); break;
        case V_YUT: yutClick(x, y); break;
        case V_FIGHT:
            if ((!F.on || F.result) && fightBtn.hit(x, y)) fightStart();
            break;
    }
    invalidateCanvas();
}

static HWND mk(const wchar_t* cls, const wchar_t* txt, DWORD style, int x, int y, int w, int h, int id = 0,
               DWORD ex = 0) {
    HWND hw = CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | style, PX(x), PY(y), S(w), S(h), g_hwnd,
                              (HMENU)(INT_PTR)id, g_inst, nullptr);
    g_ctl[hw] = {x, y, w, h, 0};
    SendMessageW(hw, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
    return hw;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_hwnd = hwnd;
            g_uiFont = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Malgun Gothic");
            g_logFont = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Malgun Gothic");
            g_tabFont = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Malgun Gothic");
            g_whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
            g_winBrush = CreateSolidBrush(COL_WIN);
            for (int i = 0; i < V_COUNT; i++) {   // 게임 탭 (모드에 맞게 layoutTabs에서 배치)
                hGameBtn[i] = mk(L"BUTTON", L"", BS_PUSHBUTTON, CX + i * 80, 8, 76, 34, ID_GAME0 + i);
                ShowWindow(hGameBtn[i], SW_HIDE);
            }
            // 게임 화면 아래 상태줄
            hStrip = mk(L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS, 14, 536, 520, 28);
            hStripBtn = mk(L"BUTTON", L"대결 끝내기", BS_PUSHBUTTON, 540, 535, 110, 30, ID_ENDMATCH);
            ShowWindow(hStripBtn, SW_HIDE);
            // 채팅 칸 위: 메인 화면으로 / 전체 화면 버튼
            hHomeBtn = mk(L"BUTTON", L"◀ 메인 화면으로", BS_PUSHBUTTON, 660, 8, 162, 32, ID_HOME);
            hFullBtn = mk(L"BUTTON", L"전체 화면 (F11)", BS_PUSHBUTTON, 828, 8, 162, 32, ID_FULL);
            ShowWindow(hHomeBtn, SW_HIDE);
            hLog = mk(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 660, 46, 330, 476, 0, WS_EX_CLIENTEDGE);
            g_ctl[hLog].font = 1;
            SendMessageW(hLog, WM_SETFONT, (WPARAM)g_logFont, TRUE);
            SendMessageW(hLog, EM_SETLIMITTEXT, 0, 0);
            hInput = mk(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 660, 530, 262, 34, ID_INPUT, WS_EX_CLIENTEDGE);
            hSend = mk(L"BUTTON", L"보내기", BS_PUSHBUTTON, 926, 530, 64, 34, ID_SEND);
            EnableWindow(hInput, FALSE);
            EnableWindow(hSend, FALSE);
            SetWindowSubclass(hInput, InputProc, 1, 0);
            SendMessageW(hInput, EM_SETCUEBANNER, TRUE, (LPARAM)L"메시지 입력 후 Enter");

            int ox = CX, oy = CY;
            hLblName = mk(L"STATIC", L"닉네임", SS_CENTERIMAGE, ox + 150, oy + 120, 70, 30);
            hName = mk(L"EDIT", L"플레이어", ES_AUTOHSCROLL | WS_TABSTOP, ox + 225, oy + 120, 265, 30, 0, WS_EX_CLIENTEDGE);
            hOnline = mk(L"BUTTON", L"1:1 방 만들기", BS_PUSHBUTTON | WS_TABSTOP, ox + 150, oy + 162, 166, 42, ID_ONLINE);
            hGroup = mk(L"BUTTON", L"단체 방 (최대 8명)", BS_PUSHBUTTON | WS_TABSTOP, ox + 324, oy + 162, 166, 42, ID_GROUP);
            hLblCode = mk(L"STATIC", L"방 코드", SS_CENTERIMAGE, ox + 150, oy + 215, 70, 32);
            hCode = mk(L"EDIT", L"", ES_AUTOHSCROLL | ES_UPPERCASE | WS_TABSTOP, ox + 225, oy + 215, 140, 32, 0, WS_EX_CLIENTEDGE);
            SendMessageW(hCode, EM_SETLIMITTEXT, 8, 0);
            SendMessageW(hCode, EM_SETCUEBANNER, TRUE, (LPARAM)L"예) 1K7F3Q");
            hCodeJoin = mk(L"BUTTON", L"코드로 참가", BS_PUSHBUTTON | WS_TABSTOP, ox + 372, oy + 214, 118, 34, ID_CODEJOIN);
            hLblIp = mk(L"STATIC", L"상대 IP", SS_CENTERIMAGE, ox + 150, oy + 286, 70, 30);
            hIp = mk(L"EDIT", L"127.0.0.1", ES_AUTOHSCROLL | WS_TABSTOP, ox + 225, oy + 286, 170, 30, 0, WS_EX_CLIENTEDGE);
            hPort = mk(L"EDIT", L"5555", ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP, ox + 400, oy + 286, 90, 30, 0, WS_EX_CLIENTEDGE);
            hHost = mk(L"BUTTON", L"IP로 방 만들기", BS_PUSHBUTTON | WS_TABSTOP, ox + 150, oy + 324, 165, 34, ID_HOST);
            hJoin = mk(L"BUTTON", L"IP로 참가", BS_PUSHBUTTON | WS_TABSTOP, ox + 325, oy + 324, 165, 34, ID_JOIN);
            hStatus = mk(L"STATIC", L"온라인 방을 만들어 친구에게 코드를 보내거나,\n친구에게 받은 코드를 입력하세요.", SS_CENTER, ox + 140, oy + 368, 360, 100);
            SetWindowSubclass(hIp, ConnEditProc, 2, ID_JOIN);
            SetWindowSubclass(hCode, ConnEditProc, 3, ID_CODEJOIN);
            SetTimer(hwnd, 1, 16, nullptr);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wp;
            if ((HWND)lp == hStrip) {   // 상태줄은 창 배경색
                SetBkColor(dc, COL_WIN);
                SetTextColor(dc, COL_TEXT);
                return (LRESULT)g_winBrush;
            }
            SetBkColor(dc, RGB(255, 255, 255));
            SetTextColor(dc, (HWND)lp == hStatus ? COL_SUB : COL_TEXT);
            return (LRESULT)g_whiteBrush;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id >= ID_GAME0 && id < ID_GAME0 + g_tabCount) {
                setView(g_tabs[id - ID_GAME0]);
                SetFocus(hwnd);
            } else if (id == ID_ENDMATCH) {
                if (!g_matchPid.empty()) {
                    sendMsg("MEND", g_matchPid);
                    endMatchLocal(L"대결을 끝냈습니다. 다른 상대를 골라 신청할 수 있어요.");
                }
                SetFocus(hwnd);
            } else if (id == ID_FULL) {
                setFullscreen(!g_full);
                SetFocus(hwnd);
            } else if (id == ID_HOME) {
                if (g_connected && !g_ended) {
                    if (MessageBoxW(hwnd, L"방에서 나가고 메인 화면으로 돌아갈까요?", L"메인 화면으로", MB_YESNO | MB_ICONQUESTION) == IDYES)
                        leaveRoom();
                } else if (g_connected) {   // 이미 연결이 끊긴 방
                    goHome(L"메인 화면으로 돌아왔습니다.\n새 방을 만들거나 코드로 참가하세요.");
                } else if (g_busy) {        // 방 코드를 받고 기다리는 중 / 접속 중
                    if (g_relay && g_sock != INVALID_SOCKET) rawSend(std::string("\xE0\x00", 2));
                    goHome(L"취소했습니다.\n다시 방을 만들거나 코드로 참가하세요.");
                }
                SetFocus(hwnd);
            } else if (id == ID_SEND) {
                sendChat();
                SetFocus(hInput);
            } else if ((id == ID_HOST || id == ID_JOIN || id == ID_ONLINE || id == ID_CODEJOIN || id == ID_GROUP) && !g_busy &&
                       !g_connected) {
                g_myName = trimW(safeName(getText(hName)));
                if (g_myName.empty()) g_myName = L"플레이어";
                if (g_myName.size() > 12) g_myName.resize(12);
                int port = _wtoi(getText(hPort).c_str());
                if (port <= 0 || port > 65535) port = 5555;
                if (id == ID_ONLINE) startOnlineHost();
                else if (id == ID_GROUP) startGroupHost();
                else if (id == ID_CODEJOIN) startOnlineJoin();
                else if (id == ID_HOST) startHost(port);
                else {
                    std::wstring ip = trimW(getText(hIp));
                    startJoin(ip.empty() ? L"127.0.0.1" : ip, port);
                }
            }
            return 0;
        }
        case WM_CONNRESULT:
            g_busy = false;
            if (wp) onConnected();
            else {
                setConnUiEnabled(true);
                SetWindowTextW(hStatus, lp ? L"접속 실패!\nIP/포트를 확인하고, 상대가 방을 만들었는지 확인하세요." : L"연결 대기 실패");
            }
            return 0;
        case WM_NET: {
            std::string* line = (std::string*)lp;
            if (wp == g_gen) handleNet(*line);   // 예전 연결의 메시지는 무시
            delete line;
            return 0;
        }
        case WM_RELAYREADY:
            onRelayReady(wp != 0, (int)lp);
            return 0;
        case WM_NETCLOSED:
            if (wp != g_gen) return 0;
            if (g_connected) onDisconnected(g_relay ? L"중계 서버와의 연결이 끊겼습니다." : g_peer + L" 님과의 연결이 끊겼습니다.");
            else if (g_relay && g_busy) relayFail(L"중계 서버와의 연결이 끊겼어요. 다시 시도하세요.");
            return 0;
        case WM_TIMER:
            if (wp == TIMER_FRAME) {
                static int frame = 0;
                frame++;
                bool wantHome = g_connected || g_busy;   // 메인 화면 버튼은 방/대기 중일 때만
                if (wantHome != (IsWindowVisible(hHomeBtn) != FALSE)) ShowWindow(hHomeBtn, wantHome ? SW_SHOW : SW_HIDE);
                if (F.on) {
                    fightTick();
                    if (g_view == V_FIGHT) invalidateCanvas();
                }
                if (g_group && g_connected && !g_ended) {
                    groupTick();
                    if (K.on && frame % 6 == 0) invalidateCanvas();   // 남은 시간 막대 갱신
                }
            } else if (wp == TIMER_PING && g_relay && g_sock != INVALID_SOCKET) {
                rawSend(std::string("\xC0\x00", 2));   // MQTT PINGREQ (연결 유지)
            } else if (wp == TIMER_JOIN) {
                if (g_connected) KillTimer(hwnd, TIMER_JOIN);
                else if (++g_joinTries > 15)
                    relayFail(L"방 " + W(g_code) + L" 를 찾을 수 없어요.\n코드가 맞는지, 친구가 방을 만들어 둔 상태인지 확인하세요.");
                else sendMsg(g_group ? "JOIN_REQ" : "JOIN", U8(g_myName));
            }
            return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            double lx = (x - PX(CX)) / g_scale, ly = (y - PY(CY)) / g_scale;
            if (lx >= 0 && ly >= 0 && lx < CW && ly < CH) canvasClick((int)lx, (int)ly);
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_RETURN && g_connected) SetFocus(hInput);
            if (wp == VK_ESCAPE && g_full) setFullscreen(false);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT cr;
            GetClientRect(hwnd, &cr);
            int cx = PX(CX), cy = PY(CY), cw = S(CW), ch = S(CH);
            ExcludeClipRect(hdc, cx, cy, cx + cw, cy + ch);
            HBRUSH bg = CreateSolidBrush(COL_WIN);
            FillRect(hdc, &cr, bg);
            DeleteObject(bg);
            for (int i = 0; g_connected && i < g_tabCount; i++) {   // 선택된 탭 아래 주황 막대
                if (g_tabs[i] != g_view) continue;
                RECT ind{PX(CX + i * g_btnStep), PY(43), PX(CX + i * g_btnStep + g_btnW), PY(47)};
                HBRUSH ab = CreateSolidBrush(COL_ACC);
                FillRect(hdc, &ind, ab);
                DeleteObject(ab);
            }
            SelectClipRgn(hdc, nullptr);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, cw, ch);
            HGDIOBJ old = SelectObject(mem, bmp);
            SetGraphicsMode(mem, GM_ADVANCED);
            XFORM xf{(FLOAT)g_scale, 0, 0, (FLOAT)g_scale, 0, 0};
            SetWorldTransform(mem, &xf);
            drawCanvas(mem);
            ModifyWorldTransform(mem, nullptr, MWT_IDENTITY);
            BitBlt(hdc, cx, cy, cw, ch, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CLOSE:
            if (g_sock != INVALID_SOCKET) {
                if (g_connected) sendMsg(g_group ? (g_isHost ? "CLOSE" : "LEAVE") : "BYE");
                if (g_relay) rawSend(std::string("\xE0\x00", 2));   // MQTT DISCONNECT
                shutdown(g_sock, SD_BOTH);
                closesocket(g_sock);
                g_sock = INVALID_SOCKET;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow) {
    g_inst = hInst;
    {
        std::random_device rd;
        char id[20];
        snprintf(id, sizeof(id), "%08x", rd());
        g_pid = id;
    }
    SetProcessDPIAware();
    HDC sdc = GetDC(nullptr);
    g_scale = g_baseScale = GetDeviceCaps(sdc, LOGPIXELSX) / 96.0;
    ReleaseDC(nullptr, sdc);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = L"MiniGameChat";
    RegisterClassExW(&wc);

    DWORD style = WND_STYLE;
    RECT r{0, 0, S(1000), S(570)};
    AdjustWindowRect(&r, style, FALSE);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"미니게임 채팅", style, CW_USEDEFAULT, CW_USEDEFAULT,
                                r.right - r.left, r.bottom - r.top, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_F11) {   // 어느 칸에 있든 F11 = 전체 화면 켜기/끄기
            setFullscreen(!g_full);
            continue;
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    WSACleanup();
    return 0;
}
