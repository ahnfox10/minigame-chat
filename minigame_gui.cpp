// 미니게임 파티 — 방 코드 하나로 친구들과 채팅하며 즐기는 미니게임 모음 (Win32 GDI, 외부 라이브러리 없음)
// 방 전체가 함께: 끝말잇기 · 가위바위보   /   초대해서 2~8명: 오목 · 윷놀이 · 격투 · 레이싱 · 페인트볼(FPS)
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
#define CY CY_OLE_   // richedit.h가 끌어오는 OLE 형식 CY와 아래 화면 좌표 상수 CY가 겹치지 않게
#include <richedit.h>
#undef CY

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

#include <chrono>

// ───────────────────────── 전역 ─────────────────────────
enum { WM_NET = WM_APP + 1, WM_CONNRESULT, WM_NETCLOSED, WM_RELAYREADY };
enum { V_WORD, V_RPS, V_OMOK, V_YUT, V_FIGHT, V_RACE, V_FPS, V_COUNT };
static const wchar_t* VIEW_NAMES[V_COUNT] = {L"끝말잇기", L"가위바위보", L"오목", L"윷놀이", L"격투", L"레이싱", L"페인트볼"};
// 초대해서 하는 게임의 최소/최대 인원 (0 = 방 전체가 함께 하는 게임)
static const int VIEW_MIN[V_COUNT] = {0, 0, 2, 2, 2, 2, 2};
static const int VIEW_MAX[V_COUNT] = {0, 0, 2, 4, 4, 8, 8};
static bool isMatchView(int v) { return v >= 0 && v < V_COUNT && VIEW_MAX[v] > 0; }
static const int TABS[V_COUNT] = {V_WORD, V_RPS, V_OMOK, V_YUT, V_FIGHT, V_RACE, V_FPS};
enum { ID_GAME0 = 100, ID_SEND = 200, ID_HOST, ID_JOIN, ID_INPUT, ID_ONLINE, ID_CODEJOIN, ID_ENDMATCH, ID_HOME, ID_FULL, ID_COPY, ID_CUSTOM };
enum { TIMER_FRAME = 1, TIMER_JOIN = 2, TIMER_PING = 3, TIMER_COPY = 4 };

// 방 코드 모드: 공개 MQTT 중계 서버를 통해 메시지를 주고받는다.
struct Broker { const char* host; int port; };
// 순서 = 먼저 시도할 순서. 마지막 서버(EMQX)는 한 사람이 초당 10개 넘게 보내면 메시지를 버려서 예비용으로만 쓴다.
static const Broker BROKERS[] = {{"broker.hivemq.com", 1883}, {"test.mosquitto.org", 1883}, {"broker.emqx.io", 1883}};
static const int SLOW_BROKER = 2;
static const int NBROKER = 3;
static const char* CODE_ABC = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static const std::string TOPIC_BASE = "mgparty-rhkwptq/v3/";

const int CX = 10, CY = 50, CW = 640, CH = 480;      // 게임 화면(논리 좌표)
static int g_btnW = 88, g_btnStep = 92;              // 탭 버튼 크기
static const int* g_tabs = TABS;
static int g_tabCount = V_COUNT;

static HINSTANCE g_inst;
static HWND g_hwnd, hLog, hInput, hSend, hGameBtn[V_COUNT], hStrip, hStripBtn, hHomeBtn, hFullBtn, hCopyBtn, hCustomBtn;
static HWND hLblName, hName, hLblIp, hIp, hPort, hHost, hJoin, hStatus, hOnline, hLblCode, hCode, hCodeJoin;
static double g_scale = 1.0;
static HFONT g_uiFont, g_logFont, g_tabFont;
static HBRUSH g_whiteBrush, g_winBrush;

static SOCKET g_sock = INVALID_SOCKET;
static std::mutex g_sendMx;
static bool g_connected = false, g_busy = false, g_isHost = false, g_ended = false;
static bool g_relay = false;              // true = 방 코드(중계 서버) 모드, false = IP 직접 연결
static bool g_slowRelay = false;          // 느린 예비 서버를 쓰는 중 (상태를 덜 자주 보냄)
static bool g_tcpHost = false;            // IP 방의 방장: 참가자들의 메시지를 서로에게 전달해 준다
static std::vector<SOCKET> g_clients;     // IP 방장에게 접속한 참가자 소켓
static std::string g_code, g_peerTopic;
static int g_joinTries = 0;
static std::string g_pid, g_hostPid;      // 방에서 나/방장을 구분하는 무작위 ID
static std::wstring g_myName;
static int g_view = V_WORD;
static std::mt19937 g_rng{std::random_device{}()};

// 플레이어 꾸미기: 색 · 머리 장식 (다른 사람에게도 보임) / 게임 판 테마 (내 화면에만 적용)
static int g_myColor = 0, g_myHat = 1, g_theme = 0, g_myPiece = 0;
static const COLORREF PCOL[8] = {RGB(96, 134, 240), RGB(240, 100, 92),  RGB(52, 184, 128),  RGB(255, 168, 56),
                                 RGB(160, 120, 232), RGB(38, 188, 212), RGB(240, 120, 180), RGB(146, 112, 84)};
static const int HAT_COUNT = 6, THEME_COUNT = 4, PIECE_COUNT = 6;
static const wchar_t* HAT_N[HAT_COUNT] = {L"없음", L"머리띠", L"왕관", L"고깔", L"고양이 귀", L"뿔"};
static const wchar_t* PIECE_N[PIECE_COUNT] = {L"동글", L"별", L"하트", L"보석", L"고양이", L"내 얼굴"};   // 윷놀이 말 모양
struct Theme { const wchar_t* name; COLORREF board, edge, line, node; };
static const Theme THEMES[THEME_COUNT] = {
    {L"나무", RGB(232, 192, 125), RGB(180, 135, 70), RGB(120, 82, 40), RGB(255, 250, 235)},
    {L"밤하늘", RGB(46, 54, 100), RGB(24, 28, 60), RGB(150, 165, 230), RGB(226, 232, 255)},
    {L"벚꽃", RGB(255, 224, 234), RGB(236, 150, 180), RGB(205, 115, 145), RGB(255, 250, 252)},
    {L"민트", RGB(208, 240, 228), RGB(90, 180, 150), RGB(60, 140, 115), RGB(250, 255, 252)}};

// 밝고 귀여운 파스텔 테마 (하늘·민트)
static const COLORREF COL_WIN = RGB(224, 239, 252), COL_BG = RGB(250, 252, 255), COL_TEXT = RGB(50, 62, 96),
                      COL_SUB = RGB(126, 140, 168), COL_ME = RGB(96, 134, 240), COL_OP = RGB(244, 112, 98),
                      COL_ACC = RGB(255, 172, 64), COL_WINC = RGB(52, 184, 128), COL_MAIN = RGB(64, 192, 168),
                      COL_LINE = RGB(198, 222, 242), COL_GRAY = RGB(166, 178, 198);

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
    if (c.font == 1) {   // 채팅 로그(RichEdit): 글꼴·크기만 바꿔서 색·굵기는 유지
        CHARFORMAT2W cf{};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE;
        wcscpy_s(cf.szFaceName, L"Malgun Gothic");
        HDC sdc = GetDC(nullptr);
        cf.yHeight = MulDiv(S(15), 1440, GetDeviceCaps(sdc, LOGPIXELSY));
        ReleaseDC(nullptr, sdc);
        SendMessageW(h, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
        SendMessageW(h, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
        SendMessageW(h, WM_VSCROLL, SB_BOTTOM, 0);   // 크기가 바뀌어도 최신 메시지가 보이게
        return;
    }
    SendMessageW(h, WM_SETFONT, (WPARAM)(c.font == 2 ? g_tabFont : g_uiFont), TRUE);
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
// 채팅 칸은 RichEdit: 채팅(이름 굵게·색)과 시스템/게임 알림(연한 회색)을 구분해서 보여 줌
static void logAppend(const std::wstring& s, COLORREF c, bool bold) {
    SendMessageW(hLog, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BOLD;
    cf.crTextColor = c;
    cf.dwEffects = bold ? CFE_BOLD : 0;
    SendMessageW(hLog, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)s.c_str());
}
static void logBreak(int spaceTwips) {   // 새 줄 시작 (+ 줄 위 여백)
    if (GetWindowTextLengthW(hLog) > 0) logAppend(L"\r", RGB(0, 0, 0), false);
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_SPACEBEFORE;
    pf.dySpaceBefore = spaceTwips;
    SendMessageW(hLog, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageW(hLog, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}
static void logEnd() { SendMessageW(hLog, WM_VSCROLL, SB_BOTTOM, 0); }
// 시스템·게임 알림: 연한 회색
static void logLine(const std::wstring& s) {
    logBreak(40);
    logAppend(s, RGB(140, 150, 170), false);
    logEnd();
}
// 채팅: "이름  내용" — 이름은 굵게(나는 파랑, 상대는 산호색), 내용은 진한 글자
static void chatLine(const std::wstring& name, const std::wstring& msg, bool mine) {
    logBreak(120);
    logAppend(name + L"  ", mine ? RGB(70, 110, 225) : RGB(225, 95, 80), true);
    logAppend(msg, RGB(36, 44, 70), false);
    logEnd();
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

static void wireSend(const std::string& msg) {
    if (g_tcpHost) {
        std::lock_guard<std::mutex> lk(g_sendMx);
        for (SOCKET s : g_clients) sendAll(s, msg + "\n");
    } else if (g_sock == INVALID_SOCKET) {
        return;
    } else if (g_relay) {
        rawSend(mqttPacket(0x30, mqttStr(g_peerTopic) + msg));
    } else {
        rawSend(msg + "\n");
    }
}
// 중계 서버(MQTT QoS 0)는 메시지를 잃어버릴 수 있다. 그래서 중요한 메시지에는 번호를 붙여 두 번 더 보내고,
// 받는 쪽은 번호 순서대로 한 번씩만 처리한다. 자주 보내는 위치 정보(F_S 등)는 잃어도 되므로 그냥 보낸다.
struct Resend { std::string msg; ULONGLONG at; int left; };
static std::vector<Resend> g_resend;
static unsigned g_txSeq = 0;
static bool isLossy(const std::string& t) { return t == "F_S" || t == "R_S" || t == "P_S" || t == "JOIN_REQ"; }
// 방 안의 모두에게 메시지를 보낸다. 형식: 보낸사람ID|종류|내용
static void sendMsg(const std::string& type, const std::string& payload = "") {
    if (!g_tcpHost && g_sock == INVALID_SOCKET) return;
    std::string body = type + "|" + payload;
    std::replace(body.begin(), body.end(), '\n', ' ');
    std::replace(body.begin(), body.end(), '\r', ' ');
    if (g_relay && !isLossy(type)) {
        std::string msg = g_pid + "|~" + std::to_string(++g_txSeq) + "~" + body;
        wireSend(msg);
        g_resend.push_back({msg, GetTickCount64() + 250, 2});
    } else {
        wireSend(g_pid + "|" + body);
    }
}
static void resendTick() {
    ULONGLONG now = GetTickCount64();
    for (auto& r : g_resend)
        if (now >= r.at) { wireSend(r.msg); r.left--; r.at = now + 450; }
    g_resend.erase(std::remove_if(g_resend.begin(), g_resend.end(), [](const Resend& r) { return r.left <= 0; }), g_resend.end());
}
// 실시간 게임에서 내 상태를 보내는 간격(초). 사람이 많을수록, 느린 서버일수록 덜 자주
static double stateEvery(int n) { return g_slowRelay ? 0.3 : n <= 2 ? 0.05 : n <= 4 ? 0.066 : 0.09; }

static void invalidateCanvas() {
    RECT r{PX(CX), PY(CY), PX(CX + CW), PY(CY + CH)};
    InvalidateRect(g_hwnd, &r, FALSE);
}
static void setView(int v) {
    g_view = v;
    RECT r{0, 0, PX(CX + CW), PY(CY)};
    InvalidateRect(g_hwnd, &r, FALSE);
    for (HWND h : hGameBtn) if (h) InvalidateRect(h, nullptr, FALSE);   // 선택된 탭 색 다시 그리기
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
    COLORREF f = enabled ? col : RGB(212, 221, 233);
    roundRect(dc, b.x, b.y + 4, b.w, b.h, darker(f, 0.82), darker(f, 0.82), b.h, 1);   // 아래 그림자 (말랑한 버튼)
    roundRect(dc, b.x, b.y, b.w, b.h, f, f, b.h, 1);
    if (enabled) {
        COLORREF hi = RGB(std::min(255, GetRValue(f) + 36), std::min(255, GetGValue(f) + 36), std::min(255, GetBValue(f) + 36));
        roundRect(dc, b.x + b.h / 4, b.y + 4, b.w - b.h / 2, b.h / 3, hi, hi, b.h / 3, 1);   // 위쪽 반짝이
    }
    text(dc, b.x, b.y, b.w, b.h, s, size, enabled ? RGB(255, 255, 255) : RGB(140, 152, 172), true);   // 비활성은 회색 글자
}
static void title(HDC dc, const std::wstring& t, const std::wstring& sub) {
    circle(dc, 26, 26, 6, COL_MAIN, COL_MAIN, 1);
    text(dc, 40, 10, 400, 32, t, 24, COL_TEXT, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    text(dc, 40, 40, 580, 22, sub, 15, COL_SUB, false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}
static void poly(HDC dc, const POINT* p, int n, COLORREF fill, COLORREF border, int pw = 2) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, pw, border);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, pen);
    Polygon(dc, p, n);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(pen);
}
// 많이 그리는 사각형용 (브러시를 매번 만들지 않음)
static void fastRect(HDC dc, int x, int y, int w, int h, COLORREF c) {
    SetDCBrushColor(dc, c);
    RECT r{x, y, x + w, y + h};
    FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}
static COLORREF mix(COLORREF a, COLORREF b, double t) {
    return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t), (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
               (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}
static bool keyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
// 키보드가 게임에 들어가는 상태인지 (창이 앞에 있고, 채팅 입력 중이 아님)
static bool gameInput() { return GetForegroundWindow() == g_hwnd && GetFocus() != hInput; }

// ═════════════════════════ 설정 저장 (닉네임 · 색 · 장식 · 판 테마) ═════════════════════════
static std::wstring g_savedName;
static std::wstring cfgPath() {
    wchar_t b[MAX_PATH] = L"";
    GetEnvironmentVariableW(L"APPDATA", b, MAX_PATH);
    std::wstring d = std::wstring(b) + L"\\MinigameParty";
    CreateDirectoryW(d.c_str(), nullptr);
    return d + L"\\settings.ini";
}
static void loadCfg() {
    std::wstring p = cfgPath();
    g_myColor = (int)(GetPrivateProfileIntW(L"me", L"color", 0, p.c_str()) % 8);
    g_myHat = (int)(GetPrivateProfileIntW(L"me", L"hat", 1, p.c_str()) % HAT_COUNT);
    g_theme = (int)(GetPrivateProfileIntW(L"me", L"theme", 0, p.c_str()) % THEME_COUNT);
    g_myPiece = (int)(GetPrivateProfileIntW(L"me", L"piece", 0, p.c_str()) % PIECE_COUNT);
    wchar_t nm[64] = L"";
    GetPrivateProfileStringW(L"me", L"name", L"", nm, 64, p.c_str());
    g_savedName = nm;
}
static void saveCfg() {
    std::wstring p = cfgPath();
    WritePrivateProfileStringW(L"me", L"color", I(g_myColor).c_str(), p.c_str());
    WritePrivateProfileStringW(L"me", L"hat", I(g_myHat).c_str(), p.c_str());
    WritePrivateProfileStringW(L"me", L"theme", I(g_theme).c_str(), p.c_str());
    WritePrivateProfileStringW(L"me", L"piece", I(g_myPiece).c_str(), p.c_str());
    if (!g_myName.empty()) WritePrivateProfileStringW(L"me", L"name", g_myName.c_str(), p.c_str());
}

// ═════════════════════════ 참가자 ═════════════════════════
const int GROUP_MAX = 8;
struct GPlayer {
    std::string pid;
    std::wstring name;
    int score = 0;
    bool alive = false;       // 끝말잇기에서 아직 탈락하지 않음
    int color = 0, hat = 0, piece = 0;   // 꾸미기 (색 · 머리 장식 · 윷놀이 말 모양)
};
static std::vector<GPlayer> GP;   // 참가자 (끝말잇기 중에는 이 순서가 차례 순서)

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
static COLORREF pcolor(const std::string& pid) {
    if (pid == g_pid) return PCOL[g_myColor];
    GPlayer* p = findP(pid);
    return p ? PCOL[p->color & 7] : COL_GRAY;
}
static int phat(const std::string& pid) {
    if (pid == g_pid) return g_myHat;
    GPlayer* p = findP(pid);
    return p ? p->hat % HAT_COUNT : 0;
}
static int ppiece(const std::string& pid) {
    if (pid == g_pid) return g_myPiece;
    GPlayer* p = findP(pid);
    return p ? p->piece % PIECE_COUNT : 0;
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
        s += p.pid + "," + std::to_string(p.score) + "," + (p.alive ? "1" : "0") + "," + std::to_string(p.color) + "," +
             std::to_string(p.hat) + "," + std::to_string(p.piece) + "," + U8(p.name);
    }
    sendMsg("ROSTER", s);
}
static void grpParseRoster(const std::string& s) {
    GP.clear();
    for (auto& e : split(s, ';')) {
        auto f = split(e, ',');
        if (f.size() < 7) continue;
        GPlayer p;
        p.pid = f[0];
        p.score = atoi(f[1].c_str());
        p.alive = f[2] == "1";
        p.color = atoi(f[3].c_str()) & 7;
        p.hat = (atoi(f[4].c_str()) % HAT_COUNT + HAT_COUNT) % HAT_COUNT;
        p.piece = (atoi(f[5].c_str()) % PIECE_COUNT + PIECE_COUNT) % PIECE_COUNT;
        p.name = W(f[6]);
        GP.push_back(p);
    }
}

// ── 아바타: 머리 장식과 얼굴 (격투 · 페인트볼 · 참가자 목록 · 꾸미기에서 같이 씀) ──
static void drawHat(HDC dc, int cx, int cy, int r, int hat, COLORREF col) {   // cx,cy = 머리 중심, r = 머리 반지름
    COLORREF dk = darker(col, 0.7);
    int pw = std::max(1, r / 8);
    switch (hat) {
        case 1:   // 머리띠
            line(dc, cx - r, cy - r * 4 / 10, cx + r, cy - r * 4 / 10, col, std::max(2, r / 3));
            break;
        case 2: {   // 왕관
            POINT p[7] = {{cx - r, cy - r * 6 / 10},      {cx - r, cy - r * 15 / 10}, {cx - r / 2, cy - r * 11 / 10}, {cx, cy - r * 17 / 10},
                          {cx + r / 2, cy - r * 11 / 10}, {cx + r, cy - r * 15 / 10}, {cx + r, cy - r * 6 / 10}};
            poly(dc, p, 7, RGB(255, 205, 60), RGB(200, 150, 20), pw);
            break;
        }
        case 3: {   // 고깔
            POINT p[3] = {{cx - r * 8 / 10, cy - r * 6 / 10}, {cx, cy - r * 22 / 10}, {cx + r * 8 / 10, cy - r * 6 / 10}};
            poly(dc, p, 3, col, dk, pw);
            circle(dc, cx, cy - r * 22 / 10, std::max(2, r / 4), RGB(255, 255, 255), dk, pw);
            break;
        }
        case 4: {   // 고양이 귀
            POINT a[3] = {{cx - r, cy - r * 4 / 10}, {cx - r * 9 / 10, cy - r * 16 / 10}, {cx - r * 2 / 10, cy - r * 9 / 10}};
            POINT b[3] = {{cx + r, cy - r * 4 / 10}, {cx + r * 9 / 10, cy - r * 16 / 10}, {cx + r * 2 / 10, cy - r * 9 / 10}};
            poly(dc, a, 3, col, dk, pw);
            poly(dc, b, 3, col, dk, pw);
            break;
        }
        case 5: {   // 뿔
            POINT a[3] = {{cx - r * 9 / 10, cy - r * 5 / 10}, {cx - r * 12 / 10, cy - r * 17 / 10}, {cx - r * 3 / 10, cy - r * 9 / 10}};
            POINT b[3] = {{cx + r * 9 / 10, cy - r * 5 / 10}, {cx + r * 12 / 10, cy - r * 17 / 10}, {cx + r * 3 / 10, cy - r * 9 / 10}};
            poly(dc, a, 3, RGB(235, 70, 70), RGB(160, 40, 40), pw);
            poly(dc, b, 3, RGB(235, 70, 70), RGB(160, 40, 40), pw);
            break;
        }
    }
}
static void drawHead(HDC dc, int cx, int cy, int r, COLORREF col, int hat) {
    circle(dc, cx, cy, r, RGB(255, 222, 190), darker(col, 0.7), std::max(1, r / 7));
    int e = std::max(1, r / 7);
    circle(dc, cx - r * 4 / 10, cy + r / 10, e, COL_TEXT, COL_TEXT, 1);
    circle(dc, cx + r * 4 / 10, cy + r / 10, e, COL_TEXT, COL_TEXT, 1);
    drawHat(dc, cx, cy, r, hat, col);
}
// 윷놀이 말: 각자 고른 모양 (0 동글, 1 별, 2 하트, 3 보석, 4 고양이, 5 내 얼굴)
static void drawPiece(HDC dc, int cx, int cy, int r, int shape, COLORREF col, int hat) {
    COLORREF dk = darker(col, 0.62), hi = mix(col, RGB(255, 255, 255), 0.7);
    int pw = std::max(1, r / 6);
    switch (shape) {
        case 1: {   // 별
            POINT p[10];
            for (int i = 0; i < 10; i++) {
                double a = -1.5708 + i * 0.62832, rr = (i % 2) ? r * 0.52 : r * 1.18;
                p[i] = {(LONG)std::lround(cx + std::cos(a) * rr), (LONG)std::lround(cy + std::sin(a) * rr)};
            }
            poly(dc, p, 10, col, dk, pw);
            break;
        }
        case 2: {   // 하트
            POINT p[24];
            for (int i = 0; i < 24; i++) {
                double t = i * 6.28318 / 24, sn = std::sin(t);
                double hx = 16 * sn * sn * sn, hy = -(13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t));
                p[i] = {(LONG)std::lround(cx + hx * r / 14.5), (LONG)std::lround(cy - r * 0.12 + hy * r / 14.5)};
            }
            poly(dc, p, 24, col, dk, pw);
            break;
        }
        case 3: {   // 보석
            POINT p[4] = {{cx, cy - r * 12 / 10}, {cx + r, cy}, {cx, cy + r * 12 / 10}, {cx - r, cy}};
            poly(dc, p, 4, col, dk, pw);
            line(dc, cx - r / 2, cy, cx, cy - r * 6 / 10, hi, pw);
            return;
        }
        case 4: {   // 고양이
            POINT a[3] = {{cx - r, cy - r * 3 / 10}, {cx - r * 9 / 10, cy - r * 13 / 10}, {cx - r * 2 / 10, cy - r * 8 / 10}};
            POINT b[3] = {{cx + r, cy - r * 3 / 10}, {cx + r * 9 / 10, cy - r * 13 / 10}, {cx + r * 2 / 10, cy - r * 8 / 10}};
            poly(dc, a, 3, col, dk, pw);
            poly(dc, b, 3, col, dk, pw);
            circle(dc, cx, cy, r * 95 / 100, col, dk, pw);
            int e = std::max(1, r / 6);
            circle(dc, cx - r * 4 / 10, cy - r / 10, e, RGB(255, 255, 255), RGB(255, 255, 255), 1);
            circle(dc, cx + r * 4 / 10, cy - r / 10, e, RGB(255, 255, 255), RGB(255, 255, 255), 1);
            circle(dc, cx, cy + r * 3 / 10, std::max(1, r / 8), dk, dk, 1);
            return;
        }
        case 5:   // 내 얼굴 (머리 장식 포함)
            drawHead(dc, cx, cy + r / 6, r * 9 / 10, col, hat);
            return;
        default:
            circle(dc, cx, cy, r * 93 / 100, col, dk, pw);
    }
    circle(dc, cx - r / 3, cy - r / 3, std::max(1, r / 5), hi, hi, 1);
}

// ═════════════════════════ 대결 (초대해서 하는 게임) 공통 ═════════════════════════
// 초대한 사람이 참가자 순서(m)와 난수 씨앗(seed)을 정해 모두에게 알리면, 각자 같은 상태로 게임을 시작한다.
struct Match {
    bool on = false;
    std::string id;
    int view = 0;
    std::vector<std::string> m;   // 참가자 (자리 순서, 게임 도중에는 바뀌지 않음)
    std::vector<char> here;       // 아직 대결에 남아 있는지
    std::vector<std::wstring> names;   // 시작할 때의 이름 (방을 나간 사람도 표시하려고)
    unsigned seed = 0;
    bool reported = false;        // 이번 판 결과를 이미 알렸는지
};
static Match M;
static std::map<std::string, std::string> g_inMatch;   // 방 전체: 누가 어느 대결에 들어가 있는지

static int mIdx(const std::string& pid) {
    for (int i = 0; i < (int)M.m.size(); i++)
        if (M.m[i] == pid) return i;
    return -1;
}
static int mMe() { return mIdx(g_pid); }
static int mCount() {
    int n = 0;
    for (char h : M.here) n += h ? 1 : 0;
    return n;
}
static int mLeaderIdx() {
    for (int i = 0; i < (int)M.here.size(); i++)
        if (M.here[i]) return i;
    return -1;
}
static bool mLeader() { int l = mLeaderIdx(); return M.on && l >= 0 && M.m[l] == g_pid; }
static std::wstring mName(int i) {
    if (i < 0 || i >= (int)M.m.size()) return L"?";
    if (M.m[i] == g_pid) return L"나";
    if (findP(M.m[i])) return pname(M.m[i]);
    return i < (int)M.names.size() ? M.names[i] : L"?";
}
static COLORREF mCol(int i) { return i < 0 || i >= (int)M.m.size() ? COL_GRAY : pcolor(M.m[i]); }
static int mHat(int i) { return i < 0 || i >= (int)M.m.size() ? 0 : phat(M.m[i]); }
static int mPiece(int i) { return i < 0 || i >= (int)M.m.size() ? 0 : ppiece(M.m[i]); }
static void reportResult(int winIdx);   // 판이 끝났을 때 (winIdx < 0 = 무승부). 대표 한 명만 방 전체에 알림
static void hostAward(const std::vector<std::string>& pids, int pts);
static void updateStrip();

// "다시 하기" 버튼: 대결 대표(남아 있는 첫 번째 사람)만 누를 수 있다
static Btn g_againBtn{0, 0, 0, 0};
static void drawAgain(HDC dc, int x, int y, int w, int h) {
    if (mLeader()) {
        g_againBtn = {x, y, w, h};
        button(dc, g_againBtn, L"다시 하기", true, COL_ME, 18);
    } else {
        int tx = std::max(4, x - 20), tw = std::min(CW - 4 - tx, w + 40);   // 화면 밖으로 나가지 않게 두 줄로
        text(dc, tx, y, tw, h, mName(mLeaderIdx()) + L" 님이 '다시 하기'를 누를 수 있어요", 13, COL_SUB, true, DT_CENTER | DT_WORDBREAK);
    }
}
static void resultBox(HDC dc, int x, int y, int w, int h, const std::wstring& s, COLORREF c, int size = 36) {
    roundRect(dc, x, y + 5, w, h, mix(c, RGB(255, 255, 255), 0.6), mix(c, RGB(255, 255, 255), 0.6), 24, 1);
    roundRect(dc, x, y, w, h, RGB(255, 255, 255), c, 24, 4);
    text(dc, x, y, w, h, s, size, c, true);
}

static int fI(const std::vector<std::string>& f, size_t i) { return i < f.size() ? atoi(f[i].c_str()) : 0; }
static std::string fS(const std::vector<std::string>& f, size_t i) { return i < f.size() ? f[i] : std::string(); }

// ═════════════════════════ 오목 (2명) ═════════════════════════
const int OM_N = 15, OM_X = 40, OM_Y = 80, OM_S = 27;   // 15줄, 칸 간격 27
static struct OmokState {
    bool on = false;
    char b[OM_N * OM_N];        // 0 빈칸, 1 선공 돌, 2 후공 돌
    int seat[3] = {-1, 0, 1};   // seat[1] = 선공 자리, seat[2] = 후공 자리
    char my = 0, turn = 1;
    int moves = 0, last = -1;
    std::wstring result;
    COLORREF rc = 0;
} O;
static const Btn omResign{452, 424, 170, 38};

static void omStart() {
    O = OmokState();
    memset(O.b, 0, sizeof(O.b));
    O.on = true;
    O.seat[1] = (int)(M.seed % 2);
    O.seat[2] = 1 - O.seat[1];
    int me = mMe();
    O.my = me == O.seat[1] ? 1 : me == O.seat[2] ? 2 : 0;
    gmsg(L"오목 시작! " + mName(O.seat[1]) + L" 선공");
}
static void omEnd(int winStone, const std::wstring& how) {
    if (!O.on) return;
    O.on = false;
    if (winStone == 0) { O.result = L"무승부!"; O.rc = COL_ACC; }
    else if (winStone == O.my) { O.result = how.empty() ? L"승리!" : how + L"\n승리!"; O.rc = COL_WINC; }
    else { O.result = how.empty() ? L"패배..." : how; O.rc = COL_OP; }
    reportResult(winStone ? O.seat[winStone] : -1);
    invalidateCanvas();
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
    O.turn = (char)(3 - c);
    if (omFive(p)) omEnd(c, L"");
    else if (O.moves == OM_N * OM_N) omEnd(0, L"");
    invalidateCanvas();
}
static void omMsg(int idx, const std::string& type, const std::vector<std::string>& f) {
    char c = idx == O.seat[1] ? 1 : idx == O.seat[2] ? 2 : 0;
    if (!O.on || !c) return;
    if (type == "O_MOVE") {
        int p = fI(f, 0);
        if (c == O.turn && p >= 0 && p < OM_N * OM_N && !O.b[p]) omPlace(p, c);
    } else if (type == "O_QUIT") {
        gmsg(mName(idx) + L" 님이 오목을 기권했습니다.");
        omEnd(3 - c, L"상대 기권");
    }
}
static void omLeft(int idx) {
    char c = idx == O.seat[1] ? 1 : idx == O.seat[2] ? 2 : 0;
    if (O.on && c) omEnd(3 - c, L"상대가 나감");
}
static void omClick(int x, int y) {
    if (!O.on || !O.my) return;
    if (omResign.hit(x, y)) {
        sendMsg("O_QUIT");
        gmsg(L"오목을 기권했습니다.");
        omEnd(3 - O.my, L"기권패");
        return;
    }
    if (O.turn != O.my) return;
    int ix = (int)std::lround((x - OM_X) / (double)OM_S), iy = (int)std::lround((y - OM_Y) / (double)OM_S);
    if (ix < 0 || ix >= OM_N || iy < 0 || iy >= OM_N) return;
    int p = iy * OM_N + ix;
    if (O.b[p]) return;
    sendMsg("O_MOVE", std::to_string(p));
    omPlace(p, O.my);
}
// 돌은 각자 꾸미기에서 고른 색. 두 사람 색이 같으면 후공 돌을 밝게 해서 구분
static COLORREF omColor(int stone) {
    COLORREF c = mCol(O.seat[stone]);
    if (stone == 2 && c == mCol(O.seat[1])) c = mix(c, RGB(255, 255, 255), 0.6);
    return c;
}
static void omStone(HDC dc, int cx, int cy, int r, int stone) {
    COLORREF c = omColor(stone);
    circle(dc, cx, cy, r, c, darker(c, 0.6), 2);
    circle(dc, cx - r / 3, cy - r / 3, std::max(1, r / 4), mix(c, RGB(255, 255, 255), 0.65), mix(c, RGB(255, 255, 255), 0.65), 1);
}
static void omDraw(HDC dc) {
    const Theme& T = THEMES[g_theme];
    std::wstring sub;
    if (O.on) sub = O.turn == O.my ? L"내 차례! 돌을 놓을 곳을 클릭하세요. 5목을 먼저 만들면 승리" : mName(O.seat[(int)O.turn]) + L" 님의 차례입니다.";
    else sub = L"게임 종료";
    title(dc, L"오목", sub);
    int E = (OM_N - 1) * OM_S;
    roundRect(dc, OM_X - 16, OM_Y - 16, E + 32, E + 32, T.board, T.edge, 14, 3);
    for (int i = 0; i < OM_N; i++) {
        line(dc, OM_X, OM_Y + i * OM_S, OM_X + E, OM_Y + i * OM_S, T.line, 1);
        line(dc, OM_X + i * OM_S, OM_Y, OM_X + i * OM_S, OM_Y + E, T.line, 1);
    }
    static const int STAR[5][2] = {{3, 3}, {11, 3}, {3, 11}, {11, 11}, {7, 7}};
    for (auto& s : STAR) circle(dc, OM_X + s[0] * OM_S, OM_Y + s[1] * OM_S, 3, T.line, T.line, 1);
    for (int p = 0; p < OM_N * OM_N; p++) {
        if (!O.b[p]) continue;
        int cx = OM_X + (p % OM_N) * OM_S, cy = OM_Y + (p / OM_N) * OM_S;
        omStone(dc, cx, cy, 12, O.b[p]);
        if (p == O.last) ring(dc, cx, cy, 15, COL_ACC, 3);
    }
    // 오른쪽 패널
    for (int s = 1; s <= 2; s++) {
        int y = 80 + (s - 1) * 34;
        omStone(dc, 466, y + 12, 11, s);
        text(dc, 484, y, 150, 24, mName(O.seat[s]) + (s == 1 ? L" (선공)" : L""), 15, COL_TEXT, true,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (O.on) {
        bool mine = O.turn == O.my;
        text(dc, 452, 160, 175, 34, mine ? L"▶ 내 차례" : L"▶ 상대 차례", 20, mine ? COL_ME : COL_OP, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        text(dc, 452, 196, 175, 22, I(O.moves) + L"수 진행", 14, COL_SUB, false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        button(dc, omResign, L"기권", true, COL_GRAY, 15);
    } else {
        text(dc, 452, 230, 175, 90, O.result, 30, O.rc, true, DT_CENTER | DT_WORDBREAK);
        drawAgain(dc, 452, 372, 170, 44);
    }
}

// ═════════════════════════ 윷놀이 (2~4명) ═════════════════════════
// 판 위치: 0=출발점(오른쪽 아래), 1~19 바깥 둘레(반시계), 20~24 오른쪽위→가운데(22)→왼쪽아래,
//          25~28 왼쪽위→가운데→출발점.  -1=대기, 99=완주
static const int HOME = -1, DONE = 99;
static const wchar_t* YUT_N[6] = {L"", L"도", L"개", L"걸", L"윷", L"모"};
static struct YutState {
    bool on = false;
    int n = 2, np = 4;   // 인원, 한 사람의 말 수 (2명 4개 · 3명 3개 · 4명 2개)
    int turn = 0, winner = -1;
    int pos[4][4];
    int throwsLeft = 0;
    std::vector<int> pending;
    int sel = -1, last = 0;
    int sticks[4] = {0, 0, 0, 0};   // 1 = 배(평평한 면)
    std::wstring note;
} Y;
static const Btn yutMainBtn{400, 222, 225, 48}, yutHomeBtn{400, 380, 225, 40};
static Btn yutChip(int i) { return {400 + (i % 4) * 57, 305 + (i / 4) * 36, 52, 32}; }

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
static void yutNextTurn() {
    for (int k = 0; k < Y.n; k++) {
        Y.turn = (Y.turn + 1) % Y.n;
        if (M.here[Y.turn]) break;
    }
    Y.throwsLeft = 1;
    Y.pending.clear();
    Y.sel = -1;
}
static void yutStart() {
    Y = YutState();
    Y.on = true;
    Y.n = std::min(4, (int)M.m.size());
    Y.np = Y.n == 2 ? 4 : Y.n == 3 ? 3 : 2;
    for (auto& s : Y.pos)
        for (int& p : s) p = HOME;
    Y.turn = (int)(M.seed % Y.n);
    if (!M.here[Y.turn]) yutNextTurn();
    Y.throwsLeft = 1;
    gmsg(L"윷놀이 시작! " + I(Y.n) + L"명 · 말 " + I(Y.np) + L"개씩 · " + mName(Y.turn) + L" 먼저");
}
static void yutApplyThrow(int res, const std::string& st) {
    if (res < 1 || res > 5) return;
    Y.last = res;
    for (int i = 0; i < 4; i++) Y.sticks[i] = (i < (int)st.size() && st[i] == '1') ? 1 : 0;
    Y.throwsLeft--;
    Y.pending.push_back(res);
    std::wstring who = mName(Y.turn);
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
    if (pi < 0 || pi >= (int)Y.pending.size() || piece < 0 || piece >= Y.np) return;
    int seat = Y.turn;
    int start = Y.pos[seat][piece];
    if (start == DONE) return;
    int steps = Y.pending[pi];
    Y.pending.erase(Y.pending.begin() + pi);
    int dest = yutWalk(start, steps);
    int moved = 0;
    for (int k = 0; k < Y.np; k++) {
        if ((start == HOME && k == piece) || (start != HOME && Y.pos[seat][k] == start)) {
            Y.pos[seat][k] = dest;
            moved++;
        }
    }
    std::wstring who = mName(seat);
    std::wstring msg = who + L" " + YUT_N[steps] + (steps == 4 ? L"으로 이동" : L"로 이동") + (moved > 1 ? L" (말 " + I(moved) + L"개 업고)" : L"");
    if (dest == DONE) msg += L" → 완주!";
    else {
        int caught = 0;
        std::wstring whom;
        for (int s = 0; s < Y.n; s++) {
            if (s == seat) continue;
            bool any = false;
            for (int k = 0; k < Y.np; k++)
                if (Y.pos[s][k] == dest) { Y.pos[s][k] = HOME; caught++; any = true; }
            if (any) whom += (whom.empty() ? L"" : L", ") + mName(s);
        }
        if (caught) {
            Y.throwsLeft++;
            msg += L" → " + whom + L"의 말 " + I(caught) + L"개 잡음! 한 번 더!";
        }
    }
    Y.note = msg;
    gmsg(L"윷놀이 " + msg);
    bool all = true;
    for (int k = 0; k < Y.np; k++) all = all && Y.pos[seat][k] == DONE;
    if (all) {
        Y.winner = seat;
        Y.on = false;
        reportResult(seat);
    } else if (Y.throwsLeft == 0 && Y.pending.empty()) {
        yutNextTurn();
    }
    Y.sel = -1;
    invalidateCanvas();
}
static void yutMsg(int idx, const std::string& type, const std::vector<std::string>& f) {
    if (!Y.on || idx != Y.turn) return;
    if (type == "Y_THROW") { if (Y.throwsLeft > 0) yutApplyThrow(fI(f, 0), fS(f, 1)); }
    else if (type == "Y_MOVE") yutApplyMove(fI(f, 0), fI(f, 1));
}
static void yutLeft(int idx) {
    if (idx < 0 || idx >= 4) return;
    for (int& p : Y.pos[idx]) p = HOME;
    if (Y.on && Y.turn == idx) yutNextTurn();
}
static void yutThrow() {
    std::string st;
    int flats = 0;
    for (int i = 0; i < 4; i++) {
        int f = std::uniform_int_distribution<int>(0, 99)(g_rng) < 55 ? 1 : 0;   // 배가 조금 더 잘 나옴 (실제 윷과 비슷하게)
        flats += f;
        st += f ? '1' : '0';
    }
    int res = flats == 0 ? 5 : flats;
    sendMsg("Y_THROW", std::to_string(res) + "|" + st);
    yutApplyThrow(res, st);
}
static void yutMove(int piece) {
    int sel = Y.sel >= 0 ? Y.sel : (Y.pending.size() == 1 ? 0 : -1);
    if (sel < 0) {
        Y.note = L"먼저 오른쪽에서 사용할 결과(도/개/걸...)를 고르세요";
        invalidateCanvas();
        return;
    }
    sendMsg("Y_MOVE", std::to_string(piece) + "|" + std::to_string(sel));
    yutApplyMove(piece, sel);
}
static void yutClick(int x, int y) {
    int me = mMe();
    if (!Y.on || me < 0 || Y.turn != me) return;
    if (Y.throwsLeft > 0) {
        if (yutMainBtn.hit(x, y)) yutThrow();
        return;
    }
    for (int i = 0; i < (int)Y.pending.size(); i++)
        if (yutChip(i).hit(x, y)) { Y.sel = i; invalidateCanvas(); return; }
    if (yutHomeBtn.hit(x, y)) {
        for (int k = 0; k < Y.np; k++)
            if (Y.pos[me][k] == HOME) { yutMove(k); return; }
        return;
    }
    for (int k = 0; k < Y.np; k++) {
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
static void yutDraw(HDC dc) {
    const Theme& T = THEMES[g_theme];
    int me = mMe();
    bool myTurn = Y.on && Y.turn == me;
    std::wstring sub;
    if (!Y.on) sub = L"게임 종료";
    else if (!myTurn) sub = mName(Y.turn) + L" 님의 차례입니다.";
    else if (Y.throwsLeft > 0) sub = L"내 차례! 윷을 던지세요.";
    else sub = L"결과를 고르고, 움직일 내 말(또는 대기 말)을 클릭하세요.";
    title(dc, L"윷놀이", sub);

    // 판
    roundRect(dc, 28, 73, 354, 354, T.board, T.edge, 24, 3);
    POINT BR = yutXY(0), TR = yutXY(5), TL = yutXY(10), BL = yutXY(15);
    line(dc, BR.x, BR.y, TR.x, TR.y, T.line, 3);
    line(dc, TR.x, TR.y, TL.x, TL.y, T.line, 3);
    line(dc, TL.x, TL.y, BL.x, BL.y, T.line, 3);
    line(dc, BL.x, BL.y, BR.x, BR.y, T.line, 3);
    line(dc, TR.x, TR.y, BL.x, BL.y, T.line, 3);
    line(dc, TL.x, TL.y, BR.x, BR.y, T.line, 3);
    for (int p = 0; p <= 28; p++) {
        POINT c = yutXY(p);
        bool big = (p % 5 == 0 && p < 20) || p == 22;
        circle(dc, c.x, c.y, big ? 17 : 11, T.node, T.line, 2);
    }
    text(dc, BR.x - 120, BR.y + 5, 96, 18, L"출발 · 도착 ▶", 12, T.line, true, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    // 말 (각자 고른 색)
    for (int s = 0; s < Y.n; s++) {
        if (!M.here[s]) continue;
        COLORREF c = mCol(s);
        std::map<int, int> cnt;
        for (int k = 0; k < Y.np; k++)
            if (Y.pos[s][k] >= 0 && Y.pos[s][k] <= 28) cnt[Y.pos[s][k]]++;
        for (auto& kv : cnt) {
            POINT pt = yutXY(kv.first);
            if (s == me && myTurn && Y.throwsLeft == 0) ring(dc, pt.x, pt.y, 17, COL_ACC, 4);
            drawPiece(dc, pt.x, pt.y, 14, mPiece(s), c, mHat(s));
            if (kv.second > 1) {   // 업은 말 수
                circle(dc, pt.x + 12, pt.y - 11, 8, RGB(255, 255, 255), darker(c, 0.6), 2);
                text(dc, pt.x + 4, pt.y - 19, 16, 16, I(kv.second), 11, COL_TEXT, true);
            }
        }
    }

    // 오른쪽 패널
    if (Y.on)
        text(dc, 400, 60, 225, 30, myTurn ? L"▶ 내 차례" : L"▶ " + mName(Y.turn) + L" 차례", 20, mCol(Y.turn), true,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    for (int i = 0; i < 4; i++) drawStick(dc, 404 + i * 44, 95, Y.last ? Y.sticks[i] == 1 : (i % 2 == 0));
    if (Y.last) text(dc, 585, 95, 45, 86, YUT_N[Y.last], 26, COL_TEXT, true);
    text(dc, 400, 186, 230, 34, Y.note, 13, COL_SUB, false, DT_LEFT | DT_WORDBREAK);

    if (Y.on) {
        button(dc, yutMainBtn, L"윷 던지기", myTurn && Y.throwsLeft > 0, COL_ME, 20);
        text(dc, 400, 278, 225, 24, L"남은 이동:", 14, COL_SUB, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int autoSel = Y.sel >= 0 ? Y.sel : (Y.pending.size() == 1 ? 0 : -1);
        for (int i = 0; i < (int)Y.pending.size() && i < 8; i++) {
            Btn b = yutChip(i);
            bool sel = i == autoSel && myTurn && Y.throwsLeft == 0;
            roundRect(dc, b.x, b.y, b.w, b.h, sel ? COL_ACC : RGB(255, 255, 255), sel ? darker(COL_ACC) : COL_LINE, 12, 2);
            text(dc, b.x, b.y, b.w, b.h, YUT_N[Y.pending[i]], 18, sel ? RGB(255, 255, 255) : COL_TEXT, true);
        }
        int myHome = 0;
        if (me >= 0)
            for (int k = 0; k < Y.np; k++) myHome += Y.pos[me][k] == HOME;
        button(dc, yutHomeBtn, L"대기 말 " + I(myHome) + L"개  ▶ 새로 출발", myTurn && Y.throwsLeft == 0 && myHome > 0, COL_ME, 15);
    } else {
        drawAgain(dc, 420, 300, 180, 46);
    }

    // 아래: 사람별 대기 · 완주
    int colW = Y.n <= 2 ? 300 : Y.n == 3 ? 200 : 152;
    for (int s = 0; s < Y.n; s++) {
        int x = 28 + s * colW, home = 0, done = 0;
        for (int k = 0; k < Y.np; k++) { home += Y.pos[s][k] == HOME; done += Y.pos[s][k] == DONE; }
        COLORREF c = M.here[s] ? mCol(s) : COL_GRAY;
        drawPiece(dc, x + 10, 447, 9, M.here[s] ? mPiece(s) : 0, c, mHat(s));
        text(dc, x + 24, 434, colW - 30, 22, mName(s), 14, M.here[s] ? COL_TEXT : COL_GRAY, true,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        text(dc, x + 24, 455, colW - 30, 20, M.here[s] ? L"대기 " + I(home) + L" · 완주 " + I(done) + L"/" + I(Y.np) : std::wstring(L"나감"), 12,
             COL_SUB, false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    if (Y.winner >= 0) {
        bool w = Y.winner == me;
        resultBox(dc, 70, 195, 270, 80, w ? L"승리! 🎉" : mName(Y.winner) + L" 승리", w ? COL_WINC : COL_OP, w ? 36 : 24);
    }
}

// ═════════════════════════ 격투 (2~4명 난투) ═════════════════════════
// 각자 자기 캐릭터만 계산해서 상태를 보내고, 맞았는지는 때린 쪽이 판정해 알린다. 마지막까지 남으면 승리.
const float GROUND = 400.f;
struct Fighter {
    float x = 160, y = GROUND, vy = 0, kb = 0;
    int facing = 1, hp = 100, act = 0, actT = 0, stun = 0;
    bool guard = false;
};
struct AtkInfo { int dur, a0, a1, range, dmg; };
static const AtkInfo ATK[3] = {{0, 0, 0, 0, 0}, {16, 3, 9, 72, 7}, {26, 8, 15, 98, 12}};
static struct FightState {
    bool on = false;
    int n = 0, me = -1, countdown = 0, goT = 0, tick = 0, fxT = 0, winner = -2;   // winner: -2 진행 중, -1 무승부
    float fxX = 0, fxY = 0;
    bool hitDone = false, pW = false, pP = false, pK = false;
    bool qW = false, qP = false, qK = false;   // 프레임 사이에 짧게 눌렀다 뗀 키 (WM_KEYDOWN에서 기록)
    Fighter f[4];
    float dx[4] = {0, 0, 0, 0}, dy[4] = {GROUND, GROUND, GROUND, GROUND};   // 화면에 그릴 위치 (지연을 부드럽게 보정)
    bool dead[4] = {true, true, true, true};
    double acc = 0, sendT = 0;
} F;

static void fightStart() {
    F = FightState();
    F.on = true;
    F.n = std::min(4, (int)M.m.size());
    F.me = mMe();
    F.countdown = 180;
    for (int i = 0; i < F.n; i++) {
        F.f[i].x = F.n == 1 ? 320.f : 90.f + i * 460.f / (F.n - 1);
        F.f[i].facing = F.f[i].x < 320 ? 1 : -1;
        F.dx[i] = F.f[i].x;
        F.dead[i] = !M.here[i];
    }
    gmsg(L"격투 시작! 게임 화면을 클릭한 뒤 키보드로 조작하세요. 마지막까지 남으면 승리!");
    SetFocus(g_hwnd);
}
static int fightNearest(int self) {
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < F.n; i++) {
        if (i == self || F.dead[i]) continue;
        float d = std::fabs(F.f[i].x - F.f[self].x);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
static void fightCheckEnd() {
    if (!F.on || F.winner != -2) return;
    int alive = 0, last = -1;
    for (int i = 0; i < F.n; i++)
        if (!F.dead[i]) { alive++; last = i; }
    if (alive > 1) return;
    F.winner = alive == 1 ? last : -1;
    if (F.winner >= 0) gmsg(L"격투: " + mName(F.winner) + (F.winner == F.me ? L" 승리! 🎉" : L" 님 승리!"));
    reportResult(F.winner);
}
static void fightSendState() {
    const Fighter& m = F.f[F.me];
    char buf[128];
    snprintf(buf, sizeof(buf), "%d|%d|%d|%d|%d|%d|%d|%d", (int)m.x, (int)m.y, m.facing, m.act, m.actT, m.guard ? 1 : 0, m.hp, m.stun);
    sendMsg("F_S", buf);
}
static void fightStep() {   // 1/60초 한 걸음
    F.tick++;
    if (F.fxT > 0) F.fxT--;
    if (F.goT > 0) F.goT--;
    if (F.countdown > 0 && --F.countdown == 0) F.goT = 45;
    for (int i = 0; i < F.n; i++) {
        if (i == F.me) continue;
        F.dx[i] += (F.f[i].x - F.dx[i]) * 0.35f;
        F.dy[i] += (F.f[i].y - F.dy[i]) * 0.5f;
    }
    if (F.me < 0 || F.dead[F.me]) return;
    Fighter& m = F.f[F.me];
    bool canInput = F.countdown == 0 && F.winner == -2 && gameInput();
    bool L = false, Rt = false, J = false, G = false, P = false, K = false;
    if (canInput) {
        L = keyDown('A') || keyDown(VK_LEFT);
        Rt = keyDown('D') || keyDown(VK_RIGHT);
        J = keyDown('W') || keyDown(VK_UP);
        G = keyDown('S') || keyDown(VK_DOWN);
        P = keyDown('J') || keyDown('Z');
        K = keyDown('K') || keyDown('X');
    }
    // 짧게 톡 친 키도 놓치지 않도록: 눌림 기록이 있으면 이번 프레임에 새로 누른 것으로 처리
    bool newW = (J && !F.pW) || (canInput && F.qW), newP = (P && !F.pP) || (canInput && F.qP),
         newK = (K && !F.pK) || (canInput && F.qK);
    F.qW = F.qP = F.qK = false;
    bool onGround = m.y >= GROUND - 0.5f;
    if (m.stun > 0) {
        m.stun--;
        m.guard = false;
    } else {
        m.guard = G && onGround && m.act == 0;
        if (m.act == 0 && !m.guard) {
            if (L) m.x -= 4.5f;
            if (Rt) m.x += 4.5f;
            if (newW && onGround) m.vy = -13.5f;
            if (newP) { m.act = 1; m.actT = 0; F.hitDone = false; }
            else if (newK) { m.act = 2; m.actT = 0; F.hitDone = false; }
        }
    }
    F.pW = J; F.pP = P; F.pK = K;
    int nr = fightNearest(F.me);
    if (m.act == 0 && m.stun == 0 && nr >= 0) {
        if (L != Rt && F.n > 2) m.facing = Rt ? 1 : -1;   // 여럿이 싸울 때는 움직이는 쪽을 봄
        else m.facing = F.f[nr].x >= m.x ? 1 : -1;
    }
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
        if (!F.hitDone && F.winner == -2 && m.actT >= a.a0 && m.actT <= a.a1) {
            int hit = -1;
            float hd = 1e9f;
            for (int i = 0; i < F.n; i++) {   // 앞에 있는 가장 가까운 한 명
                if (i == F.me || F.dead[i]) continue;
                float dx = (F.f[i].x - m.x) * m.facing;
                if (dx > -24 && dx < a.range && std::fabs(F.f[i].y - m.y) < 70 && dx < hd) { hd = dx; hit = i; }
            }
            if (hit >= 0) {
                F.hitDone = true;
                sendMsg("F_HIT", M.m[hit] + "|" + std::to_string(a.dmg) + "|" + std::to_string(m.facing));
                F.fxT = 10;
                F.fxX = F.f[hit].x;
                F.fxY = F.f[hit].y - 60;
            }
        }
        if (m.actT >= a.dur) { m.act = 0; m.actT = 0; }
    }
    F.sendT += 1.0 / 60;
    if (F.sendT >= stateEvery(F.n)) { F.sendT = 0; fightSendState(); }
}
static void fightTick(double dt) {
    if (!F.on) return;
    F.acc += dt;
    int steps = 0;
    while (F.acc >= 1.0 / 60 && steps < 6) { fightStep(); F.acc -= 1.0 / 60; steps++; }
    if (steps == 6) F.acc = 0;
}
static void fightOnHit(int dmg, int dir) {
    if (!F.on || F.winner != -2 || F.me < 0 || F.dead[F.me]) return;
    Fighter& m = F.f[F.me];
    bool blocked = m.guard && m.facing == -dir;
    m.hp -= blocked ? std::max(1, dmg / 5) : dmg;
    m.kb = dir * (blocked ? 2.f : 4.f);
    if (!blocked) { m.stun = 16; m.act = 0; }
    F.fxT = 10;
    F.fxX = m.x;
    F.fxY = m.y - 60;
    if (m.hp <= 0) {
        m.hp = 0;
        F.dead[F.me] = true;
        sendMsg("F_KO");
        gmsg(L"격투: K.O. 당했습니다...");
        fightCheckEnd();
    }
    fightSendState();
}
static void fightMsg(int idx, const std::string& type, const std::vector<std::string>& f) {
    if (!F.on || idx < 0 || idx >= F.n) return;
    if (type == "F_S") {
        if (f.size() >= 8 && !F.dead[idx]) {
            Fighter& o = F.f[idx];
            o.x = (float)fI(f, 0);
            o.y = (float)fI(f, 1);
            o.facing = fI(f, 2) >= 0 ? 1 : -1;
            o.act = std::max(0, std::min(2, fI(f, 3)));
            o.actT = fI(f, 4);
            o.guard = fI(f, 5) != 0;
            o.hp = fI(f, 6);
            o.stun = fI(f, 7);
        }
    } else if (type == "F_HIT") {
        if (fS(f, 0) == g_pid) fightOnHit(std::max(0, std::min(20, fI(f, 1))), fI(f, 2) >= 0 ? 1 : -1);
    } else if (type == "F_KO") {
        if (!F.dead[idx]) {
            F.dead[idx] = true;
            F.f[idx].hp = 0;
            gmsg(L"격투: " + mName(idx) + L" K.O.!");
            fightCheckEnd();
        }
    }
}
static void fightLeft(int idx) {
    if (idx < 0 || idx >= 4) return;
    F.dead[idx] = true;
    fightCheckEnd();
}
static void drawFighter(HDC dc, const Fighter& f, COLORREF col, int hat, const std::wstring& name, bool shadow = true) {
    int x = (int)f.x, y = (int)f.y, d = f.facing;
    bool air = f.y < GROUND - 1 && shadow;
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
    if (shadow) {
        HBRUSH sb = CreateSolidBrush(RGB(95, 140, 70));
        HGDIOBJ ob = SelectObject(dc, sb), op = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, x - 28, (int)GROUND - 5, x + 28, (int)GROUND + 6);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(sb);
    }
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
    // 머리 + 장식 (보는 쪽으로 눈)
    circle(dc, neck.x, neck.y - 14, 14, RGB(255, 222, 190), dk, 2);
    circle(dc, neck.x + d * 6, neck.y - 14, 2, COL_TEXT, COL_TEXT, 1);
    drawHat(dc, neck.x, neck.y - 14, 14, hat, col);
    if (f.guard) text(dc, x - 40, y - 172, 80, 18, L"🛡 방어", 13, dk, true);
    if (!name.empty()) text(dc, x - 60, y - 154, 120, 18, name, 14, dk, true);
}
static void fightDraw(HDC dc) {
    for (int i = 0; i < 20; i++) fastRect(dc, 0, i * 20, CW, 20, RGB(170 + i * 3, 200 + i * 2, 245));
    fastRect(dc, 0, (int)GROUND, CW, CH - (int)GROUND, RGB(120, 170, 90));
    fastRect(dc, 0, (int)GROUND, CW, 6, RGB(95, 140, 70));
    circle(dc, 560, 96, 26, RGB(255, 240, 160), RGB(255, 220, 120), 2);

    // 체력 막대 (사람 수만큼)
    int bw = (CW - 20 - (F.n - 1) * 10) / std::max(1, F.n);
    for (int i = 0; i < F.n; i++) {
        int x = 10 + i * (bw + 10), hp = F.dead[i] ? 0 : std::max(0, F.f[i].hp);
        COLORREF c = mCol(i);
        roundRect(dc, x, 10, bw, 20, RGB(255, 255, 255), darker(c, 0.7), 10, 2);
        int fw = (bw - 6) * hp / 100;
        if (fw > 0) fastRect(dc, x + 3, 13, fw, 14, hp > 50 ? RGB(60, 200, 100) : hp > 25 ? RGB(250, 190, 40) : RGB(235, 60, 60));
        text(dc, x, 32, bw, 20, mName(i) + (F.dead[i] ? L"  K.O." : L"  " + I(hp)), 13, darker(c, 0.75), true,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    for (int pass = 0; pass < 2; pass++) {   // 내 캐릭터를 맨 위에
        for (int i = 0; i < F.n; i++) {
            if (F.dead[i] || (i == F.me) != (pass == 1)) continue;
            Fighter d = F.f[i];
            if (i != F.me) { d.x = F.dx[i]; d.y = F.dy[i]; }
            drawFighter(dc, d, mCol(i), mHat(i), mName(i));
        }
    }
    if (F.fxT > 0) {
        int r = 10 + (10 - F.fxT) * 3;
        ring(dc, (int)F.fxX, (int)F.fxY, r, RGB(255, 210, 40), 5);
        circle(dc, (int)F.fxX, (int)F.fxY, 8, RGB(255, 250, 200), RGB(255, 180, 0), 2);
    }
    if (F.countdown > 0) text(dc, 0, 140, CW, 100, I(F.countdown / 60 + 1), 80, COL_TEXT, true);
    else if (F.goT > 0) text(dc, 0, 140, CW, 100, L"FIGHT!", 70, COL_OP, true);
    if (F.winner != -2) {
        bool w = F.winner == F.me;
        resultBox(dc, 150, 120, 340, 90, F.winner < 0 ? L"무승부" : w ? L"승리! K.O." : mName(F.winner) + L" 승리", w ? COL_WINC : COL_OP, w ? 40 : 28);
        drawAgain(dc, 230, 230, 180, 50);
    } else if (F.me >= 0 && F.dead[F.me]) {
        text(dc, 0, 70, CW, 30, L"K.O. — 남은 사람들의 대결을 구경하는 중", 17, COL_OP, true);
    } else if (GetFocus() == hInput) {
        text(dc, 0, 60, CW, 24, L"⌨ 채팅창에 입력 중 — 조작하려면 게임 화면을 클릭하세요 (Esc)", 15, COL_OP, true);
    }
    text(dc, 0, (int)GROUND + 20, CW, 24, L"A/D 이동   W 점프   S 방어   J 펀치   K 발차기   (방향키 / Z X도 가능)", 15,
         RGB(255, 255, 255), true);
    text(dc, 0, (int)GROUND + 46, CW, 22, L"게임 화면을 클릭해야 키보드가 게임에 들어갑니다 · Enter = 채팅", 13, RGB(235, 245, 230));
}


// ═════════════════════════ 레이싱 (2~8명) ═════════════════════════
// 서킷 3바퀴. 계산은 위에서 본 평면 지도 위에서 하고, 화면은 운전석에서 보는 1인칭으로 그린다 (미니맵은 오른쪽 위).
// 각자 자기 차만 계산해서 위치를 보내고, 다른 차는 받은 위치로 부드럽게 따라 그린다.
struct V2 { float x, y; };
const float RW = 2400, RH = 1500, R_HALF = 64;   // 맵 크기, 도로 반폭
const int R_LAPS = 3;
struct RCar {
    float x = 0, y = 0, ang = 0, spd = 0;
    float tx = 0, ty = 0, tang = 0;   // 네트워크로 받은 목표 위치 (다른 사람 차)
    int prog = 0, finMs = -1;
    bool nitro = false, seen = false;
};
static struct RaceState {
    bool on = false;
    int n = 0, me = -1, track = 0;
    std::vector<V2> pts;   // 도로 중심선 (닫힌 곡선)
    RCar c[8];
    int idx = 0, lap = 0, nitroLeft = 3;
    float nitroT = 0, boostT = 0, offT = 0;
    ULONGLONG t0 = 0, firstFinAt = 0;
    bool over = false, pNitro = false;
    int winner = -2;
    double sendAcc = 0;
} RC;

static void raceBuild(int t) {
    static const V2 A[] = {{400, 300},  {1000, 220}, {1600, 300},  {2050, 420}, {2150, 800},  {1900, 1150}, {1400, 1050},
                           {1100, 800}, {800, 1000}, {900, 1300}, {500, 1350}, {250, 1000}, {300, 600}};
    static const V2 B[] = {{300, 400},   {700, 250},   {1100, 450},  {1500, 250},  {2000, 350}, {2150, 750}, {1800, 950},
                           {2100, 1250}, {1500, 1350}, {1100, 1100}, {700, 1350}, {300, 1150}, {450, 800}};
    const V2* cp = t ? B : A;
    const int nc = 13, SUB = 18;
    RC.pts.clear();
    for (int i = 0; i < nc; i++) {   // Catmull-Rom 곡선으로 부드럽게
        V2 p0 = cp[(i + nc - 1) % nc], p1 = cp[i], p2 = cp[(i + 1) % nc], p3 = cp[(i + 2) % nc];
        for (int s = 0; s < SUB; s++) {
            float u = s / (float)SUB, u2 = u * u, u3 = u2 * u;
            V2 q;
            q.x = 0.5f * (2 * p1.x + (-p0.x + p2.x) * u + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * u2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * u3);
            q.y = 0.5f * (2 * p1.y + (-p0.y + p2.y) * u + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * u2 + (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * u3);
            RC.pts.push_back(q);
        }
    }
}
// 1인칭 화면용 바닥 지도: 맵을 4px 칸으로 나눠 도로 · 연석 · 잔디 등을 미리 칠해 둔다 (경기 시작 때 한 번)
const int RT_S = 4, RT_W = (int)RW / RT_S, RT_H = (int)RH / RT_S;
static std::vector<unsigned char> g_rtex;   // 0 잔디, 1 도로, 2 흰 연석, 3 빨간 연석, 4 가운데 점선, 5 출발선, 6 부스트 발판
static void rtDisc(float x, float y, float r, unsigned char v) {
    int x0 = std::max(0, (int)((x - r) / RT_S)), x1 = std::min(RT_W - 1, (int)((x + r) / RT_S));
    int y0 = std::max(0, (int)((y - r) / RT_S)), y1 = std::min(RT_H - 1, (int)((y + r) / RT_S));
    for (int yy = y0; yy <= y1; yy++)
        for (int xx = x0; xx <= x1; xx++) {
            float dx = (xx + 0.5f) * RT_S - x, dy = (yy + 0.5f) * RT_S - y;
            if (dx * dx + dy * dy <= r * r) g_rtex[yy * RT_W + xx] = v;
        }
}
static void raceBake() {
    g_rtex.assign(RT_W * RT_H, 0);
    int N = (int)RC.pts.size();
    for (int pass = 0; pass < 3; pass++)   // 연석 → 도로 → 가운데 점선 순서로 덮어 칠함
        for (int i = 0; i < N; i++) {
            V2 a = RC.pts[i], b = RC.pts[(i + 1) % N];
            for (int k = 0; k < 4; k++) {
                float x = a.x + (b.x - a.x) * k / 4, y = a.y + (b.y - a.y) * k / 4;
                if (pass == 0) rtDisc(x, y, R_HALF + 10, (i / 2) % 2 ? 3 : 2);
                else if (pass == 1) rtDisc(x, y, R_HALF, 1);
                else if (i % 3 == 0) rtDisc(x, y, 3, 4);
            }
        }
    V2 a = RC.pts[0], b = RC.pts[1];
    float ang = std::atan2(b.y - a.y, b.x - a.x), nx = -std::sin(ang), ny = std::cos(ang);
    for (float t = -R_HALF; t <= R_HALF; t += 2) rtDisc(a.x + nx * t, a.y + ny * t, 6, 5);
    for (int p : {N / 4, N / 2, N * 3 / 4}) rtDisc(RC.pts[p].x, RC.pts[p].y, 24, 6);
}
static float raceDirAt(int k) {
    int N = (int)RC.pts.size();
    V2 a = RC.pts[((k % N) + N) % N], b = RC.pts[((k + 1) % N + N) % N];
    return std::atan2(b.y - a.y, b.x - a.x);
}
static void raceStart() {
    RC = RaceState();
    RC.on = true;
    RC.n = std::min(8, (int)M.m.size());
    RC.me = mMe();
    RC.track = (int)((M.seed >> 3) % 2);
    raceBuild(RC.track);
    raceBake();
    for (int i = 0; i < RC.n; i++) {   // 출발선 뒤 2열 정렬
        int k = 12 - (i / 2) * 3;
        float a = raceDirAt(k), side = (i % 2) ? 30.f : -30.f;
        RCar& c = RC.c[i];
        c.x = c.tx = RC.pts[k].x - std::sin(a) * side;
        c.y = c.ty = RC.pts[k].y + std::cos(a) * side;
        c.ang = c.tang = a;
        c.seen = true;
        if (i == RC.me) RC.idx = k;
    }
    RC.t0 = GetTickCount64() + 3500;
    gmsg(std::wstring(L"레이싱 시작! ") + (RC.track ? L"산길 코스" : L"호수 코스") + L" · " + I(R_LAPS) + L"바퀴. 게임 화면을 클릭하고 준비하세요.");
    SetFocus(g_hwnd);
}
static std::wstring raceTime(int ms) {
    wchar_t b[32];
    swprintf(b, 32, L"%d:%02d.%d", ms / 60000, (ms / 1000) % 60, (ms / 100) % 10);
    return b;
}
static void raceCheckEnd() {
    if (!RC.on || RC.over) return;
    int best = -1;
    bool all = true;
    for (int i = 0; i < RC.n; i++) {
        if (!M.here[i]) continue;
        if (RC.c[i].finMs < 0) all = false;
        else if (best < 0 || RC.c[i].finMs < RC.c[best].finMs) best = i;
    }
    if (best < 0) return;
    if (!all && GetTickCount64() - RC.firstFinAt < 20000) return;
    RC.over = true;
    RC.winner = best;
    reportResult(best);
}
static void raceFin(int i, int ms) {
    if (RC.c[i].finMs >= 0) return;
    RC.c[i].finMs = ms;
    if (!RC.firstFinAt) RC.firstFinAt = GetTickCount64();
    gmsg(L"레이싱: " + mName(i) + L" 완주! " + raceTime(ms));
    raceCheckEnd();
}
static void raceTick(double dtd) {
    if (!RC.on) return;
    float dt = (float)std::min(dtd, 0.05);
    int N = (int)RC.pts.size();
    ULONGLONG now = GetTickCount64();
    bool go = now >= RC.t0;
    if (RC.me >= 0 && M.here[RC.me]) {
        RCar& c = RC.c[RC.me];
        bool in = go && c.finMs < 0 && !RC.over && gameInput();
        bool up = in && (keyDown('W') || keyDown(VK_UP)), down = in && (keyDown('S') || keyDown(VK_DOWN));
        bool left = in && (keyDown('A') || keyDown(VK_LEFT)), right = in && (keyDown('D') || keyDown(VK_RIGHT));
        bool nit = in && (keyDown(VK_SHIFT) || keyDown(VK_SPACE));
        // 중심선에서 가장 가까운 점 (지금 위치 근처만 찾아서 지름길을 막음)
        int best = RC.idx;
        float bd = 1e18f;
        for (int k = -8; k <= 14; k++) {
            int j = ((RC.idx + k) % N + N) % N;
            float ddx = RC.pts[j].x - c.x, ddy = RC.pts[j].y - c.y, d = ddx * ddx + ddy * ddy;
            if (d < bd) { bd = d; best = j; }
        }
        if (best != RC.idx) {
            if (RC.idx > N * 3 / 4 && best < N / 4) RC.lap++;
            else if (RC.idx < N / 4 && best > N * 3 / 4) RC.lap--;
            RC.idx = best;
        }
        bool onTrack = bd < (R_HALF + 8) * (R_HALF + 8);
        RC.offT = bd > (R_HALF + 150) * (R_HALF + 150) ? RC.offT + dt : 0;
        if ((RC.offT > 2.5f || (in && keyDown('R'))) && c.finMs < 0) {   // 코스를 많이 벗어나면 도로로 되돌림
            c.x = RC.pts[RC.idx].x;
            c.y = RC.pts[RC.idx].y;
            c.ang = raceDirAt(RC.idx);
            c.spd = 0;
            RC.offT = 0;
        }
        if (nit && !RC.pNitro && RC.nitroLeft > 0 && RC.nitroT <= 0) { RC.nitroLeft--; RC.nitroT = 1.6f; }
        RC.pNitro = nit;
        if (RC.nitroT > 0) RC.nitroT -= dt;
        if (RC.boostT > 0) RC.boostT -= dt;
        if (onTrack && go)
            for (int p : {N / 4, N / 2, N * 3 / 4})
                if (std::abs(RC.idx - p) <= 1) RC.boostT = 1.0f;
        bool fast = RC.nitroT > 0 || RC.boostT > 0;
        float maxS = (onTrack ? 520.f : 210.f) + (RC.nitroT > 0 ? 260.f : 0.f) + (RC.boostT > 0 ? 200.f : 0.f);
        if (go && c.finMs < 0) {
            if (up || fast) c.spd += (fast ? 900.f : 420.f) * dt;
            else if (down) c.spd -= 620.f * dt;
            else c.spd -= c.spd * 0.9f * dt;
        } else {
            c.spd -= c.spd * 2.0f * dt;
        }
        if (c.spd > maxS) c.spd -= (c.spd - maxS) * std::min(1.f, 5 * dt);
        if (c.spd < -130) c.spd = -130;
        float turn = (right ? 1.f : 0.f) - (left ? 1.f : 0.f);
        c.ang += turn * 2.6f * dt * std::max(-1.f, std::min(1.f, c.spd / 200.f));
        c.x = std::max(20.f, std::min(RW - 20, c.x + std::cos(c.ang) * c.spd * dt));
        c.y = std::max(20.f, std::min(RH - 20, c.y + std::sin(c.ang) * c.spd * dt));
        c.prog = RC.lap * N + RC.idx;
        c.nitro = fast;
        if (c.finMs < 0 && RC.lap >= R_LAPS) {
            int ms = (int)(now - RC.t0);
            sendMsg("R_FIN", std::to_string(ms));
            raceFin(RC.me, ms);
        }
        RC.sendAcc += dtd;
        if (RC.sendAcc >= stateEvery(RC.n)) {
            RC.sendAcc = 0;
            char buf[96];
            snprintf(buf, sizeof(buf), "%d|%d|%d|%d|%d|%d", (int)c.x, (int)c.y, (int)(c.ang * 100), (int)c.spd, c.prog, c.nitro ? 1 : 0);
            sendMsg("R_S", buf);
        }
    }
    for (int i = 0; i < RC.n; i++) {   // 다른 차: 받은 위치를 향해 부드럽게
        if (i == RC.me) continue;
        RCar& c = RC.c[i];
        c.tx += std::cos(c.tang) * c.spd * dt;
        c.ty += std::sin(c.tang) * c.spd * dt;
        float k = std::min(1.f, dt * 12);
        c.x += (c.tx - c.x) * k;
        c.y += (c.ty - c.y) * k;
        float da = std::remainder(c.tang - c.ang, 6.2831853f);
        c.ang += da * k;
    }
    if (RC.firstFinAt) raceCheckEnd();
}
static void raceMsg(int idx, const std::string& type, const std::vector<std::string>& f) {
    if (!RC.on || idx < 0 || idx >= RC.n || idx == RC.me) return;
    RCar& c = RC.c[idx];
    if (type == "R_S" && f.size() >= 6) {
        c.tx = (float)fI(f, 0);
        c.ty = (float)fI(f, 1);
        c.tang = fI(f, 2) / 100.f;
        c.spd = (float)std::max(-200, std::min(1200, fI(f, 3)));
        c.prog = fI(f, 4);
        c.nitro = fI(f, 5) != 0;
    } else if (type == "R_FIN") {
        raceFin(idx, std::max(1000, fI(f, 0)));
    }
}
static void raceLeft(int) { if (RC.firstFinAt) raceCheckEnd(); }
// 순위: 완주한 사람(시간순) → 달리는 중인 사람(많이 간 순)
static std::vector<int> raceRank() {
    std::vector<int> v;
    for (int i = 0; i < RC.n; i++)
        if (M.here[i]) v.push_back(i);
    std::sort(v.begin(), v.end(), [](int a, int b) {
        const RCar &x = RC.c[a], &y = RC.c[b];
        if ((x.finMs >= 0) != (y.finMs >= 0)) return x.finMs >= 0;
        if (x.finMs >= 0) return x.finMs < y.finMs;
        return x.prog > y.prog;
    });
    return v;
}
static void raceCar(HDC dc, float sx, float sy, float ang, COLORREF col, bool nitro, const std::wstring& name) {
    float c = std::cos(ang), s = std::sin(ang);
    auto P = [&](float lx, float ly) { return POINT{(LONG)std::lround(sx + lx * c - ly * s), (LONG)std::lround(sy + lx * s + ly * c)}; };
    if (nitro) {
        POINT fl[3] = {P(-19, -6), P(-40, 0), P(-19, 6)};
        poly(dc, fl, 3, RGB(255, 205, 70), RGB(255, 120, 40), 2);
    }
    POINT w1 = P(-11, -12), w2 = P(-11, 12), w3 = P(12, -12), w4 = P(12, 12);
    line(dc, w1.x, w1.y, w2.x, w2.y, RGB(40, 44, 56), 7);
    line(dc, w3.x, w3.y, w4.x, w4.y, RGB(40, 44, 56), 7);
    POINT body[4] = {P(-19, -10), P(19, -10), P(19, 10), P(-19, 10)};
    poly(dc, body, 4, col, darker(col, 0.6), 2);
    POINT win[4] = {P(3, -7), P(11, -7), P(11, 7), P(3, 7)};
    poly(dc, win, 4, RGB(214, 238, 252), darker(col, 0.6), 1);
    if (!name.empty()) {
        text(dc, (int)sx - 59, (int)sy - 37, 120, 16, name, 12, RGB(30, 36, 50), true);
        text(dc, (int)sx - 60, (int)sy - 38, 120, 16, name, 12, RGB(255, 255, 255), true);
    }
}
// 뒤에서 본 다른 차 (sx = 화면 가로 중심, gy = 바퀴가 닿는 높이, s = 맵 1칸이 화면에서 차지하는 픽셀)
static void raceCarRear(HDC dc, int sx, int gy, float s, COLORREF col, bool nitro, const std::wstring& name) {
    int w = std::max(4, (int)(42 * s)), h = std::max(3, (int)(26 * s));
    COLORREF dk = darker(col, 0.6);
    int ww = std::max(1, w / 5), wh = std::max(1, h / 3);
    fastRect(dc, sx - w / 2 - ww / 3, gy - wh, ww, wh, RGB(34, 38, 50));
    fastRect(dc, sx + w / 2 - ww + ww / 3, gy - wh, ww, wh, RGB(34, 38, 50));
    roundRect(dc, sx - w / 2, gy - h, w, h - wh / 2, col, dk, std::max(2, h / 3), std::max(1, h / 14));
    roundRect(dc, sx - w * 3 / 10, gy - h + h / 8, w * 6 / 10, h / 3, RGB(214, 238, 252), dk, std::max(2, h / 6), std::max(1, h / 18));
    fastRect(dc, sx - w * 4 / 10, gy - h / 2, std::max(1, w / 7), std::max(1, h / 8), RGB(255, 90, 80));
    fastRect(dc, sx + w * 4 / 10 - std::max(1, w / 7), gy - h / 2, std::max(1, w / 7), std::max(1, h / 8), RGB(255, 90, 80));
    if (nitro) circle(dc, sx, gy - wh / 2, std::max(2, h / 4), RGB(255, 205, 70), RGB(255, 120, 40), std::max(1, h / 12));
    if (w > 26 && !name.empty()) {
        int fs = std::max(11, std::min(18, w / 6));
        text(dc, sx - 79, gy - h - fs - 7, 160, fs + 6, name, fs, RGB(30, 36, 50), true);
        text(dc, sx - 80, gy - h - fs - 8, 160, fs + 6, name, fs, RGB(255, 255, 255), true);
    }
}
// 1인칭 화면: 운전석에서 보듯 바닥을 원근법으로 그린다 (작은 픽셀 버퍼에 그려서 확대)
static void raceView3D(HDC dc, int view) {
    const int W = 320, H = 240, HOR = 96;
    const float camH = 30.f, foc = 190.f;
    static std::vector<unsigned> px(W * H);
    static const int SKY[THEME_COUNT][2][3] = {{{112, 166, 236}, {206, 230, 250}}, {{16, 22, 56}, {64, 84, 136}},
                                               {{250, 196, 214}, {255, 238, 242}}, {{140, 212, 226}, {226, 248, 240}}};
    static const int GRASS[THEME_COUNT][2][3] = {{{126, 190, 108}, {112, 176, 96}}, {{52, 84, 96}, {44, 72, 86}},
                                                 {{246, 200, 214}, {236, 184, 202}}, {{150, 214, 190}, {134, 200, 176}}};
    const RCar& mc = RC.c[view];
    float ca = std::cos(mc.ang), sa = std::sin(mc.ang);
    float camx = mc.x - ca * 10, camy = mc.y - sa * 10;
    const int* fogc = SKY[g_theme][1];
    for (int y = 0; y < HOR; y++) {
        int r = SKY[g_theme][0][0] + (SKY[g_theme][1][0] - SKY[g_theme][0][0]) * y / HOR;
        int g = SKY[g_theme][0][1] + (SKY[g_theme][1][1] - SKY[g_theme][0][1]) * y / HOR;
        int b = SKY[g_theme][0][2] + (SKY[g_theme][1][2] - SKY[g_theme][0][2]) * y / HOR;
        unsigned c = (unsigned)((r << 16) | (g << 8) | b);
        for (int x = 0; x < W; x++) px[y * W + x] = c;
    }
    for (int y = HOR; y < H; y++) {
        float d = camH * foc / (y - HOR + 0.5f);          // 이 줄에 보이는 바닥까지의 거리
        float sx = -sa * d / foc, sy = ca * d / foc;       // 화면 한 픽셀 옆 = 맵에서 이만큼
        float wx = camx + ca * d - sx * (W / 2), wy = camy + sa * d - sy * (W / 2);
        int fog = std::min(215, (int)(d * 256 / 2000));    // 멀수록 하늘색에 섞임
        unsigned* row = &px[y * W];
        for (int x = 0; x < W; x++, wx += sx, wy += sy) {
            int r, g, b;
            int ix = (int)wx, iy = (int)wy;
            unsigned char code = (wx < 0 || wy < 0 || wx >= RW || wy >= RH) ? 255 : g_rtex[(iy / RT_S) * RT_W + ix / RT_S];
            switch (code) {
                case 1: { int v = ((ix / 40 + iy / 40) & 1) ? 96 : 90; r = v; g = v + 6; b = v + 22; break; }
                case 2: r = 242; g = 242; b = 246; break;
                case 3: r = 228; g = 72; b = 72; break;
                case 4: r = 226; g = 230; b = 238; break;
                case 5: if ((ix / 10 + iy / 10) & 1) { r = g = b = 245; } else { r = g = b = 34; } break;
                case 6: r = 255; g = 214; b = 80; break;
                default: {
                    const int* gc = GRASS[g_theme][(((ix + 4000) / 110 + (iy + 4000) / 110) & 1)];
                    r = gc[0]; g = gc[1]; b = gc[2];
                    if (code == 255) { r = r * 3 / 4; g = g * 3 / 4; b = b * 3 / 4; }   // 맵 바깥
                }
            }
            r += ((fogc[0] - r) * fog) >> 8;
            g += ((fogc[1] - g) * fog) >> 8;
            b += ((fogc[2] - b) * fog) >> 8;
            row[x] = (unsigned)((r << 16) | (g << 8) | b);
        }
    }
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = W;
    bmi.bmiHeader.biHeight = -H;   // 위에서 아래로
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, 0, 0, CW, CH, 0, 0, W, H, px.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    // 다른 차 (먼 차부터)
    const float k = (float)CW / W;   // 버퍼 → 화면 배율
    std::vector<std::pair<float, int>> cars;
    for (int i = 0; i < RC.n; i++) {
        if (i == view || !M.here[i]) continue;
        float rx = RC.c[i].x - camx, ry = RC.c[i].y - camy, depth = rx * ca + ry * sa;
        if (depth > 14) cars.push_back({depth, i});
    }
    std::sort(cars.begin(), cars.end(), [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first > b.first; });
    for (auto& c : cars) {
        const RCar& o = RC.c[c.second];
        float rx = o.x - camx, ry = o.y - camy, depth = c.first, lat = -rx * sa + ry * ca;
        float s = foc / depth * k;
        raceCarRear(dc, (int)(CW / 2 + lat * s), (int)(HOR * k + camH * s), s, mCol(c.second), o.nitro, mName(c.second));
    }
    // 내 차 앞부분 (보닛)
    COLORREF my = mCol(view), dk = darker(my, 0.6);
    POINT hood[4] = {{170, CH}, {236, CH - 54}, {404, CH - 54}, {470, CH}};
    poly(dc, hood, 4, my, dk, 3);
    POINT shine[4] = {{262, CH - 46}, {378, CH - 46}, {392, CH - 30}, {248, CH - 30}};
    poly(dc, shine, 4, mix(my, RGB(255, 255, 255), 0.35), mix(my, RGB(255, 255, 255), 0.35), 1);
    if (mc.nitro) {   // 빨라질 때 속도선
        for (int i = 0; i < 6; i++) {
            int x = 40 + ((i * 97 + (int)(GetTickCount64() / 40) * 31) % 560), y = 230 + (i * 53) % 150;
            line(dc, x, y, x + (x < CW / 2 ? -26 : 26), y + 16, RGB(255, 255, 255), 2);
        }
    }
}
static void raceDraw(HDC dc) {
    int N = (int)RC.pts.size();
    int view = RC.me >= 0 ? RC.me : 0;
    const RCar& mc = RC.c[view];
    raceView3D(dc, view);
    // HUD
    std::vector<int> rank = raceRank();
    int myRank = 1;
    for (int i = 0; i < (int)rank.size(); i++)
        if (rank[i] == view) myRank = i + 1;
    ULONGLONG now = GetTickCount64();
    roundRect(dc, 10, 10, 168, 92, RGB(255, 255, 255), COL_LINE, 16, 2);
    text(dc, 22, 14, 150, 26, L"LAP " + I(std::min(R_LAPS, std::max(0, RC.lap) + 1)) + L" / " + I(R_LAPS), 19, COL_TEXT, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    text(dc, 22, 40, 150, 24, I(myRank) + L"위 / " + I((int)rank.size()) + L"명", 17, mCol(view), true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    int el = mc.finMs >= 0 ? mc.finMs : now > RC.t0 ? (int)(now - RC.t0) : 0;
    text(dc, 22, 64, 80, 20, raceTime(el), 14, COL_SUB, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    text(dc, 96, 64, 76, 20, I((int)std::fabs(mc.spd) / 3) + L" km/h", 14, COL_SUB, true, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    text(dc, 22, 82, 60, 16, L"부스터", 11, COL_SUB, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    for (int i = 0; i < 3; i++) circle(dc, 76 + i * 16, 90, 5, i < RC.nitroLeft ? COL_ACC : RGB(226, 233, 242), i < RC.nitroLeft ? darker(COL_ACC) : COL_LINE, 1);
    // 미니맵
    {
        const float ms = 0.042f;   // 작은 미니맵 (오른쪽 위)
        int mx = CW - 112, my = 10;
        roundRect(dc, mx, my, 102, 68, RGB(255, 255, 255), COL_LINE, 14, 2);
        std::vector<POINT> mp(N + 1);
        for (int i = 0; i <= N; i++) mp[i] = {(LONG)(mx + 1 + RC.pts[i % N].x * ms), (LONG)(my + 3 + RC.pts[i % N].y * ms)};
        HPEN p = CreatePen(PS_SOLID, 3, RGB(170, 178, 196));
        HGDIOBJ op = SelectObject(dc, p);
        Polyline(dc, mp.data(), N + 1);
        SelectObject(dc, op);
        DeleteObject(p);
        for (int i = 0; i < RC.n; i++)
            if (M.here[i]) circle(dc, (int)(mx + 1 + RC.c[i].x * ms), (int)(my + 3 + RC.c[i].y * ms), i == view ? 4 : 3, mCol(i), i == view ? COL_TEXT : RGB(255, 255, 255), 1);
    }
    if (now < RC.t0) {
        int left = (int)((RC.t0 - now) / 1000) + 1;
        text(dc, 2, 142, CW, 100, left > 3 ? L"준비" : I(left), 80, RGB(30, 36, 50), true);
        text(dc, 0, 140, CW, 100, left > 3 ? L"준비" : I(left), 80, RGB(255, 255, 255), true);
        roundRect(dc, 90, 400, 460, 60, RGB(255, 255, 255), COL_LINE, 18, 2);
        text(dc, 90, 404, 460, 26, L"W/↑ 가속   S/↓ 브레이크   A/D 또는 ←/→ 방향   Shift 부스터(3번)", 14, COL_TEXT, true);
        text(dc, 90, 430, 460, 24, L"노란 발판을 밟으면 빨라져요 · 잔디에서는 느려져요 · R = 도로로 복귀", 13, COL_SUB);
    } else if (now < RC.t0 + 900) {
        text(dc, 0, 140, CW, 100, L"출발!", 70, RGB(255, 255, 255), true);
    }
    if (RC.offT > 0.5f && mc.finMs < 0) text(dc, 0, 110, CW, 30, L"코스 이탈! 곧 도로로 돌아갑니다", 18, RGB(255, 255, 255), true);
    if (mc.finMs >= 0 || RC.over) {   // 결과표
        int h = 70 + (int)rank.size() * 26 + (RC.over ? 60 : 24);
        roundRect(dc, 170, 110, 300, h, RGB(255, 255, 255), COL_ACC, 22, 3);
        text(dc, 170, 118, 300, 32, RC.over ? L"🏁 경기 종료" : L"🏁 완주! 다른 차를 기다리는 중", RC.over ? 22 : 16, COL_TEXT, true);
        int y = 156;
        for (int i = 0; i < (int)rank.size(); i++) {
            const RCar& c = RC.c[rank[i]];
            text(dc, 190, y, 40, 24, I(i + 1) + L"위", 15, COL_SUB, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            text(dc, 232, y, 130, 24, mName(rank[i]), 15, darker(mCol(rank[i]), 0.8), true, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            text(dc, 362, y, 90, 24, c.finMs >= 0 ? raceTime(c.finMs) : L"달리는 중", 14, COL_TEXT, false, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            y += 26;
        }
        if (RC.over) drawAgain(dc, 240, y + 12, 160, 44);
    }
    if (GetFocus() == hInput && !RC.over) text(dc, 0, 106, CW, 24, L"⌨ 채팅창에 입력 중 — 운전하려면 게임 화면을 클릭하세요 (Esc)", 14, RGB(255, 255, 255), true);
}

// ═════════════════════════ 페인트볼 (1인칭 슈팅, 2~8명) ═════════════════════════
// 레이캐스팅으로 그리는 3D 미로에서 서로에게 페인트를 맞히는 게임. 먼저 5번 아웃시키면 승리.
// 맞았는지는 쏜 쪽이 판정해 알리고, 체력은 맞은 쪽이 관리한다.
const int PB_W = 20, PB_H = 20, PB_GOAL = 5, PB_COLS = 160;
static const char* PB_MAP[PB_H] = {
    "11111111111111111111", "1........2.........1", "1.33.....2....44...1", "1.3...........4....1", "1.......22.........1",
    "1..........1...33..1", "1222...4...1....3..1", "1......4...........1", "1......4444...2....1", "1.............2....1",
    "1....3........2222.1", "1....3...1.........1", "1.3333...1....4....1", "1........1....4....1", "1...2........44..2.1",
    "1...2....33......2.1", "1...22...3.....222.1", "1..................1", "1.........4........1", "11111111111111111111"};
static const V2 PB_SPAWN[8] = {{1.5f, 1.5f}, {18.5f, 18.5f}, {18.5f, 1.5f}, {1.5f, 18.5f}, {10.5f, 9.5f}, {5.5f, 4.5f}, {15.5f, 13.5f}, {9.5f, 17.5f}};
struct PPl {
    float x = 1.5f, y = 1.5f, a = 0;
    float tx = 1.5f, ty = 1.5f, ta = 0;
    int hp = 100, kills = 0;
    bool dead = false, seen = false;
};
static struct PbState {
    bool on = false, over = false, qShoot = false;
    int n = 0, me = -1, winner = -2;
    PPl p[8];
    ULONGLONG t0 = 0, respawnAt = 0, lastShot = 0, hurtT = 0, hitT = 0, safeUntil = 0;
    double sendAcc = 0;
    std::vector<std::wstring> feed;
    ULONGLONG feedT = 0;
    float z[PB_COLS];
} PB;

// 마우스 조준: 게임 화면을 클릭하면 커서를 숨기고 화면 가운데에 붙잡아 둔 채, 움직인 만큼 시점을 돌린다. Esc로 풀림
static bool g_mouseLook = false;
static POINT pbCenter() {
    POINT p{PX(CX + CW / 2), PY(CY + CH / 2)};
    ClientToScreen(g_hwnd, &p);
    return p;
}
static void pbMouse(bool on) {
    if (on == g_mouseLook) return;
    g_mouseLook = on;
    ShowCursor(on ? FALSE : TRUE);
    if (on) {
        POINT a{PX(CX), PY(CY)}, b{PX(CX + CW), PY(CY + CH)}, c = pbCenter();
        ClientToScreen(g_hwnd, &a);
        ClientToScreen(g_hwnd, &b);
        RECT r{a.x, a.y, b.x, b.y};
        ClipCursor(&r);
        SetCursorPos(c.x, c.y);
    } else {
        ClipCursor(nullptr);
    }
}
static bool pbWall(int x, int y) { return x < 0 || y < 0 || x >= PB_W || y >= PB_H || PB_MAP[y][x] != '.'; }
static bool pbFree(float x, float y) {
    const float r = 0.22f;
    return !pbWall((int)(x - r), (int)(y - r)) && !pbWall((int)(x + r), (int)(y - r)) && !pbWall((int)(x - r), (int)(y + r)) &&
           !pbWall((int)(x + r), (int)(y + r));
}
// 광선을 쏴서 벽까지의 거리(카메라 평면 기준)를 구한다 (DDA)
static float pbCast(float px, float py, float dx, float dy, int* side, char* cell) {
    int mx = (int)px, my = (int)py;
    float ddx = dx == 0 ? 1e30f : std::fabs(1 / dx), ddy = dy == 0 ? 1e30f : std::fabs(1 / dy);
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    float sdx = dx < 0 ? (px - mx) * ddx : (mx + 1 - px) * ddx, sdy = dy < 0 ? (py - my) * ddy : (my + 1 - py) * ddy;
    int s = 0;
    for (int i = 0; i < 64; i++) {
        if (sdx < sdy) { sdx += ddx; mx += sx; s = 0; }
        else { sdy += ddy; my += sy; s = 1; }
        if (pbWall(mx, my)) break;
    }
    if (side) *side = s;
    if (cell) *cell = (mx >= 0 && my >= 0 && mx < PB_W && my < PB_H) ? PB_MAP[my][mx] : '1';
    return std::max(0.05f, s == 0 ? sdx - ddx : sdy - ddy);
}
static void pbSpawn(int i, int slot) {
    PPl& p = PB.p[i];
    V2 s = PB_SPAWN[((slot % 8) + 8) % 8];
    p.x = p.tx = s.x;
    p.y = p.ty = s.y;
    p.a = p.ta = std::atan2(PB_H / 2.f - s.y, PB_W / 2.f - s.x);
    p.hp = 100;
    p.dead = false;
}
static void pbStart() {
    PB = PbState();
    PB.on = true;
    PB.n = std::min(8, (int)M.m.size());
    PB.me = mMe();
    for (int i = 0; i < PB.n; i++) { pbSpawn(i, i + (int)(M.seed % 8)); PB.p[i].seen = true; }
    for (float& z : PB.z) z = 1e9f;
    PB.t0 = GetTickCount64() + 3500;
    gmsg(L"페인트볼 시작! 먼저 " + I(PB_GOAL) + L"번 맞혀 아웃시키면 승리. 게임 화면을 클릭하고 준비하세요.");
    SetFocus(g_hwnd);
}
static void pbKill(int killer, int victim) {
    if (PB.over) return;
    if (killer >= 0 && killer < PB.n && killer != victim) PB.p[killer].kills++;
    PB.feed.push_back(mName(killer) + L"  ▶  " + mName(victim) + L" 아웃!");
    if (PB.feed.size() > 3) PB.feed.erase(PB.feed.begin());
    PB.feedT = GetTickCount64();
    if (killer >= 0 && killer < PB.n && PB.p[killer].kills >= PB_GOAL) {
        PB.over = true;
        PB.winner = killer;
        gmsg(L"페인트볼: " + mName(killer) + (killer == PB.me ? L" 승리! 🎉" : L" 님 승리!"));
        reportResult(killer);
    }
}
static void pbShoot() {
    ULONGLONG now = GetTickCount64();
    PPl& m = PB.p[PB.me];
    if (now - PB.lastShot < 320) return;
    PB.lastShot = now;
    float dx = std::cos(m.a), dy = std::sin(m.a);
    float wall = pbCast(m.x, m.y, dx, dy, nullptr, nullptr);
    int hit = -1;
    float hd = 1e9f;
    for (int i = 0; i < PB.n; i++) {
        if (i == PB.me || !M.here[i] || PB.p[i].dead) continue;
        float rx = PB.p[i].x - m.x, ry = PB.p[i].y - m.y;
        float depth = rx * dx + ry * dy, lat = -rx * dy + ry * dx;
        if (depth > 0.15f && depth < wall && std::fabs(lat) < 0.3f && depth < hd) { hd = depth; hit = i; }
    }
    if (hit >= 0) {
        PB.hitT = now;
        sendMsg("P_HIT", M.m[hit] + "|34");
    }
}
static void pbTick(double dtd) {
    if (!PB.on) return;
    float dt = (float)std::min(dtd, 0.05);
    ULONGLONG now = GetTickCount64();
    if (PB.me >= 0 && M.here[PB.me]) {
        PPl& m = PB.p[PB.me];
        if (m.dead && now >= PB.respawnAt && !PB.over) {   // 다른 사람들과 가장 먼 곳에서 다시 시작
            int best = 0;
            float bd = -1;
            for (int s = 0; s < 8; s++) {
                float md = 1e9f;
                for (int i = 0; i < PB.n; i++) {
                    if (i == PB.me || !M.here[i]) continue;
                    float ddx = PB.p[i].x - PB_SPAWN[s].x, ddy = PB.p[i].y - PB_SPAWN[s].y;
                    md = std::min(md, ddx * ddx + ddy * ddy);
                }
                if (md > bd) { bd = md; best = s; }
            }
            pbSpawn(PB.me, best);
            PB.safeUntil = now + 1500;
        }
        bool in = now >= PB.t0 && !PB.over && !m.dead && gameInput();
        if (g_mouseLook) {   // 마우스가 움직인 만큼 돌고, 커서는 다시 가운데로
            POINT c = pbCenter(), p;
            if (GetCursorPos(&p) && (p.x != c.x || p.y != c.y)) {
                if (in) m.a += (p.x - c.x) * 0.0026f;
                SetCursorPos(c.x, c.y);
            }
        }
        if (in) {
            float turn = (keyDown(VK_RIGHT) || keyDown('E') ? 1.f : 0.f) - (keyDown(VK_LEFT) || keyDown('Q') ? 1.f : 0.f);
            m.a += turn * 2.6f * dt;
            float fwd = (keyDown('W') || keyDown(VK_UP) ? 1.f : 0.f) - (keyDown('S') || keyDown(VK_DOWN) ? 1.f : 0.f);
            float str = (keyDown('D') ? 1.f : 0.f) - (keyDown('A') ? 1.f : 0.f);
            float c = std::cos(m.a), s = std::sin(m.a), sp = 3.3f * dt;
            float nx = m.x + (c * fwd - s * str) * sp, ny = m.y + (s * fwd + c * str) * sp;
            if (pbFree(nx, m.y)) m.x = nx;
            if (pbFree(m.x, ny)) m.y = ny;
            if (PB.qShoot || keyDown(VK_SPACE) || keyDown('J') || (g_mouseLook && keyDown(VK_LBUTTON))) pbShoot();
        }
        PB.qShoot = false;
        PB.sendAcc += dtd;
        if (PB.sendAcc >= stateEvery(PB.n)) {
            PB.sendAcc = 0;
            char buf[96];
            snprintf(buf, sizeof(buf), "%d|%d|%d|%d|%d", (int)(m.x * 100), (int)(m.y * 100), (int)(m.a * 100), m.hp, m.dead ? 1 : 0);
            sendMsg("P_S", buf);
        }
    }
    for (int i = 0; i < PB.n; i++) {
        if (i == PB.me) continue;
        PPl& p = PB.p[i];
        float k = std::min(1.f, dt * (g_slowRelay ? 5.f : 12.f));
        p.x += (p.tx - p.x) * k;
        p.y += (p.ty - p.y) * k;
        p.a += std::remainder(p.ta - p.a, 6.2831853f) * k;
    }
}
static void pbMsg(int idx, const std::string& type, const std::vector<std::string>& f) {
    if (!PB.on || idx < 0 || idx >= PB.n || idx == PB.me) return;
    ULONGLONG now = GetTickCount64();
    if (type == "P_S" && f.size() >= 5) {
        PPl& p = PB.p[idx];
        p.tx = std::max(0.5f, std::min(PB_W - 0.5f, fI(f, 0) / 100.f));
        p.ty = std::max(0.5f, std::min(PB_H - 0.5f, fI(f, 1) / 100.f));
        p.ta = fI(f, 2) / 100.f;
        p.hp = fI(f, 3);
        bool dead = fI(f, 4) != 0;
        if (p.dead && !dead) { p.x = p.tx; p.y = p.ty; }   // 다시 태어난 자리로 바로 이동
        p.dead = dead;
    } else if (type == "P_HIT") {
        if (fS(f, 0) != g_pid || PB.me < 0 || PB.over) return;
        PPl& m = PB.p[PB.me];
        if (m.dead || now < PB.safeUntil) return;
        m.hp -= std::max(0, std::min(50, fI(f, 1)));
        PB.hurtT = now;
        if (m.hp <= 0) {
            m.hp = 0;
            m.dead = true;
            PB.respawnAt = now + 2500;
            sendMsg("P_DEAD", M.m[idx]);
            pbKill(idx, PB.me);
        }
    } else if (type == "P_DEAD") {
        PB.p[idx].dead = true;
        pbKill(mIdx(fS(f, 0)), idx);
    }
}
static void pbLeft(int) {}
// 다른 플레이어 그리기: sx = 화면 가로 중심, h = 벽 높이(거리에 반비례)
static void pbDrawPlayer(HDC dc, int sx, float h, COLORREF col, int hat, const std::wstring& name) {
    int mid = CH / 2, foot = mid + (int)(h * 0.5f);
    int bw = std::max(2, (int)(h * 0.30f)), top = mid - (int)(h * 0.10f), hr = std::max(2, (int)(h * 0.12f));
    COLORREF dk = darker(col, 0.65);
    int lw = std::max(1, bw / 3);
    fastRect(dc, sx - bw / 2 + lw / 4, mid + (int)(h * 0.26f), lw, foot - (mid + (int)(h * 0.26f)), dk);
    fastRect(dc, sx + bw / 2 - lw - lw / 4, mid + (int)(h * 0.26f), lw, foot - (mid + (int)(h * 0.26f)), dk);
    roundRect(dc, sx - bw / 2, top, bw, (int)(h * 0.40f), col, dk, std::max(2, bw / 3), std::max(1, (int)(h / 90)));
    fastRect(dc, sx + bw / 4, top + (int)(h * 0.10f), std::max(1, bw / 2), std::max(1, (int)(h * 0.06f)), RGB(60, 66, 84));   // 페인트 총
    drawHead(dc, sx, top - hr + 2, hr, col, hat);
    if (h > 46) {
        int fs = std::max(11, std::min(18, (int)(h / 14)));
        text(dc, sx - 80, top - hr * 3 - fs - 4, 160, fs + 6, name, fs, RGB(255, 255, 255), true);
    }
}
static void pbDraw(HDC dc) {
    int view = PB.me >= 0 ? PB.me : 0;
    const PPl& m = PB.p[view];
    ULONGLONG now = GetTickCount64();
    float dx = std::cos(m.a), dy = std::sin(m.a), plx = -dy * 0.66f, ply = dx * 0.66f;
    int mid = CH / 2;
    // 천장 · 바닥 (멀수록 어둡게)
    for (int i = 0; i < 8; i++) {
        fastRect(dc, 0, i * 30, CW, 30, mix(RGB(150, 190, 235), RGB(214, 232, 250), i / 7.0));
        fastRect(dc, 0, mid + i * 30, CW, 30, mix(RGB(150, 140, 126), RGB(206, 196, 180), i / 7.0));
    }
    // 벽
    static const COLORREF WCOL[4] = {RGB(126, 138, 172), RGB(206, 124, 96), RGB(96, 170, 128), RGB(204, 172, 96)};
    const int cw = CW / PB_COLS;
    for (int col = 0; col < PB_COLS; col++) {
        float cam = 2.f * (col + 0.5f) / PB_COLS - 1;
        int side = 0;
        char cell = '1';
        float d = pbCast(m.x, m.y, dx + plx * cam, dy + ply * cam, &side, &cell);
        PB.z[col] = d;
        int lh = (int)(CH / d), y0 = std::max(0, mid - lh / 2), y1 = std::min(CH, mid + lh / 2);
        COLORREF base = WCOL[(cell >= '1' && cell <= '4') ? cell - '1' : 0];
        double sh = (side ? 0.78 : 1.0) * std::max(0.35, 1.0 - d / 16.0);
        fastRect(dc, col * cw, y0, cw + 1, y1 - y0, darker(base, sh));
        fastRect(dc, col * cw, y0, cw + 1, std::max(1, lh / 40), darker(base, sh * 0.8));   // 벽 위쪽 테두리
    }
    // 다른 플레이어 (먼 사람부터)
    std::vector<std::pair<float, int>> spr;
    for (int i = 0; i < PB.n; i++) {
        if (i == view || !M.here[i] || PB.p[i].dead) continue;
        float rx = PB.p[i].x - m.x, ry = PB.p[i].y - m.y;
        float depth = rx * dx + ry * dy;
        if (depth > 0.2f) spr.push_back({depth, i});
    }
    std::sort(spr.begin(), spr.end(), [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first > b.first; });
    for (auto& s : spr) {
        const PPl& p = PB.p[s.second];
        float rx = p.x - m.x, ry = p.y - m.y, depth = s.first, lat = -rx * dy + ry * dx;
        int sx = (int)(CW / 2 + lat / depth / 0.66f * (CW / 2));
        float h = CH / depth;
        int half = (int)(h * 0.4f) + 90;
        int c0 = std::max(0, (sx - half) / cw), c1 = std::min(PB_COLS - 1, (sx + half) / cw);
        for (int c = c0; c <= c1;) {   // 벽에 가려지지 않은 구간만 잘라서 그림
            if (PB.z[c] <= depth) { c++; continue; }
            int e = c;
            while (e + 1 <= c1 && PB.z[e + 1] > depth) e++;
            int sv = SaveDC(dc);
            IntersectClipRect(dc, c * cw, 0, (e + 1) * cw, CH);
            pbDrawPlayer(dc, sx, h, mCol(s.second), mHat(s.second), mName(s.second));
            RestoreDC(dc, sv);
            c = e + 1;
        }
    }
    // 내 페인트 총
    COLORREF mc = mCol(view);
    if (!m.dead) {
        int kick = now - PB.lastShot < 120 ? 10 : 0;
        POINT g[6] = {{388, CH}, {400, 392 + kick}, {412, 372 + kick}, {440, 372 + kick}, {470, 400 + kick}, {500, CH}};
        poly(dc, g, 6, RGB(72, 78, 98), RGB(40, 44, 58), 3);
        roundRect(dc, 408, 348 + kick, 36, 36, mc, darker(mc, 0.6), 14, 3);
        if (now - PB.lastShot < 90) {
            circle(dc, 426, 340, 26, mix(mc, RGB(255, 255, 255), 0.5), mc, 4);
            circle(dc, CW / 2, mid, 7, mc, mc, 1);
        }
    }
    // 조준점
    line(dc, CW / 2 - 12, mid, CW / 2 - 4, mid, RGB(255, 255, 255), 2);
    line(dc, CW / 2 + 4, mid, CW / 2 + 12, mid, RGB(255, 255, 255), 2);
    line(dc, CW / 2, mid - 12, CW / 2, mid - 4, RGB(255, 255, 255), 2);
    line(dc, CW / 2, mid + 4, CW / 2, mid + 12, RGB(255, 255, 255), 2);
    if (now - PB.hitT < 180) {
        line(dc, CW / 2 - 16, mid - 16, CW / 2 - 7, mid - 7, RGB(255, 80, 80), 3);
        line(dc, CW / 2 + 16, mid - 16, CW / 2 + 7, mid - 7, RGB(255, 80, 80), 3);
        line(dc, CW / 2 - 16, mid + 16, CW / 2 - 7, mid + 7, RGB(255, 80, 80), 3);
        line(dc, CW / 2 + 16, mid + 16, CW / 2 + 7, mid + 7, RGB(255, 80, 80), 3);
    }
    if (now - PB.hurtT < 260) {   // 맞았을 때 화면 테두리
        fastRect(dc, 0, 0, CW, 10, RGB(240, 70, 70));
        fastRect(dc, 0, CH - 10, CW, 10, RGB(240, 70, 70));
        fastRect(dc, 0, 0, 10, CH, RGB(240, 70, 70));
        fastRect(dc, CW - 10, 0, 10, CH, RGB(240, 70, 70));
    }
    // 미니맵 (내 위치만)
    {
        const int cs = 4, ox = 10, oy = 10;
        fastRect(dc, ox - 2, oy - 2, PB_W * cs + 4, PB_H * cs + 4, RGB(255, 255, 255));
        for (int y = 0; y < PB_H; y++)
            for (int x = 0; x < PB_W; x++)
                if (pbWall(x, y)) fastRect(dc, ox + x * cs, oy + y * cs, cs, cs, RGB(150, 160, 186));
        int px = ox + (int)(m.x * cs), py = oy + (int)(m.y * cs);
        line(dc, px, py, px + (int)(dx * 7), py + (int)(dy * 7), darker(mc, 0.6), 2);
        circle(dc, px, py, 3, mc, darker(mc, 0.6), 1);
    }
    // 점수판
    {
        std::vector<int> ord;
        for (int i = 0; i < PB.n; i++)
            if (M.here[i]) ord.push_back(i);
        std::sort(ord.begin(), ord.end(), [](int a, int b) { return PB.p[a].kills > PB.p[b].kills; });
        int h = 30 + (int)ord.size() * 20;
        roundRect(dc, CW - 160, 10, 150, h, RGB(255, 255, 255), COL_LINE, 14, 2);
        text(dc, CW - 150, 13, 130, 20, L"먼저 " + I(PB_GOAL) + L"번 아웃!", 12, COL_SUB, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int y = 34;
        for (int i : ord) {
            circle(dc, CW - 146, y + 9, 5, mCol(i), darker(mCol(i), 0.6), 1);
            text(dc, CW - 136, y, 92, 18, mName(i), 13, COL_TEXT, i == view, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            text(dc, CW - 44, y, 26, 18, I(PB.p[i].kills), 14, COL_TEXT, true, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            y += 20;
        }
    }
    // 체력
    roundRect(dc, 10, CH - 38, 204, 26, RGB(255, 255, 255), RGB(80, 85, 100), 12, 2);
    if (m.hp > 0) fastRect(dc, 13, CH - 35, 198 * std::min(100, m.hp) / 100, 20, m.hp > 50 ? RGB(60, 200, 100) : m.hp > 25 ? RGB(250, 190, 40) : RGB(235, 60, 60));
    text(dc, 10, CH - 38, 204, 26, L"체력 " + I(std::max(0, m.hp)), 14, COL_TEXT, true);
    if (now - PB.feedT < 4000)
        for (int i = 0; i < (int)PB.feed.size(); i++) {
            text(dc, 1, 15 + i * 22, CW, 20, PB.feed[i], 15, RGB(30, 36, 50), true);
            text(dc, 0, 14 + i * 22, CW, 20, PB.feed[i], 15, RGB(255, 255, 255), true);
        }
    if (now < PB.safeUntil && !m.dead) text(dc, 0, CH - 70, CW, 22, L"보호 중 (잠깐 동안 맞지 않아요)", 14, RGB(255, 255, 255), true);
    if (now < PB.t0) {
        int left = (int)((PB.t0 - now) / 1000) + 1;
        text(dc, 2, 122, CW, 100, left > 3 ? L"준비" : I(left), 80, RGB(30, 36, 50), true);
        text(dc, 0, 120, CW, 100, left > 3 ? L"준비" : I(left), 80, RGB(255, 255, 255), true);
        roundRect(dc, 80, 300, 480, 60, RGB(255, 255, 255), COL_LINE, 18, 2);
        text(dc, 80, 304, 480, 26, L"화면 클릭 → 마우스로 조준 · 클릭 발사   W/A/S/D 이동   Esc 마우스 풀기", 14, COL_TEXT, true);
        text(dc, 80, 330, 480, 24, L"키보드만으로도 가능: ←/→ 돌기, Space 발사 · 세 번 맞으면 아웃", 13, COL_SUB);
    }
    if (PB.over) {
        bool w = PB.winner == PB.me;
        resultBox(dc, 150, 130, 340, 90, w ? L"승리! 🎉" : mName(PB.winner) + L" 승리", w ? COL_WINC : COL_OP, w ? 40 : 28);
        drawAgain(dc, 230, 240, 180, 50);
    } else if (m.dead) {
        resultBox(dc, 190, 150, 260, 70, L"아웃! 곧 다시 시작", COL_OP, 22);
    } else if (GetFocus() == hInput) {
        text(dc, 0, 96, CW, 24, L"⌨ 채팅창에 입력 중 — 조작하려면 게임 화면을 클릭하세요 (Esc)", 14, RGB(255, 255, 255), true);
    } else if (!g_mouseLook && now >= PB.t0) {
        text(dc, 1, 97, CW, 24, L"화면을 클릭하면 마우스로 조준합니다 (Esc로 풀기)", 14, RGB(30, 36, 50), true);
        text(dc, 0, 96, CW, 24, L"화면을 클릭하면 마우스로 조준합니다 (Esc로 풀기)", 14, RGB(255, 255, 255), true);
    }
}


// ═════════════════════════ 방 전체가 함께 하는 게임: 끝말잇기 · 가위바위보 ═════════════════════════
// 방장 프로그램이 심판: 참가자 명단, 단어 검사, 제한 시간, 탈락을 결정해서 모두에게 알린다.
static void onConnected();
static void onDisconnected(const std::wstring& why);
static void relayFail(const std::wstring& why);
static void copyToClipboard(const std::wstring& s);
static std::wstring bestIP();
static void updateCopyBtn();
static void showConnUi(bool show);
// 친구에게 알려줄 방 주소: 방 코드 또는 (IP 방이면) 내 IP
static std::wstring g_roomAddr;
static std::wstring roomLabel() { return g_roomAddr; }
static const wchar_t* RPS_N[3] = {L"가위", L"바위", L"보"};
static const wchar_t* RPS_E[3] = {L"✌", L"✊", L"✋"};

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
    return L"방 " + roomLabel() + L" · " + I((int)GP.size()) + L"/" + I(GROUP_MAX) + L"명" + (g_isHost ? L" · 내가 방장" : L"");
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

// ═════════════════════════ 대결: 게임별 연결 ═════════════════════════
static void gameStop() { O.on = false; Y.on = false; F.on = false; RC.on = false; PB.on = false; pbMouse(false); }
static void gameLeft(int idx) {
    switch (M.view) {
        case V_OMOK: omLeft(idx); break;
        case V_YUT: yutLeft(idx); break;
        case V_FIGHT: fightLeft(idx); break;
        case V_RACE: raceLeft(idx); break;
        case V_FPS: pbLeft(idx); break;
    }
}
static void gameStart() {
    M.reported = false;
    gameStop();
    switch (M.view) {
        case V_OMOK: omStart(); break;
        case V_YUT: yutStart(); break;
        case V_FIGHT: fightStart(); break;
        case V_RACE: raceStart(); break;
        case V_FPS: pbStart(); break;
    }
    for (int i = 0; i < (int)M.m.size(); i++)
        if (!M.here[i]) gameLeft(i);
}
static bool isGameMsg(const std::string& t) { return t.size() > 2 && t[1] == '_' && strchr("OYFRPG", t[0]) != nullptr; }
static void gameMsg(int idx, const std::string& type, const std::vector<std::string>& f) {
    switch (type[0]) {
        case 'O': if (M.view == V_OMOK) omMsg(idx, type, f); break;
        case 'Y': if (M.view == V_YUT) yutMsg(idx, type, f); break;
        case 'F': if (M.view == V_FIGHT) fightMsg(idx, type, f); break;
        case 'R': if (M.view == V_RACE) raceMsg(idx, type, f); break;
        case 'P': if (M.view == V_FPS) pbMsg(idx, type, f); break;
    }
}
static bool gameRealtime() { return M.on && (M.view == V_FIGHT || M.view == V_RACE || M.view == V_FPS); }
static void gameTick(double dt) {
    if (!M.on) return;
    switch (M.view) {
        case V_FIGHT: fightTick(dt); break;
        case V_RACE: raceTick(dt); break;
        case V_FPS: pbTick(dt); break;
    }
}
static void gameDraw(HDC dc) {
    switch (M.view) {
        case V_OMOK: omDraw(dc); break;
        case V_YUT: yutDraw(dc); break;
        case V_FIGHT: fightDraw(dc); break;
        case V_RACE: raceDraw(dc); break;
        case V_FPS: pbDraw(dc); break;
    }
}
static void gameClick(int x, int y) {
    switch (M.view) {
        case V_OMOK: omClick(x, y); break;
        case V_YUT: yutClick(x, y); break;
        case V_FPS:
            if (PB.on && !PB.over) {
                pbMouse(true);   // 첫 클릭은 마우스 조준 시작
                PB.qShoot = true;
            }
            break;
    }
}
static std::string joinCsv(const std::vector<std::string>& v) {
    std::string s;
    for (auto& e : v) s += (s.empty() ? "" : ",") + e;
    return s;
}
static std::wstring namesOf(const std::vector<std::string>& v) {
    std::wstring s;
    for (auto& e : v) s += (s.empty() ? L"" : L", ") + pname(e);
    return s;
}
// 판이 끝나면 대결 대표(남아 있는 첫 번째 사람)가 방 전체에 결과를 알린다. 방장은 점수를 준다.
static void reportResult(int winIdx) {
    if (!M.on || M.reported) return;
    M.reported = true;
    if (!mLeader()) return;
    std::vector<std::string> in;
    for (int i = 0; i < (int)M.m.size(); i++)
        if (M.here[i]) in.push_back(M.m[i]);
    std::wstring t = std::wstring(VIEW_NAMES[M.view]) + L" (" + namesOf(in) + L") → " +
                     (winIdx >= 0 ? pname(M.m[winIdx]) + L" 승리! +20점" : std::wstring(L"무승부"));
    std::string wp = winIdx >= 0 ? M.m[winIdx] : "";
    sendMsg("MRES", wp + "|" + U8(t));
    logLine(L"[대결] " + t);
    if (g_isHost && !wp.empty()) hostAward({wp}, 20);
}

// ═════════════════════════ 대결: 초대 → 수락 → 시작 ═════════════════════════
struct InvOut {   // 내가 보낸 초대
    bool on = false;
    std::string id;
    int view = 0;
    std::vector<std::string> to;
    std::map<std::string, int> st;   // 0 응답 대기, 1 수락, 2 거절
    ULONGLONG at = 0;
};
struct InvIn {   // 내가 받은 초대
    bool on = false;
    std::string id, from;
    int view = 0;
    std::vector<std::string> all;
    ULONGLONG at = 0;
};
static InvOut INV;
static InvIn INVIN;
static std::set<std::string> g_sel;   // 초대 화면에서 고른 사람
static std::string g_waitId;          // 수락하고 시작을 기다리는 초대
static ULONGLONG g_waitAt = 0;
static int g_invSeq = 0;

static void endMatchLocal(const std::wstring& why) {
    if (!M.on) return;
    g_inMatch.erase(g_pid);
    M = Match();
    gameStop();
    updateStrip();
    if (!why.empty()) sys(why);
    invalidateCanvas();
}
static void leaveMatch(const std::wstring& why) {
    if (!M.on) return;
    sendMsg("MLEAVE", M.id);
    endMatchLocal(why);
}
// id = 나간 대결 (비어 있으면 방 자체를 나간 것). 예전 대결에서 나간다는 알림이 늦게 와도 새 대결에는 영향이 없게 한다
static void memberLeft(const std::string& pid, const std::string& id = "") {
    auto it = g_inMatch.find(pid);
    if (it != g_inMatch.end() && (id.empty() || it->second == id)) g_inMatch.erase(it);
    int i = mIdx(pid);
    if (!M.on || i < 0 || !M.here[i] || (!id.empty() && id != M.id)) return;
    M.here[i] = 0;
    if (mCount() < 2) M.reported = true;   // 혼자 남아서 끝난 판은 점수를 주지 않음
    gmsg(mName(i) + L" 님이 대결에서 나갔습니다.");
    gameLeft(i);
    if (mCount() < 2) endMatchLocal(L"함께 하던 사람이 모두 나가서 대결이 끝났습니다.");
    else updateStrip();
}
static void applyMStart(const std::string& id, int view, unsigned seed, const std::vector<std::string>& mem) {
    if (!isMatchView(view) || (int)mem.size() < VIEW_MIN[view] || (int)mem.size() > VIEW_MAX[view]) return;
    bool mine = inList(mem, g_pid);
    if (INVIN.on && INVIN.id == id) INVIN.on = false;
    if (g_waitId == id) g_waitId.clear();
    if (mine && M.on && M.id != id) leaveMatch(L"");   // 다른 대결을 하던 중이면 그쪽에서는 나옴
    for (auto& p : mem) g_inMatch[p] = id;
    std::wstring who = namesOf(mem);
    if (!mine) {
        logLine(L"[대결] " + who + L" — " + VIEW_NAMES[view] + L" 시작!");
        return;
    }
    M = Match();
    M.on = true;
    M.id = id;
    M.view = view;
    M.m = mem;
    M.here.assign(mem.size(), 1);
    M.seed = seed;
    for (auto& p : mem) M.names.push_back(pname(p));
    sys(who + L" — " + VIEW_NAMES[view] + L" 대결 시작!");
    gameStart();
    setView(view);
    updateStrip();
}
static void applyRestart(unsigned seed, const std::vector<std::string>& mem) {
    if (!M.on || !inList(mem, g_pid) || (int)mem.size() < VIEW_MIN[M.view] || (int)mem.size() > VIEW_MAX[M.view]) return;
    M.m = mem;
    M.here.assign(mem.size(), 1);
    M.names.clear();
    for (int i = 0; i < (int)mem.size(); i++) {
        M.names.push_back(pname(mem[i]));
        auto it = g_inMatch.find(mem[i]);
        if (mem[i] != g_pid && (it == g_inMatch.end() || it->second != M.id)) M.here[i] = 0;   // 그 사이에 나간 사람
    }
    M.seed = seed;
    gmsg(std::wstring(VIEW_NAMES[M.view]) + L" 다시 시작!");
    gameStart();
    updateStrip();
}
static void requestRestart() {
    if (!mLeader()) return;
    std::vector<std::string> mem;
    for (int i = 0; i < (int)M.m.size(); i++)
        if (M.here[i]) mem.push_back(M.m[i]);
    if ((int)mem.size() < VIEW_MIN[M.view]) return;
    unsigned seed = g_rng();
    sendMsg("G_RESTART", std::to_string(seed) + "|" + joinCsv(mem));
    applyRestart(seed, mem);
}
static void cancelInvite(const std::wstring& why) {
    if (!INV.on) return;
    sendMsg("INV_CANCEL", INV.id);
    INV.on = false;
    if (!why.empty()) sys(why);
}
static void sendInvite(int view) {
    std::vector<std::string> to;
    for (auto& p : GP)
        if (p.pid != g_pid && g_sel.count(p.pid)) to.push_back(p.pid);
    if (to.empty() || (int)to.size() > VIEW_MAX[view] - 1) return;
    cancelInvite(L"");
    INV = InvOut();
    INV.on = true;
    INV.id = g_pid + "-" + std::to_string(++g_invSeq);
    INV.view = view;
    INV.to = to;
    INV.at = GetTickCount64();
    sendMsg("INV", INV.id + "|" + std::to_string(view) + "|" + joinCsv(to));
    sys(namesOf(to) + L" 님에게 " + VIEW_NAMES[view] + L" 초대를 보냈습니다. 응답을 기다리는 중...");
}
static int invAccepted() {
    int n = 0;
    for (auto& p : INV.to)
        if (findP(p) && INV.st.count(p) && INV.st[p] == 1) n++;
    return n;
}
static void startInvited() {   // 수락한 사람들과 시작
    if (!INV.on) return;
    std::vector<std::string> mem{g_pid};
    for (auto& p : INV.to)
        if (findP(p) && INV.st.count(p) && INV.st[p] == 1) mem.push_back(p);
    if ((int)mem.size() < VIEW_MIN[INV.view]) return;
    unsigned seed = g_rng();
    std::string id = INV.id;
    int view = INV.view;
    INV.on = false;
    g_sel.clear();
    sendMsg("MSTART", id + "|" + std::to_string(view) + "|" + std::to_string(seed) + "|" + joinCsv(mem));
    applyMStart(id, view, seed, mem);
}
static void invCheck() {   // 모두 응답했으면 바로 시작 (전원 거절이면 취소)
    if (!INV.on) return;
    int wait = 0;
    for (auto& p : INV.to)
        if (findP(p) && (!INV.st.count(p) || INV.st[p] == 0)) wait++;
    if (wait) return;
    if (invAccepted() >= VIEW_MIN[INV.view] - 1) startInvited();
    else cancelInvite(L"초대받은 사람이 모두 거절해서 초대가 취소됐습니다.");
}
static void acceptInv() {
    if (!INVIN.on) return;
    sendMsg("INV_OK", INVIN.id);
    g_waitId = INVIN.id;
    g_waitAt = GetTickCount64();
    INVIN.on = false;
    sys(L"초대를 수락했습니다. 초대한 사람이 시작하면 바로 게임이 열립니다.");
    SetFocus(g_hwnd);
}
static void declineInv() {
    if (!INVIN.on) return;
    sendMsg("INV_NO", INVIN.id);
    INVIN.on = false;
}

// 명단이 바뀌었을 때 (누가 들어오거나 나감)
static void onRosterChanged() {
    std::vector<std::string> gone;
    for (auto& kv : g_inMatch)
        if (!findP(kv.first)) gone.push_back(kv.first);
    for (auto& p : gone) memberLeft(p);
    if (INVIN.on && !findP(INVIN.from)) INVIN.on = false;
    for (auto it = g_sel.begin(); it != g_sel.end();) it = findP(*it) ? std::next(it) : g_sel.erase(it);
    invCheck();
    grpsCheck();
    updateStrip();
}
static void roomTick() {
    groupHostTick();
    ULONGLONG now = GetTickCount64();
    if (INV.on && now - INV.at > 30000) {
        if (invAccepted() >= VIEW_MIN[INV.view] - 1) startInvited();
        else cancelInvite(L"응답이 없어 초대를 취소했습니다.");
        invalidateCanvas();
    }
    if (INVIN.on && now - INVIN.at > 30000) { INVIN.on = false; invalidateCanvas(); }
    if (!g_waitId.empty() && now - g_waitAt > 40000) { g_waitId.clear(); invalidateCanvas(); }
}
static void printHelp() {
    logLine(L"──── 도움말 ────");
    logLine(L"· 끝말잇기 · 가위바위보: 방 전체가 함께");
    logLine(L"· 오목 · 윷놀이 · 격투 · 레이싱 · 페인트볼: 탭에서 함께 할 사람을 고르고 초대");
    logLine(L"· 이기면 공동 점수판에 점수가 쌓여요");
    logLine(L"· 위쪽 '꾸미기'에서 내 색 · 장식 · 판 테마를 바꿀 수 있어요");
    logLine(L"· Enter = 채팅 입력, Esc = 게임 화면으로, F11 = 전체 화면");
    logLine(L"───────────────");
}
static void chatFrom(const std::string& pid, const std::wstring& msg) {   // 이름은 그 사람이 고른 색으로
    logBreak(120);
    logAppend(pname(pid) + L"  ", darker(pcolor(pid), 0.8), true);
    logAppend(msg, RGB(36, 44, 70), false);
    logEnd();
}
static void setProfile(const std::string& pid, int color, int hat, int piece) {
    if (GPlayer* p = findP(pid)) {
        p->color = color & 7;
        p->hat = (hat % HAT_COUNT + HAT_COUNT) % HAT_COUNT;
        p->piece = (piece % PIECE_COUNT + PIECE_COUNT) % PIECE_COUNT;
    }
}

// ═════════════════════════ 받은 메시지 ═════════════════════════  형식: 보낸사람ID|종류|내용
static void handleLine(const std::string& line) {
    size_t p1 = line.find('|');
    if (p1 == std::string::npos) return;
    size_t p2 = line.find('|', p1 + 1);
    std::string from = line.substr(0, p1);
    std::string type = line.substr(p1 + 1, p2 == std::string::npos ? std::string::npos : p2 - p1 - 1);
    std::string payload = p2 == std::string::npos ? "" : line.substr(p2 + 1);
    if (from == g_pid || type.empty()) return;   // 내가 보낸 메시지가 되돌아온 것
    auto f = split(payload, '|');
    auto fs = [&](size_t i) { return i < f.size() ? f[i] : std::string(); };
    auto fi = [&](size_t i) { return i < f.size() ? atoi(f[i].c_str()) : 0; };
    invalidateCanvas();

    // 모두 공통 (방장 포함)
    if (g_connected && !g_ended) {
        if (isGameMsg(type)) {   // 대결 중인 게임의 메시지: 같은 대결에 있는 사람 것만 처리
            int i = M.on ? mIdx(from) : -1;
            if (i < 0 || !M.here[i]) return;
            if (type == "G_RESTART") {
                if (i == mLeaderIdx()) applyRestart((unsigned)strtoul(fs(0).c_str(), nullptr, 10), split(fs(1), ','));
            } else {
                gameMsg(i, type, f);
            }
            return;
        }
        if (type == "INV") {
            int v = fi(1);
            auto to = split(fs(2), ',');
            if (inList(to, g_pid) && findP(from) && isMatchView(v)) {
                INVIN = InvIn();
                INVIN.on = true;
                INVIN.id = fs(0);
                INVIN.from = from;
                INVIN.view = v;
                INVIN.all = to;
                INVIN.at = GetTickCount64();
                logLine(L"[초대] " + pname(from) + L" 님이 " + VIEW_NAMES[v] + L"에 초대했습니다! 게임 화면에서 수락/거절하세요.");
                FlashWindow(g_hwnd, TRUE);
            }
            return;
        }
        if (type == "INV_OK" || type == "INV_NO") {
            if (INV.on && INV.id == fs(0) && inList(INV.to, from)) {
                INV.st[from] = type == "INV_OK" ? 1 : 2;
                sys(pname(from) + (type == "INV_OK" ? L" 님이 초대를 수락했습니다." : L" 님이 초대를 거절했습니다."));
                invCheck();
            }
            return;
        }
        if (type == "INV_CANCEL") {
            if (INVIN.on && INVIN.id == fs(0)) INVIN.on = false;
            if (g_waitId == fs(0)) { g_waitId.clear(); sys(L"초대가 취소되었습니다."); }
            return;
        }
        if (type == "MSTART") {
            auto mem = split(fs(3), ',');
            if (!mem.empty() && mem[0] == from) applyMStart(fs(0), fi(1), (unsigned)strtoul(fs(2).c_str(), nullptr, 10), mem);
            return;
        }
        if (type == "MLEAVE") { memberLeft(from, fs(0)); return; }
        if (type == "MRES") {
            logLine(L"[대결] " + W(fs(1)));
            if (g_isHost && !fs(0).empty() && findP(fs(0))) hostAward({fs(0)}, 20);
            return;
        }
        if (type == "GRPS") { GR.picks[from] = std::max(0, std::min(2, fi(0))); grpsCheck(); return; }
        if (type == "PROF") {
            setProfile(from, fi(0), fi(1), fi(2));
            if (g_isHost) grpSendRoster();
            return;
        }
        if (type == "CHAT") {
            if (findP(from)) chatFrom(from, W(payload));
            return;
        }
    }

    if (g_isHost) {
        if (type == "JOIN_REQ") {
            if (findP(from)) { grpSendRoster(); return; }   // 재전송된 요청
            if ((int)GP.size() >= GROUP_MAX) { sendMsg("REJECT", from + "|방이 꽉 찼어요 (최대 8명)"); return; }
            GPlayer np;
            np.pid = from;
            np.name = safeName(W(fs(0)));
            if (np.name.empty()) np.name = L"플레이어";
            if (np.name.size() > 12) np.name.resize(12);
            np.color = fi(1) & 7;
            np.hat = (fi(2) % HAT_COUNT + HAT_COUNT) % HAT_COUNT;
            np.piece = (fi(3) % PIECE_COUNT + PIECE_COUNT) % PIECE_COUNT;
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
    if (from != g_hostPid) return;   // 아래는 방장만 보낼 수 있는 메시지
    if (type == "SYS") sys(W(payload));
    else if (type == "KSTART") kApplyStart(W(fs(0)), fs(1), fi(2));
    else if (type == "ACCEPT") kApplyAccept(fs(0), W(fs(1)), fs(2), fi(3), fi(4));
    else if (type == "DENY") kApplyDeny(fs(0), W(fs(1)), W(fs(2)));
    else if (type == "OUT") kApplyOut(fs(0), fs(1), fi(2), W(fs(3)));
    else if (type == "KEND") kApplyEnd(fs(0), fi(1));
    else if (type == "CLOSE" || type == "LEAVE") onDisconnected(L"방장이 방을 닫았습니다.");
}

// 번호가 붙은 메시지: 보낸 사람별로 번호 순서대로, 한 번씩만 처리 (빠진 번호는 잠깐 기다렸다가 건너뜀)
struct RxState {
    bool init = false;
    unsigned next = 0;
    std::map<unsigned, std::pair<std::string, ULONGLONG>> buf;
};
static std::map<std::string, RxState> g_rx;
static void rxFlush(const std::string& from) {
    unsigned gen = g_gen;
    ULONGLONG now = GetTickCount64();
    for (;;) {
        auto rit = g_rx.find(from);
        if (rit == g_rx.end() || rit->second.buf.empty()) return;
        RxState& r = rit->second;
        auto it = r.buf.begin();
        if (it->first != r.next) {
            if (now - it->second.second < 900) return;
            r.next = it->first;
        }
        std::string line = it->second.first;
        r.buf.erase(it);
        r.next++;
        handleLine(line);
        if (gen != g_gen) return;   // 처리하다가 방을 나갔으면 중단
    }
}
static void rxTick() {
    std::vector<std::string> who;
    for (auto& kv : g_rx)
        if (!kv.second.buf.empty()) who.push_back(kv.first);
    unsigned gen = g_gen;
    for (auto& w : who) {
        rxFlush(w);
        if (gen != g_gen) return;
    }
}
static void handleNet(const std::string& line) {
    size_t p1 = line.find('|');
    if (p1 == std::string::npos || p1 + 1 >= line.size()) return;
    if (line[p1 + 1] != '~') { handleLine(line); return; }
    size_t p2 = line.find('~', p1 + 2);
    if (p2 == std::string::npos) return;
    std::string from = line.substr(0, p1);
    if (from == g_pid) return;
    unsigned seq = (unsigned)strtoul(line.substr(p1 + 2, p2 - p1 - 2).c_str(), nullptr, 10);
    RxState& r = g_rx[from];
    if (!r.init) { r.init = true; r.next = seq; }
    if (seq < r.next || r.buf.count(seq)) return;   // 이미 처리했거나 받아 둔 것
    r.buf[seq] = {from + "|" + line.substr(p2 + 1), GetTickCount64()};
    rxFlush(from);
}

// ═════════════════════════ 방 화면 ═════════════════════════
static void drawPlayerPanel(HDC dc, int view) {
    roundRect(dc, 15, 75, 205, 395, RGB(255, 255, 255), COL_LINE, 18, 2);
    text(dc, 28, 82, 180, 24, L"참가자 · 점수", 15, COL_SUB, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    int y = 112;
    for (auto& p : GP) {
        bool cur = view == V_WORD && K.on && p.pid == K.cur;
        bool out = view == V_WORD && K.on && !p.alive;
        if (cur) roundRect(dc, 22, y - 2, 191, 40, RGB(255, 238, 214), COL_ACC, 12, 2);
        drawHead(dc, 40, y + 21, 10, out ? COL_GRAY : pcolor(p.pid), phat(p.pid));
        COLORREF nc = out ? RGB(175, 178, 188) : COL_TEXT;
        std::wstring nm = (p.pid == g_hostPid ? L"★" : L"") + p.name + (p.pid == g_pid ? L" (나)" : L"");
        std::wstring mark;
        if (view == V_RPS && GR.picks.count(p.pid)) mark = L"✓ ";
        text(dc, 56, y, 88, 36, nm, 14, nc, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        text(dc, 142, y, 66, 36, out ? std::wstring(L"탈락") : mark + I(p.score) + L"점", 13, mark.empty() ? nc : COL_WINC, !mark.empty(),
             DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        y += 44;
    }
}
static void drawWord(HDC dc) {
    title(dc, L"끝말잇기", groupSub());
    button(dc, grpCopyBtn, g_relay ? L"코드 복사" : L"IP 복사", true, COL_GRAY, 14);
    drawPlayerPanel(dc, V_WORD);
    int rx = 240, rw = 390;
    if (K.on) {
        text(dc, rx, 76, rw, 22, L"현재 단어", 14, COL_SUB, true);
        roundRect(dc, rx, 100, rw, 90, RGB(255, 255, 255), COL_LINE, 18, 2);
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
        roundRect(dc, rx, 262, rw, 18, RGB(232, 241, 251), COL_LINE, 10, 1);
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
        roundRect(dc, rx, 76, rw, 118, RGB(255, 255, 255), COL_LINE, 18, 2);
        text(dc, rx, 82, rw, 22, g_relay ? L"방 코드 (친구들에게 알려주세요)" : L"방장 IP (같은 와이파이 친구에게 알려주세요)", 14, COL_SUB, true);
        text(dc, rx, 104, rw, 80, roomLabel(), g_relay ? 50 : 34, COL_ME, true);
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
        roundRect(dc, b.x, b.y, b.w, b.h, sel ? RGB(255, 238, 214) : RGB(255, 255, 255), sel ? COL_ACC : COL_LINE, 20,
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

// ── 초대 화면: 함께 할 사람을 여러 명 고를 수 있다 ──
static Btn pickBtn(int i) { return {40 + (i % 2) * 290, 92 + (i / 2) * 68, 270, 58}; }
static const Btn invAllBtn{40, 392, 150, 46}, invSendBtn{205, 392, 255, 46}, invCancelBtn{475, 392, 125, 46};
static std::vector<const GPlayer*> others() {
    std::vector<const GPlayer*> v;
    for (auto& p : GP)
        if (p.pid != g_pid) v.push_back(&p);
    return v;
}
static void drawPicker(HDC dc, int view) {
    std::wstring cnt = VIEW_MIN[view] == VIEW_MAX[view] ? I(VIEW_MAX[view]) + L"명" : I(VIEW_MIN[view]) + L"~" + I(VIEW_MAX[view]) + L"명";
    title(dc, VIEW_NAMES[view], cnt + L"이 하는 게임이에요. 함께 할 사람을 고르고 초대를 보내세요. (승리 +20점)");
    auto ov = others();
    if (ov.empty()) {
        text(dc, 0, 170, CW, 40, L"아직 다른 참가자가 없어요.", 20, COL_SUB, true);
        text(dc, 0, 214, CW, 30, (g_relay ? L"방 코드 " : L"내 IP ") + roomLabel() + L" 를 친구에게 보내세요! (아래 '복사' 버튼)", 16, COL_TEXT, true);
        return;
    }
    bool inviting = INV.on && INV.view == view;
    if (!inviting && g_sel.empty() && ov.size() == 1) g_sel.insert(ov[0]->pid);   // 둘뿐이면 자동 선택
    int nSel = 0;
    for (int i = 0; i < (int)ov.size() && i < 8; i++) {
        const GPlayer& p = *ov[i];
        Btn b = pickBtn(i);
        int st = inviting && inList(INV.to, p.pid) ? (INV.st.count(p.pid) ? INV.st[p.pid] : 0) : -1;
        bool sel = inviting ? st >= 0 : g_sel.count(p.pid) > 0;
        bool busy = g_inMatch.count(p.pid) > 0;
        nSel += sel ? 1 : 0;
        COLORREF pc = PCOL[p.color & 7];
        roundRect(dc, b.x, b.y, b.w, b.h, sel ? mix(pc, RGB(255, 255, 255), 0.86) : RGB(255, 255, 255), sel ? pc : COL_LINE, 16, sel ? 3 : 2);
        circle(dc, b.x + 24, b.y + 29, 11, sel ? pc : RGB(255, 255, 255), sel ? darker(pc, 0.7) : COL_GRAY, 2);
        if (sel) text(dc, b.x + 13, b.y + 18, 22, 22, L"✓", 15, RGB(255, 255, 255), true);
        drawHead(dc, b.x + 62, b.y + 34, 13, pc, p.hat);
        text(dc, b.x + 86, b.y + 6, b.w - 96, 26, p.name, 17, COL_TEXT, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        std::wstring s = st == 1   ? L"수락!"
                         : st == 2 ? L"거절"
                         : st == 0 ? L"응답 기다리는 중..."
                         : busy    ? L"다른 대결 중 (초대는 보낼 수 있어요)"
                         : sel     ? L"초대할 사람으로 선택됨"
                                   : L"클릭해서 선택";
        text(dc, b.x + 86, b.y + 30, b.w - 96, 22, s, 13, st == 1 ? COL_WINC : st == 2 ? COL_OP : COL_SUB, st >= 1,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (inviting) {
        int ok = invAccepted();
        button(dc, invSendBtn, ok ? L"지금 시작 (" + I(ok + 1) + L"명)" : std::wstring(L"응답 기다리는 중..."), ok >= VIEW_MIN[view] - 1, COL_WINC, 17);
        button(dc, invCancelBtn, L"초대 취소", true, COL_GRAY, 15);
        text(dc, 40, 392, 150, 46, L"모두 응답하면\n자동으로 시작해요", 12, COL_SUB, false, DT_CENTER | DT_WORDBREAK);
    } else {
        button(dc, invAllBtn, L"전체 선택", true, COL_GRAY, 15);
        button(dc, invSendBtn, nSel ? L"초대 보내기 (" + I(nSel) + L"명)" : std::wstring(L"함께 할 사람을 고르세요"), nSel > 0, COL_ME, 17);
    }
    if (!g_waitId.empty()) text(dc, 0, 446, CW, 24, L"초대를 수락했어요. 초대한 사람이 시작하면 바로 게임이 열립니다.", 14, COL_WINC, true);
    else if (M.on) text(dc, 0, 446, CW, 24, std::wstring(L"지금 ") + VIEW_NAMES[M.view] + L" 대결 중이에요. 새 대결을 시작하면 그 대결에서는 나오게 됩니다.", 13, COL_SUB);
}
static void pickerClick(int view, int x, int y) {
    auto ov = others();
    int maxSel = VIEW_MAX[view] - 1;
    if (INV.on && INV.view == view) {
        if (invSendBtn.hit(x, y)) startInvited();
        else if (invCancelBtn.hit(x, y)) cancelInvite(L"초대를 취소했습니다.");
        return;
    }
    for (int i = 0; i < (int)ov.size() && i < 8; i++) {
        if (!pickBtn(i).hit(x, y)) continue;
        const std::string& pid = ov[i]->pid;
        if (g_sel.count(pid)) g_sel.erase(pid);
        else if ((int)g_sel.size() < maxSel) g_sel.insert(pid);
        else gmsg(std::wstring(VIEW_NAMES[view]) + L"은(는) 최대 " + I(VIEW_MAX[view]) + L"명까지 할 수 있어요.");
        return;
    }
    if (invAllBtn.hit(x, y) && !ov.empty()) {
        bool all = (int)g_sel.size() >= std::min(maxSel, (int)ov.size());
        g_sel.clear();
        if (!all)
            for (int i = 0; i < (int)ov.size() && i < maxSel; i++) g_sel.insert(ov[i]->pid);
    } else if (invSendBtn.hit(x, y)) {
        sendInvite(view);
    }
}
static const Btn invYes{150, 272, 160, 46}, invNo{330, 272, 160, 46};
static void drawInvOverlay(HDC dc) {
    if (!INVIN.on) return;
    roundRect(dc, 100, 146, 440, 196, mix(COL_ACC, RGB(255, 255, 255), 0.6), mix(COL_ACC, RGB(255, 255, 255), 0.6), 24, 1);
    roundRect(dc, 100, 140, 440, 196, RGB(255, 255, 255), COL_ACC, 24, 4);
    text(dc, 100, 154, 440, 34, L"✉ 게임 초대", 22, COL_TEXT, true);
    text(dc, 110, 192, 420, 30, pname(INVIN.from) + L" 님이 " + VIEW_NAMES[INVIN.view] + L"에 초대했어요!", 17, COL_TEXT, true);
    std::vector<std::string> with{INVIN.from};
    for (auto& p : INVIN.all)
        if (p != g_pid) with.push_back(p);
    text(dc, 110, 224, 420, 24, L"함께: " + namesOf(with), 13, COL_SUB, false, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    button(dc, invYes, L"수락", true, COL_WINC, 18);
    button(dc, invNo, L"거절", true, COL_GRAY, 18);
}
static void drawRoom(HDC dc) {
    switch (g_view) {
        case V_WORD: drawWord(dc); break;
        case V_RPS: drawGRps(dc); break;
        default:
            if (M.on && M.view == g_view) gameDraw(dc);
            else drawPicker(dc, g_view);
    }
    drawInvOverlay(dc);
}
static void roomClick(int x, int y) {
    if (INVIN.on) {   // 초대 창이 떠 있으면 그것부터
        if (invYes.hit(x, y)) acceptInv();
        else if (invNo.hit(x, y)) declineInv();
        return;
    }
    switch (g_view) {
        case V_WORD:
            if (grpCopyBtn.hit(x, y)) {
                copyToClipboard(roomLabel());
                sys(roomLabel() + L" 를 클립보드에 복사했습니다.");
            } else if (g_isHost && !K.on && grpStartBtn.hit(x, y)) {
                hostStartK();
            }
            break;
        case V_RPS:
            for (int i = 0; i < 3; i++)
                if (grpsBtn(i).hit(x, y)) grpsPick(i);
            break;
        default:
            if (M.on && M.view == g_view) {
                if (mLeader() && g_againBtn.hit(x, y)) requestRestart();
                else gameClick(x, y);
            } else {
                pickerClick(g_view, x, y);
            }
    }
}

// ═════════════════════════ 꾸미기 ═════════════════════════
static bool g_custom = false;
static const Btn custClose{250, 408, 140, 38};
static Btn custColorBtn(int i) { return {60 + i * 44, 88, 38, 38}; }
static Btn custHatBtn(int i) { return {60 + i * 58, 152, 52, 60}; }
static Btn custPieceBtn(int i) { return {60 + i * 58, 238, 52, 46}; }
static Btn custThemeBtn(int i) { return {60 + i * 90, 310, 82, 70}; }
static void profileChanged() {
    saveCfg();
    if (g_connected && !g_ended) {
        setProfile(g_pid, g_myColor, g_myHat, g_myPiece);
        if (g_isHost) grpSendRoster();
        else sendMsg("PROF", std::to_string(g_myColor) + "|" + std::to_string(g_myHat) + "|" + std::to_string(g_myPiece));
    }
}
static void setCustom(bool on) {
    g_custom = on;
    if (!g_connected) showConnUi(!on);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}
static void drawCustom(HDC dc) {
    COLORREF my = PCOL[g_myColor];
    roundRect(dc, 30, 26, 580, 434, RGB(206, 226, 244), RGB(206, 226, 244), 32, 1);
    roundRect(dc, 30, 20, 580, 434, RGB(255, 255, 255), COL_LINE, 32, 2);
    text(dc, 60, 28, 110, 34, L"꾸미기", 24, COL_TEXT, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    text(dc, 150, 33, 440, 26, L"색 · 장식 · 말은 친구들 화면에도 보여요 · 판 테마는 내 화면에만 적용돼요", 12, COL_SUB, false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    text(dc, 60, 66, 360, 20, L"내 색  (돌 · 말 · 캐릭터 · 자동차 · 채팅 이름)", 13, COL_SUB, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    for (int i = 0; i < 8; i++) {
        Btn b = custColorBtn(i);
        if (i == g_myColor) ring(dc, b.x + 19, b.y + 19, 20, COL_TEXT, 3);
        circle(dc, b.x + 19, b.y + 19, 14, PCOL[i], darker(PCOL[i], 0.7), 2);
    }
    text(dc, 60, 130, 360, 20, L"머리 장식  (격투 · 페인트볼 캐릭터)", 13, COL_SUB, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    for (int i = 0; i < HAT_COUNT; i++) {
        Btn b = custHatBtn(i);
        bool sel = i == g_myHat;
        roundRect(dc, b.x, b.y, b.w, b.h, sel ? mix(my, RGB(255, 255, 255), 0.86) : RGB(255, 255, 255), sel ? my : COL_LINE, 14, sel ? 3 : 2);
        drawHead(dc, b.x + 26, b.y + 31, 11, my, i);
        text(dc, b.x - 4, b.y + 43, b.w + 8, 15, HAT_N[i], 10, COL_SUB, sel);
    }
    text(dc, 60, 216, 360, 20, L"윷놀이 말 모양", 13, COL_SUB, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    for (int i = 0; i < PIECE_COUNT; i++) {
        Btn b = custPieceBtn(i);
        bool sel = i == g_myPiece;
        roundRect(dc, b.x, b.y, b.w, b.h, sel ? mix(my, RGB(255, 255, 255), 0.86) : RGB(255, 255, 255), sel ? my : COL_LINE, 14, sel ? 3 : 2);
        drawPiece(dc, b.x + 26, b.y + (i == 5 ? 20 : 17), 10, i, my, g_myHat);
        text(dc, b.x - 4, b.y + 30, b.w + 8, 14, PIECE_N[i], 10, COL_SUB, sel);
    }
    text(dc, 60, 288, 360, 20, L"게임 판 테마  (오목 · 윷놀이 · 레이싱)", 13, COL_SUB, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    for (int i = 0; i < THEME_COUNT; i++) {
        Btn b = custThemeBtn(i);
        const Theme& T = THEMES[i];
        bool sel = i == g_theme;
        if (sel) roundRect(dc, b.x - 5, b.y - 5, b.w + 10, 58, RGB(255, 255, 255), COL_TEXT, 16, 3);
        roundRect(dc, b.x, b.y, b.w, 48, T.board, T.edge, 12, 3);
        for (int k = 1; k <= 3; k++) line(dc, b.x + k * 20, b.y + 7, b.x + k * 20, b.y + 41, T.line, 1);
        for (int k = 1; k <= 2; k++) line(dc, b.x + 8, b.y + k * 16, b.x + b.w - 8, b.y + k * 16, T.line, 1);
        circle(dc, b.x + 40, b.y + 16, 6, my, darker(my, 0.6), 2);
        circle(dc, b.x + 60, b.y + 32, 6, T.node, T.line, 2);
        text(dc, b.x, b.y + 52, b.w, 18, T.name, 13, COL_TEXT, sel);
    }
    // 미리보기
    roundRect(dc, 442, 66, 150, 322, COL_BG, COL_LINE, 20, 2);
    text(dc, 442, 72, 150, 22, L"미리보기", 12, COL_SUB, true);
    Fighter pf;
    pf.x = 517;
    pf.y = 262;
    drawFighter(dc, pf, my, g_myHat, L"", false);
    text(dc, 442, 268, 150, 20, g_myName.empty() ? getText(hName) : g_myName, 14, darker(my, 0.75), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    const Theme& T = THEMES[g_theme];
    roundRect(dc, 456, 300, 122, 72, T.board, T.edge, 12, 3);
    line(dc, 466, 338, 568, 338, T.line, 2);
    circle(dc, 488, 338, 13, T.node, T.line, 2);
    drawPiece(dc, 488, 338, 11, g_myPiece, my, g_myHat);
    raceCar(dc, 542, 338, -0.35f, my, false, L"");
    button(dc, custClose, L"닫기", true, COL_MAIN, 16);
}
static void customClick(int x, int y) {
    if (custClose.hit(x, y) || x < 30 || x > 610 || y < 20 || y > 454) { setCustom(false); return; }
    for (int i = 0; i < 8; i++)
        if (custColorBtn(i).hit(x, y)) { g_myColor = i; profileChanged(); }
    for (int i = 0; i < HAT_COUNT; i++)
        if (custHatBtn(i).hit(x, y)) { g_myHat = i; profileChanged(); }
    for (int i = 0; i < PIECE_COUNT; i++)
        if (custPieceBtn(i).hit(x, y)) { g_myPiece = i; profileChanged(); }
    for (int i = 0; i < THEME_COUNT; i++)
        if (custThemeBtn(i).hit(x, y)) { g_theme = i; saveCfg(); }
    invalidateCanvas();
}

static void updateStrip() {
    updateCopyBtn();
    std::wstring s;
    if (g_ended) s = L"연결이 끊어졌습니다.";
    else if (!g_connected) s = L"";
    else if (M.on) {
        std::vector<std::string> in;
        for (int i = 0; i < (int)M.m.size(); i++)
            if (M.here[i]) in.push_back(M.m[i]);
        s = std::wstring(L"▶ ") + VIEW_NAMES[M.view] + L" 대결 중: " + namesOf(in);
    } else {
        s = (g_relay ? L"방 " : L"IP 방 ") + roomLabel() + L"   ·   " + I((int)GP.size()) + L"명   ·   게임 탭 → 함께 할 사람 선택 → 초대";
    }
    SetWindowTextW(hStrip, s.c_str());
    ShowWindow(hStripBtn, M.on && !g_ended ? SW_SHOW : SW_HIDE);
}
static void layoutTabs() {
    for (int i = 0; i < V_COUNT; i++) {
        SetWindowTextW(hGameBtn[i], VIEW_NAMES[g_tabs[i]]);
        g_ctl[hGameBtn[i]] = {CX + i * g_btnStep, 8, g_btnW, 34, 2};
        placeCtl(hGameBtn[i]);
        ShowWindow(hGameBtn[i], SW_SHOW);
    }
}


// ═════════════════════════ 연결 ═════════════════════════
// IP 직접 연결: 한 줄에 메시지 하나. hostSide = 방장이 참가자 한 명에게서 받는 쪽
static void recvLoop(SOCKET s, unsigned gen, bool hostSide) {
    std::string buf, pid;
    char tmp[4096];
    for (;;) {
        int n = recv(s, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        buf.append(tmp, n);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string* line = new std::string(buf.substr(0, pos));
            buf.erase(0, pos + 1);
            if (hostSide && pid.empty()) {
                size_t b = line->find('|');
                if (b != std::string::npos) pid = line->substr(0, b);
            }
            if (line->empty() || !PostMessageW(g_hwnd, WM_NET, gen, (LPARAM)line)) delete line;
        }
        if (buf.size() > 65536) break;
    }
    if (!hostSide) {
        PostMessageW(g_hwnd, WM_NETCLOSED, gen, 0);
        return;
    }
    bool mine = false;   // 방장 쪽: 이 참가자를 목록에서 빼고, 나갔다고 알림
    {
        std::lock_guard<std::mutex> lk(g_sendMx);
        auto it = std::find(g_clients.begin(), g_clients.end(), s);
        if (it != g_clients.end()) { g_clients.erase(it); mine = true; }
    }
    if (mine) closesocket(s);
    if (!pid.empty()) {
        std::string* line = new std::string(pid + "|LEAVE|");
        if (!PostMessageW(g_hwnd, WM_NET, gen, (LPARAM)line)) delete line;
    }
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
    for (HWND h : {hName, hIp, hPort, hHost, hJoin, hOnline, hCode, hCodeJoin}) EnableWindow(h, en);
}
static void showConnUi(bool show) {
    for (HWND h : {hLblName, hName, hLblIp, hIp, hPort, hHost, hJoin, hStatus, hOnline, hLblCode, hCode, hCodeJoin})
        ShowWindow(h, show ? SW_SHOW : SW_HIDE);
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
static void sendJoinReq() {
    sendMsg("JOIN_REQ", U8(g_myName) + "|" + std::to_string(g_myColor) + "|" + std::to_string(g_myHat) + "|" + std::to_string(g_myPiece));
}
// 방 만들기 (방 코드): 중계 서버에 접속해서 방 주소를 연다
static void startGroupHost() {
    g_relay = true;
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
static void startOnlineJoin() {
    std::wstring wc = trimW(getText(hCode));
    std::string code;
    for (wchar_t c : wc)
        if (c != L' ' && c != L'-') code += (char)towupper(c);
    if (code.size() != 6 || code[0] < 'A' || code[0] >= 'A' + NBROKER) {
        SetWindowTextW(hStatus, L"방 코드 6자리를 정확히 입력하세요.\n(예: AK7F3Q)");
        return;
    }
    g_relay = true;
    g_isHost = false;
    g_busy = true;
    setConnUiEnabled(false);
    SetWindowTextW(hStatus, (L"방 " + W(code) + L" 를 찾는 중...\n(취소: 오른쪽 위 '메인')").c_str());
    unsigned gen = g_gen;
    std::thread([code, gen]() {
        std::string topic = TOPIC_BASE + code + "/g";
        SOCKET s = mqttOpen(code[0] - 'A', topic, topic, g_pid + "|LEAVE|");
        if (gen != g_gen) {   // 그 사이 취소됨
            if (s != INVALID_SOCKET) closesocket(s);
            return;
        }
        if (s != INVALID_SOCKET) {
            g_code = code;
            g_peerTopic = topic;
            g_sock = s;
            std::thread(mqttRecvLoop, s, gen).detach();
        }
        PostMessageW(g_hwnd, WM_RELAYREADY, s != INVALID_SOCKET, 3);
    }).detach();
}
static void relayFail(const std::wstring& why) {
    g_resend.clear();
    g_rx.clear();
    KillTimer(g_hwnd, TIMER_JOIN);
    KillTimer(g_hwnd, TIMER_PING);
    SOCKET s = g_sock;
    g_sock = INVALID_SOCKET;
    if (s != INVALID_SOCKET) closesocket(s);
    g_gen++;   // 이 연결에서 뒤늦게 오는 메시지는 무시
    g_busy = false;
    g_relay = false;
    setConnUiEnabled(true);
    SetWindowTextW(hStatus, why.c_str());
    updateCopyBtn();
}
static void becomeHost() {   // 나 혼자 있는 방으로 시작
    GPlayer me;
    me.pid = g_pid;
    me.name = g_myName;
    me.color = g_myColor;
    me.hat = g_myHat;
    me.piece = g_myPiece;
    GP = {me};
    g_hostPid = g_pid;
    onConnected();
}
// kind: 2 = 방장, 3 = 참가
static void onRelayReady(bool ok, int kind) {
    if (!ok) {
        relayFail(L"중계 서버에 연결할 수 없어요.\n인터넷 연결을 확인하거나, 같은 와이파이라면\nIP로 직접 연결해 보세요.");
        return;
    }
    SetTimer(g_hwnd, TIMER_PING, 20000, nullptr);
    g_roomAddr = W(g_code);
    g_slowRelay = !g_code.empty() && g_code[0] - 'A' == SLOW_BROKER;
    if (kind == 2) {
        copyToClipboard(W(g_code));
        becomeHost();
        sys(L"방 코드 " + W(g_code) + L" 가 클립보드에 복사됐습니다. 친구들에게 보내주세요!");
    } else {
        g_joinTries = 0;
        sendJoinReq();
        SetTimer(g_hwnd, TIMER_JOIN, 700, nullptr);
    }
}
// IP로 방 만들기: 같은 와이파이의 친구들이 내 PC로 직접 접속. 방장이 메시지를 서로에게 전달해 준다.
static void startHost(int port) {
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL yes = TRUE;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons((u_short)port);
    if (bind(ls, (sockaddr*)&a, sizeof(a)) == SOCKET_ERROR || listen(ls, 8) == SOCKET_ERROR) {
        closesocket(ls);
        SetWindowTextW(hStatus, (L"포트 " + I(port) + L"를 열 수 없습니다. (이미 사용 중?)").c_str());
        return;
    }
    g_relay = false;
    g_isHost = true;
    g_tcpHost = true;
    g_listen = ls;
    unsigned gen = g_gen;
    std::thread([ls, gen]() {
        for (;;) {
            SOCKET s = accept(ls, nullptr, nullptr);
            if (s == INVALID_SOCKET) return;   // 방을 닫음
            if (gen != g_gen) { closesocket(s); return; }
            BOOL nd = TRUE;
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nd, sizeof(nd));
            {
                std::lock_guard<std::mutex> lk(g_sendMx);
                g_clients.push_back(s);
            }
            std::thread(recvLoop, s, gen, true).detach();
        }
    }).detach();
    std::wstring ip = bestIP();
    g_roomAddr = ip + (port != 5555 ? L":" + I(port) : L"");
    copyToClipboard(ip);
    becomeHost();
    sys(L"IP 방을 만들었습니다. 내 IP " + g_roomAddr + L" 가 클립보드에 복사됐습니다. 같은 와이파이 친구에게 알려주세요!");
}
static void startJoin(const std::wstring& ipW, int port) {
    g_relay = false;
    g_isHost = false;
    g_busy = true;
    g_roomAddr = ipW + (port != 5555 ? L":" + I(port) : L"");
    setConnUiEnabled(false);
    SetWindowTextW(hStatus, (ipW + L":" + I(port) + L" 에 접속 중...").c_str());
    std::string ip = U8(ipW);
    unsigned gen = g_gen;
    std::thread([ip, port, gen]() {
        SOCKET s = tcpConnect(ip.c_str(), port, 5000);
        if (gen != g_gen) {   // 취소됨
            if (s != INVALID_SOCKET) closesocket(s);
            return;
        }
        if (s != INVALID_SOCKET) g_sock = s;
        PostMessageW(g_hwnd, WM_CONNRESULT, s != INVALID_SOCKET, 1);
    }).detach();
}
static void onDisconnected(const std::wstring& why) {
    if (g_ended) return;
    g_ended = true;
    sys(why);
    sys(L"연결이 끊어졌습니다. 오른쪽 위 '메인'을 눌러 다시 접속할 수 있어요.");
    EnableWindow(hInput, FALSE);
    EnableWindow(hSend, FALSE);
    M = Match();
    gameStop();
    K.on = false;
    INV.on = false;
    INVIN.on = false;
    g_waitId.clear();
    KillTimer(g_hwnd, TIMER_PING);
    SOCKET s = g_sock;
    g_sock = INVALID_SOCKET;
    if (s != INVALID_SOCKET) closesocket(s);
    updateStrip();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}
// 같은 와이파이 친구에게 알려줄 내 IP 하나 (사설 IP 우선)
static std::wstring bestIP() {
    std::wstring all = localIPs(), first, best;
    size_t p = 0;
    while (p <= all.size()) {
        size_t q = all.find(L", ", p);
        std::wstring ip = all.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p);
        if (!ip.empty()) {
            if (first.empty()) first = ip;
            bool priv = ip.rfind(L"192.168.", 0) == 0 || ip.rfind(L"10.", 0) == 0 || ip.rfind(L"172.", 0) == 0;
            if (priv && best.empty()) best = ip;
        }
        if (q == std::wstring::npos) break;
        p = q + 2;
    }
    return best.empty() ? (first.empty() ? L"127.0.0.1" : first) : best;
}

// 복사 버튼 (상태줄 오른쪽): 방 코드 또는 방장 IP를 다시 복사
static bool g_copyOn = false;
static std::wstring copyLabel() { return g_relay ? L"코드 복사" : L"IP 복사"; }
static void updateCopyBtn() {
    if (!hCopyBtn) return;
    bool on = g_connected && !g_ended && !M.on;
    if (on == g_copyOn) return;
    g_copyOn = on;
    KillTimer(g_hwnd, TIMER_COPY);
    if (!on) { ShowWindow(hCopyBtn, SW_HIDE); return; }
    SetWindowTextW(hCopyBtn, copyLabel().c_str());
    ShowWindow(hCopyBtn, SW_SHOW);
    InvalidateRect(hCopyBtn, nullptr, TRUE);
}
static void doCopy() {
    if (!g_copyOn) return;
    copyToClipboard(roomLabel());
    sys((g_relay ? L"방 코드 " : L"방장 IP ") + roomLabel() + L" 를 클립보드에 복사했습니다. 친구에게 붙여넣기(Ctrl+V) 해서 보내세요!");
    SetWindowTextW(hCopyBtn, L"복사 완료!");
    InvalidateRect(hCopyBtn, nullptr, TRUE);
    SetTimer(g_hwnd, TIMER_COPY, 1500, nullptr);
}

static void onConnected() {
    g_connected = true;
    g_busy = false;
    g_custom = false;
    showConnUi(false);
    EnableWindow(hInput, TRUE);
    EnableWindow(hSend, TRUE);
    layoutTabs();
    g_view = V_WORD;
    updateStrip();
    sys((g_relay ? L"방 " : L"IP 방 ") + roomLabel() + L" 에 들어왔습니다! 위쪽 탭에서 게임을 고르세요.");
    printHelp();
    SetWindowTextW(g_hwnd, (L"미니게임 파티 - 방 " + roomLabel() + L" - " + g_myName).c_str());
    InvalidateRect(g_hwnd, nullptr, FALSE);
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
    {
        std::lock_guard<std::mutex> lk(g_sendMx);
        for (SOCKET c : g_clients) {
            shutdown(c, SD_BOTH);
            closesocket(c);
        }
        g_clients.clear();
    }
    SOCKET s = g_sock;
    g_sock = INVALID_SOCKET;
    if (s != INVALID_SOCKET) {
        shutdown(s, SD_BOTH);
        closesocket(s);
    }
    g_connected = g_busy = g_isHost = g_ended = g_relay = g_tcpHost = g_slowRelay = false;
    g_resend.clear();
    g_rx.clear();
    g_code.clear();
    g_peerTopic.clear();
    g_hostPid.clear();
    g_roomAddr.clear();
    K = decltype(K)();
    GP.clear();
    GR = decltype(GR)();
    M = Match();
    g_inMatch.clear();
    gameStop();
    INV = InvOut();
    INVIN = InvIn();
    g_sel.clear();
    g_waitId.clear();
    g_view = V_WORD;
    for (HWND h : hGameBtn) ShowWindow(h, SW_HIDE);
    showConnUi(!g_custom);
    setConnUiEnabled(true);
    SetWindowTextW(hCode, L"");
    EnableWindow(hInput, FALSE);
    EnableWindow(hSend, FALSE);
    SetWindowTextW(hStatus, status.c_str());
    SetWindowTextW(g_hwnd, L"미니게임 파티");
    updateStrip();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}
static void sayBye() {   // 방을 나간다고 알림
    if (!g_connected || g_ended) return;
    sendMsg(g_isHost ? "CLOSE" : "LEAVE");
    if (g_relay && g_sock != INVALID_SOCKET) rawSend(std::string("\xE0\x00", 2));   // MQTT DISCONNECT
}
static void leaveRoom() {
    sayBye();
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
    SetWindowTextW(hFullBtn, on ? L"창 모드" : L"전체 화면");
    applyLayout();
}

static void sendChat() {
    std::wstring t = trimW(getText(hInput));
    SetWindowTextW(hInput, L"");
    if (t.empty() || !g_connected || g_ended) return;
    if (t[0] == L'/') {
        printHelp();
    } else if (K.on && K.cur == g_pid) {   // 끝말잇기 내 차례에는 입력이 곧 단어
        std::wstring w;
        for (wchar_t c : t)
            if (c != L' ' && c != L'|') w += c;
        chatFrom(g_pid, w + L"  (제출)");
        submitWord(w);
    } else {
        std::replace(t.begin(), t.end(), L'|', L'/');
        sendMsg("CHAT", U8(t));
        chatFrom(g_pid, t);
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
    // 배경 방울 장식
    static const int BUB[][4] = {{40, 140, 34, 0}, {86, 230, 14, 1}, {58, 360, 24, 2}, {96, 440, 10, 0},
                                 {600, 130, 28, 2}, {560, 210, 12, 0}, {596, 330, 36, 1}, {552, 430, 16, 2},
                                 {70, 40, 12, 1}, {575, 40, 18, 0}};
    static const COLORREF BC[3] = {RGB(212, 243, 235), RGB(220, 232, 255), RGB(255, 242, 210)};
    for (auto& b : BUB) circle(dc, b[0], b[1], b[2], BC[b[3]], BC[b[3]], 1);
    text(dc, 0, 14, CW, 54, L"🎮", 34, COL_TEXT, false, DT_CENTER | DT_VCENTER | DT_SINGLELINE, true);
    text(dc, 0, 14, 268, 54, L"미니게임", 34, COL_MAIN, true, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    text(dc, 372, 14, 268, 54, L"파티", 34, COL_ME, true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    text(dc, 0, 68, CW, 26, L"끝말잇기 · 가위바위보 · 오목 · 윷놀이 · 격투 · 레이싱 · 페인트볼 — 2명부터 8명까지!", 14, COL_SUB);
    roundRect(dc, 125, 110, 390, 370, RGB(206, 226, 244), RGB(206, 226, 244), 32, 1);   // 카드 그림자
    roundRect(dc, 125, 104, 390, 370, RGB(255, 255, 255), COL_LINE, 32, 2);
    line(dc, 150, 268, 490, 268, COL_LINE, 1);
    fillRect(dc, 205, 258, 230, 20, RGB(255, 255, 255));
    text(dc, 205, 258, 230, 20, L"같은 와이파이라면 IP로 직접 연결", 13, COL_SUB);
    drawHead(dc, 84, 290, 16, PCOL[g_myColor], g_myHat);   // 내 캐릭터 (꾸미기에서 바꿈)
}
static void drawCanvas(HDC dc) {
    g_againBtn = {0, 0, 0, 0};
    fillRect(dc, 0, 0, CW, CH, COL_BG);
    if (g_custom) { drawCustom(dc); return; }
    if (!g_connected) drawConnect(dc);
    else drawRoom(dc);
}
static void canvasClick(int x, int y) {
    if (g_custom) { customClick(x, y); return; }
    if (!g_connected || g_ended) return;
    roomClick(x, y);
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

// 둥글고 말랑한 파스텔 버튼 (BS_OWNERDRAW)
static void drawOwnerBtn(const DRAWITEMSTRUCT* di) {
    HWND h = di->hwndItem;
    HDC dc = di->hDC;
    RECT r = di->rcItem;
    int ht = r.bottom - r.top;
    bool down = (di->itemState & ODS_SELECTED) != 0, off = (di->itemState & ODS_DISABLED) != 0;
    bool onCard = h == hOnline || h == hCodeJoin || h == hHost || h == hJoin ||
                  false;   // 메인 화면 흰 카드 위

    // 버튼 종류별 색: 채움색, 글자색, 테두리색
    COLORREF fill = COL_MAIN, fg = RGB(255, 255, 255), border = 0;
    bool outline = false;
    int tab = -1;
    for (int i = 0; i < g_tabCount; i++)
        if (h == hGameBtn[i]) tab = i;
    if (tab >= 0) {
        bool sel = g_connected && g_tabs[tab] == g_view;
        if (sel) fill = COL_MAIN;
        else { fill = RGB(255, 255, 255); fg = COL_TEXT; outline = true; border = COL_LINE; }
    } else if (h == hHomeBtn || h == hFullBtn || h == hCustomBtn) {
        fill = RGB(255, 255, 255); fg = COL_TEXT; outline = true; border = COL_LINE;
    } else if (h == hOnline) fill = COL_MAIN;
    else if (h == hCodeJoin) fill = COL_ACC;
    else if (h == hHost || h == hJoin) fill = RGB(176, 156, 226);
    else if (h == hStripBtn) fill = COL_OP;
    if (off) { fill = RGB(226, 233, 242); fg = RGB(158, 170, 188); outline = false; }
    if (down) fill = darker(fill, 0.9);
    if (!outline) border = fill;

    HBRUSH bgb = CreateSolidBrush(onCard ? RGB(255, 255, 255) : COL_WIN);
    FillRect(dc, &r, bgb);
    DeleteObject(bgb);
    int sh = std::max(2, ht / 10);   // 아래 그림자 두께
    int y0 = r.top + (down ? sh : 0);
    COLORREF shade = outline ? COL_LINE : darker(fill, 0.82);
    if (!down) {
        HBRUSH sb = CreateSolidBrush(shade);
        HPEN sp = CreatePen(PS_SOLID, 1, shade);
        HGDIOBJ ob = SelectObject(dc, sb), op = SelectObject(dc, sp);
        RoundRect(dc, r.left, r.top + sh, r.right, r.bottom, ht, ht);
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(sb); DeleteObject(sp);
    }
    HBRUSH fb = CreateSolidBrush(fill);
    HPEN fp = CreatePen(PS_SOLID, std::max(1, ht / 18), border);
    HGDIOBJ ob = SelectObject(dc, fb), op = SelectObject(dc, fp);
    RoundRect(dc, r.left, y0, r.right, y0 + ht - sh, ht, ht);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(fb); DeleteObject(fp);

    wchar_t buf[128];
    GetWindowTextW(h, buf, 128);
    HFONT f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
    LOGFONTW lf{};
    GetObjectW(f, sizeof(lf), &lf);
    lf.lfWeight = FW_BOLD;
    HFONT bf = CreateFontIndirectW(&lf);
    HGDIOBJ of = SelectObject(dc, bf);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    RECT tr{r.left + 2, y0, r.right - 2, y0 + ht - sh};
    DrawTextW(dc, buf, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, of);
    DeleteObject(bf);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_hwnd = hwnd;
            loadCfg();
            g_uiFont = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Malgun Gothic");
            g_logFont = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Malgun Gothic");
            g_tabFont = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Malgun Gothic");
            g_whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
            g_winBrush = CreateSolidBrush(COL_WIN);
            for (int i = 0; i < V_COUNT; i++) {   // 게임 탭
                hGameBtn[i] = mk(L"BUTTON", L"", BS_OWNERDRAW, CX + i * g_btnStep, 8, g_btnW, 34, ID_GAME0 + i);
                ShowWindow(hGameBtn[i], SW_HIDE);
            }
            // 게임 화면 아래 상태줄
            hStrip = mk(L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS, 14, 536, 520, 28);
            hStripBtn = mk(L"BUTTON", L"대결 나가기", BS_OWNERDRAW, 540, 535, 110, 30, ID_ENDMATCH);
            ShowWindow(hStripBtn, SW_HIDE);
            hCopyBtn = mk(L"BUTTON", L"코드 복사", BS_OWNERDRAW, 540, 535, 110, 30, ID_COPY);
            ShowWindow(hCopyBtn, SW_HIDE);
            // 채팅 칸 위: 메인 화면으로 / 꾸미기 / 전체 화면
            hHomeBtn = mk(L"BUTTON", L"◀ 메인", BS_OWNERDRAW, 660, 8, 104, 32, ID_HOME);
            hCustomBtn = mk(L"BUTTON", L"꾸미기", BS_OWNERDRAW, 770, 8, 104, 32, ID_CUSTOM);
            hFullBtn = mk(L"BUTTON", L"전체 화면", BS_OWNERDRAW, 880, 8, 110, 32, ID_FULL);
            ShowWindow(hHomeBtn, SW_HIDE);
            LoadLibraryW(L"Msftedit.dll");   // RichEdit 4.1 (MSFTEDIT_CLASS)
            hLog = mk(MSFTEDIT_CLASS, L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 660, 46, 330, 476, 0,
                      WS_EX_CLIENTEDGE);
            g_ctl[hLog].font = 1;
            placeCtl(hLog);
            SendMessageW(hLog, EM_EXLIMITTEXT, 0, 4 * 1024 * 1024);
            SendMessageW(hLog, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
            SendMessageW(hLog, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(6), S(6)));
            hInput = mk(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 660, 530, 262, 34, ID_INPUT, WS_EX_CLIENTEDGE);
            hSend = mk(L"BUTTON", L"보내기", BS_OWNERDRAW, 926, 530, 64, 34, ID_SEND);
            EnableWindow(hInput, FALSE);
            EnableWindow(hSend, FALSE);
            SetWindowSubclass(hInput, InputProc, 1, 0);
            SendMessageW(hInput, EM_SETCUEBANNER, TRUE, (LPARAM)L"메시지 입력 후 Enter");

            int ox = CX, oy = CY;
            hLblName = mk(L"STATIC", L"닉네임", SS_CENTERIMAGE, ox + 150, oy + 120, 70, 30);
            hName = mk(L"EDIT", g_savedName.empty() ? L"플레이어" : g_savedName.c_str(), ES_AUTOHSCROLL | WS_TABSTOP, ox + 225, oy + 120, 265, 30, 0,
                       WS_EX_CLIENTEDGE);
            SendMessageW(hName, EM_SETLIMITTEXT, 12, 0);
            hOnline = mk(L"BUTTON", L"방 만들기 (2~8명)", BS_OWNERDRAW | WS_TABSTOP, ox + 150, oy + 162, 340, 42, ID_ONLINE);
            hLblCode = mk(L"STATIC", L"방 코드", SS_CENTERIMAGE, ox + 150, oy + 215, 70, 32);
            hCode = mk(L"EDIT", L"", ES_AUTOHSCROLL | ES_UPPERCASE | WS_TABSTOP, ox + 225, oy + 215, 140, 32, 0, WS_EX_CLIENTEDGE);
            SendMessageW(hCode, EM_SETLIMITTEXT, 8, 0);
            SendMessageW(hCode, EM_SETCUEBANNER, TRUE, (LPARAM)L"예) AK7F3Q");
            hCodeJoin = mk(L"BUTTON", L"코드로 참가", BS_OWNERDRAW | WS_TABSTOP, ox + 372, oy + 214, 118, 34, ID_CODEJOIN);
            hLblIp = mk(L"STATIC", L"방장 IP", SS_CENTERIMAGE, ox + 150, oy + 286, 70, 30);
            hIp = mk(L"EDIT", L"127.0.0.1", ES_AUTOHSCROLL | WS_TABSTOP, ox + 225, oy + 286, 170, 30, 0, WS_EX_CLIENTEDGE);
            hPort = mk(L"EDIT", L"5555", ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP, ox + 400, oy + 286, 90, 30, 0, WS_EX_CLIENTEDGE);
            hHost = mk(L"BUTTON", L"IP로 방 만들기", BS_OWNERDRAW | WS_TABSTOP, ox + 150, oy + 324, 165, 34, ID_HOST);
            hJoin = mk(L"BUTTON", L"IP로 참가", BS_OWNERDRAW | WS_TABSTOP, ox + 325, oy + 324, 165, 34, ID_JOIN);
            hStatus = mk(L"STATIC", L"방을 만들어 친구들에게 코드를 보내거나,\n친구에게 받은 코드를 입력하세요.", SS_CENTER, ox + 140, oy + 368, 360, 100);
            SetWindowSubclass(hIp, ConnEditProc, 2, ID_JOIN);
            SetWindowSubclass(hCode, ConnEditProc, 3, ID_CODEJOIN);
            SetTimer(hwnd, TIMER_FRAME, 10, nullptr);
            return 0;
        }
        case WM_DRAWITEM:
            if (((DRAWITEMSTRUCT*)lp)->CtlType == ODT_BUTTON) { drawOwnerBtn((DRAWITEMSTRUCT*)lp); return TRUE; }
            break;
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
                if (M.on && MessageBoxW(hwnd, L"지금 하는 대결에서 나갈까요?", L"대결 나가기", MB_YESNO | MB_ICONQUESTION) == IDYES)
                    leaveMatch(L"대결에서 나왔습니다. 다른 게임에 사람들을 초대할 수 있어요.");
                SetFocus(hwnd);
            } else if (id == ID_COPY) {
                doCopy();
                SetFocus(hwnd);
            } else if (id == ID_CUSTOM) {
                setCustom(!g_custom);
                SetFocus(hwnd);
            } else if (id == ID_FULL) {
                setFullscreen(!g_full);
                SetFocus(hwnd);
            } else if (id == ID_HOME) {
                if (g_connected && !g_ended) {
                    if (MessageBoxW(hwnd, g_isHost ? L"내가 방장이라 나가면 방이 닫힙니다.\n방을 닫고 메인 화면으로 돌아갈까요?" : L"방에서 나가고 메인 화면으로 돌아갈까요?",
                                    L"메인 화면으로", MB_YESNO | MB_ICONQUESTION) == IDYES)
                        leaveRoom();
                } else if (g_connected) {   // 이미 연결이 끊긴 방
                    goHome(L"메인 화면으로 돌아왔습니다.\n새 방을 만들거나 코드로 참가하세요.");
                } else if (g_busy) {        // 접속 중
                    if (g_relay && g_sock != INVALID_SOCKET) rawSend(std::string("\xE0\x00", 2));
                    goHome(L"취소했습니다.\n다시 방을 만들거나 코드로 참가하세요.");
                }
                SetFocus(hwnd);
            } else if (id == ID_SEND) {
                sendChat();
                SetFocus(hInput);
            } else if ((id == ID_HOST || id == ID_JOIN || id == ID_ONLINE || id == ID_CODEJOIN) && !g_busy && !g_connected) {
                g_myName = trimW(safeName(getText(hName)));
                if (g_myName.empty()) g_myName = L"플레이어";
                if (g_myName.size() > 12) g_myName.resize(12);
                saveCfg();
                int port = _wtoi(getText(hPort).c_str());
                if (port <= 0 || port > 65535) port = 5555;
                if (id == ID_ONLINE) startGroupHost();
                else if (id == ID_CODEJOIN) startOnlineJoin();
                else if (id == ID_HOST) startHost(port);
                else {
                    std::wstring ip = trimW(getText(hIp));
                    startJoin(ip.empty() ? L"127.0.0.1" : ip, port);
                }
            }
            return 0;
        }
        case WM_CONNRESULT:   // IP로 참가: 접속 결과
            if (wp && g_sock != INVALID_SOCKET) {
                BOOL nd = TRUE;
                setsockopt(g_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nd, sizeof(nd));
                std::thread(recvLoop, g_sock, (unsigned)g_gen, false).detach();
                g_joinTries = 0;
                sendJoinReq();
                SetTimer(hwnd, TIMER_JOIN, 700, nullptr);
            } else {
                g_busy = false;
                setConnUiEnabled(true);
                SetWindowTextW(hStatus, L"접속 실패!\nIP/포트를 확인하고, 방장이 'IP로 방 만들기'를 눌렀는지 확인하세요.");
            }
            return 0;
        case WM_NET: {
            std::string* line = (std::string*)lp;
            if (wp == g_gen) {   // 예전 연결의 메시지는 무시
                if (g_tcpHost) {   // IP 방장: 받은 메시지를 다른 참가자들에게도 전달
                    std::lock_guard<std::mutex> lk(g_sendMx);
                    for (SOCKET c : g_clients) sendAll(c, *line + "\n");
                }
                handleNet(*line);
            }
            delete line;
            return 0;
        }
        case WM_RELAYREADY:
            onRelayReady(wp != 0, (int)lp);
            return 0;
        case WM_NETCLOSED:
            if (wp != g_gen) return 0;
            if (g_connected) onDisconnected(g_relay ? L"중계 서버와의 연결이 끊겼습니다." : L"방장과의 연결이 끊겼습니다.");
            else if (g_busy) relayFail(g_relay ? L"중계 서버와의 연결이 끊겼어요. 다시 시도하세요." : L"방장과의 연결이 끊겼어요. 다시 시도하세요.");
            return 0;
        case WM_TIMER:
            if (wp == TIMER_FRAME) {
                static int frame = 0;
                static auto last = std::chrono::steady_clock::now();
                auto nowT = std::chrono::steady_clock::now();
                double dt = std::min(0.25, std::chrono::duration<double>(nowT - last).count());
                last = nowT;
                frame++;
                bool wantHome = g_connected || g_busy;   // 메인 화면 버튼은 방/접속 중일 때만
                if (wantHome != (IsWindowVisible(hHomeBtn) != FALSE)) ShowWindow(hHomeBtn, wantHome ? SW_SHOW : SW_HIDE);
                if (g_relay && (g_connected || g_busy)) {
                    resendTick();
                    rxTick();
                }
                // 마우스 조준은 페인트볼을 직접 조작하는 동안에만 (채팅 · 다른 탭 · 다른 창 · 게임 끝이면 풀림)
                if (g_mouseLook && (!g_connected || g_ended || !M.on || M.view != V_FPS || g_view != V_FPS || !PB.on || PB.over || g_custom || !gameInput()))
                    pbMouse(false);
                if (g_connected && !g_ended) {
                    gameTick(dt);
                    roomTick();
                    if (gameRealtime() && g_view == M.view && !g_custom) invalidateCanvas();
                    else if (frame % 6 == 0 && (K.on || INV.on || INVIN.on)) invalidateCanvas();   // 남은 시간 막대 등
                }
            } else if (wp == TIMER_COPY) {   // "복사 완료!" → 원래 글자로
                KillTimer(hwnd, TIMER_COPY);
                SetWindowTextW(hCopyBtn, copyLabel().c_str());
                InvalidateRect(hCopyBtn, nullptr, TRUE);
            } else if (wp == TIMER_PING && g_relay && g_sock != INVALID_SOCKET) {
                rawSend(std::string("\xC0\x00", 2));   // MQTT PINGREQ (연결 유지)
            } else if (wp == TIMER_JOIN) {
                if (g_connected) KillTimer(hwnd, TIMER_JOIN);
                else if (++g_joinTries > 15)
                    relayFail(g_relay ? L"방 " + W(g_code) + L" 를 찾을 수 없어요.\n코드가 맞는지, 친구가 방을 만들어 둔 상태인지 확인하세요."
                                      : std::wstring(L"방장이 응답하지 않아요.\n방장이 같은 버전으로 'IP로 방 만들기'를 했는지 확인하세요."));
                else sendJoinReq();
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
            if (M.on && F.on && !(lp & 0x40000000)) {   // 격투: 새로 누른 키를 기록 (자동 반복 제외)
                if (wp == 'J' || wp == 'Z') F.qP = true;
                if (wp == 'K' || wp == 'X') F.qK = true;
                if (wp == 'W' || wp == VK_UP) F.qW = true;
            }
            if (M.on && PB.on && (wp == VK_SPACE || wp == 'J')) PB.qShoot = true;   // 페인트볼: 짧게 눌러도 발사
            if (wp == VK_RETURN && g_connected) SetFocus(hInput);
            if (wp == VK_ESCAPE && g_mouseLook) pbMouse(false);
            else if (wp == VK_ESCAPE && g_custom) setCustom(false);
            else if (wp == VK_ESCAPE && g_full) setFullscreen(false);
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
            pbMouse(false);
            sayBye();
            saveCfg();
            g_gen++;
            {
                std::lock_guard<std::mutex> lk(g_sendMx);
                for (SOCKET c : g_clients) { shutdown(c, SD_BOTH); closesocket(c); }
                g_clients.clear();
            }
            if (g_sock != INVALID_SOCKET) {
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
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"미니게임 파티", style, CW_USEDEFAULT, CW_USEDEFAULT,
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
