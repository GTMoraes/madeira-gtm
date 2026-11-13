// GTMTouchSA.asi - controles de toque contextuais para GTA San Andreas 1.0 US no Madeira
// (substitui o GTMSteerSA.asi: inclui a direcao por setas)
//
// 1) Contexto: a cada quadro escreve em C:\gtmtouch.bin em que situacao o CJ esta
//    (a pe, carro, moto, helicoptero, aviao, barco, passageiro). O overlay do
//    Madeira (GTMTouch.dylib) le isso e troca os botoes sozinho.
// 2) Toques: le do mesmo arquivo o estado dos botoes e analogicos do overlay e
//    mistura no controle que o GInput le (XInputGetState), como se fosse um pad.
// 3) Direcao: dentro de veiculo, D-pad esquerda/direita vira esterco total com rampa.
//
// Caminho principal: intercepta XInputGetState / XInputGetStateEx (ordinal 100).
// Reserva: se nenhuma chamada XInput aparecer, escreve direto em CPad::NewState
// logo depois de CTheScripts::Process (CALL em 0x53C077).
//
// GTMTouchSA.ini (opcional, mesma pasta):
//   [Steer]  Enabled=1  RampMs=150  ReleaseMs=60
//   [Touch]  Enabled=1
//   [Debug]  Log=1

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "MinHook.h"

// ---------------------------------------------------------------- configuracao e log
static int  g_steerOn = 1, g_rampMs = 150, g_releaseMs = 60, g_touchOn = 1, g_log = 1;
static char g_dir[MAX_PATH];

static void Log(const char* fmt, ...) {
    if (!g_log) return;
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%sGTMTouchSA.log", g_dir);
    FILE* f = fopen(path, "a");
    if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
    fputc('\n', f);
    fclose(f);
}

// ---------------------------------------------------------------- estado do jogo (1.0 US)
static bool IsSupportedExe() {
    DWORD v = *(DWORD*)0x401000;
    return v == 0x53EC8B55 /* 1.0 US compact */ || v == 0x16197BE9 /* 1.0 US hoodlum */;
}
static bool MenuActive() { return *(volatile bool*)(0xBA6748 + 0x5C); }   // FrontEndMenuManager.m_bMenuActive

static char* PlayerPed() {
    unsigned char focus = *(volatile unsigned char*)0xB7CD74;                 // CWorld::PlayerInFocus
    if (focus > 1) focus = 0;
    char* info = (char*)0xB7CD98 + focus * 0x190;                             // CWorld::Players[focus]
    return *(char* volatile*)info;                                            // m_pPed
}
static char* PlayerVehicle() {
    char* ped = PlayerPed();
    if (!ped || !(ped[0x46D] & 1)) return nullptr;                            // bInVehicle
    return *(char* volatile*)(ped + 0x58C);                                   // m_pVehicle
}

enum Context { CTX_NONE = 0, CTX_FOOT, CTX_CAR, CTX_BIKE, CTX_HELI, CTX_PLANE, CTX_BOAT, CTX_PASSENGER };

static int CurrentContext() {
    if (MenuActive()) return CTX_NONE;
    char* ped = PlayerPed();
    if (!ped) return CTX_NONE;
    char* veh = PlayerVehicle();
    if (!veh) return CTX_FOOT;
    if (*(char* volatile*)(veh + 0x460) != ped) return CTX_PASSENGER;        // m_pDriver
    switch (*(volatile int*)(veh + 0x590)) {                                  // m_nVehicleClass
        case 9: case 10:  return CTX_BIKE;                                    // BIKE, BMX
        case 3: case 7:   return CTX_HELI;                                    // HELI, FHELI
        case 4: case 8:   return CTX_PLANE;                                   // PLANE, FPLANE
        case 5:           return CTX_BOAT;
        default:          return CTX_CAR;                                     // carro, monster truck, quadriciclo, trem
    }
}
static bool SteeringContext() {
    int c = CurrentContext();
    return c == CTX_CAR || c == CTX_BIKE || c == CTX_HELI || c == CTX_PLANE || c == CTX_BOAT;
}

// ---------------------------------------------------------------- arquivo compartilhado
// 64 bytes. [0..31] escrito pelo jogo, [32..63] escrito pelo overlay.
#pragma pack(push, 1)
struct GameBlock { DWORD magic; WORD version; WORD flags; DWORD seq; DWORD context; BYTE pad[16]; };
struct HostBlock { DWORD magic; DWORD seq; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; BYTE pad[12]; };
#pragma pack(pop)
static_assert(sizeof(GameBlock) == 32 && sizeof(HostBlock) == 32, "layout");
static const DWORD GAME_MAGIC = 0x544D5447;   // "GTMT"
static const DWORD HOST_MAGIC = 0x484D5447;   // "GTMH"

static HANDLE g_file = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_io;
static DWORD g_gameSeq = 0;
static int   g_lastCtx = -1;

static HostBlock g_host = {};
static DWORD g_hostSeqSeen = 0, g_hostSeqTick = 0;
static bool  g_hostEverSeen = false;

static bool OpenShared() {
    char root[MAX_PATH] = "C:\\gtmtouch.bin";
    g_file = CreateFileA(root, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_file == INVALID_HANDLE_VALUE) { Log("nao abriu %s (erro %lu)", root, GetLastError()); return false; }
    LARGE_INTEGER sz; GetFileSizeEx(g_file, &sz);
    if (sz.QuadPart < 64) {
        BYTE zero[64] = {};
        OVERLAPPED o = {}; DWORD n;
        WriteFile(g_file, zero, 64, &n, &o);
    }
    Log("arquivo compartilhado aberto: %s", root);
    return true;
}

static void WriteGame(int ctx, WORD flags) {
    if (g_file == INVALID_HANDLE_VALUE) return;
    GameBlock g = {};
    g.magic = GAME_MAGIC; g.version = 1; g.flags = flags; g.seq = ++g_gameSeq; g.context = (DWORD)ctx;
    EnterCriticalSection(&g_io);
    OVERLAPPED o = {}; DWORD n;
    WriteFile(g_file, &g, sizeof g, &n, &o);
    LeaveCriticalSection(&g_io);
}

// le o bloco do overlay; devolve true se ele esta vivo (mudou nos ultimos 300 ms)
static bool ReadHost(HostBlock* out) {
    if (!g_touchOn || g_file == INVALID_HANDLE_VALUE) return false;
    HostBlock h = {};
    EnterCriticalSection(&g_io);
    OVERLAPPED o = {}; o.Offset = 32; DWORD n = 0;
    BOOL ok = ReadFile(g_file, &h, sizeof h, &n, &o);
    LeaveCriticalSection(&g_io);
    DWORD now = GetTickCount();
    if (ok && n == sizeof h && h.magic == HOST_MAGIC) {
        if (h.seq != g_hostSeqSeen) {
            g_hostSeqSeen = h.seq; g_hostSeqTick = now;
            if (!g_hostEverSeen) { g_hostEverSeen = true; Log("overlay de toque detectado"); }
        }
        g_host = h;
    }
    bool alive = g_hostEverSeen && (now - g_hostSeqTick) < 300;
    if (alive) *out = g_host;
    return alive;
}

// ---------------------------------------------------------------- rampa da direcao
static double g_steer = 0.0;
static LARGE_INTEGER g_qpf, g_last;

static double Advance(int dir) {
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    double ms = g_last.QuadPart ? (now.QuadPart - g_last.QuadPart) * 1000.0 / g_qpf.QuadPart : 0.0;
    g_last = now;
    if (ms > 100.0) ms = 100.0;
    double target = (double)dir;
    double span = (dir != 0 && (g_steer * target >= 0.0)) ? g_rampMs : g_releaseMs;
    double step = span > 0 ? ms / span : 2.0;
    if (g_steer < target) { g_steer += step; if (g_steer > target) g_steer = target; }
    else if (g_steer > target) { g_steer -= step; if (g_steer < target) g_steer = target; }
    return g_steer;
}

// ---------------------------------------------------------------- caminho XInput
typedef struct { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } PadState;
typedef struct { DWORD dwPacketNumber; PadState Gamepad; } XState;
typedef DWORD (WINAPI* XGetState_t)(DWORD, XState*);

static const WORD DPAD_LEFT = 0x0004, DPAD_RIGHT = 0x0008;
static volatile LONG g_xinputCalls = 0;
static volatile DWORD g_lastXInputTick = 0;
static DWORD g_packetBump = 0;
static PadState g_lastOut = {};

static SHORT Stronger(SHORT a, SHORT b) { return (abs((int)b) > abs((int)a)) ? b : a; }

static DWORD Filter(DWORD ret, DWORD idx, XState* st) {
    if (idx != 0 || !st) return ret;
    InterlockedIncrement(&g_xinputCalls);
    g_lastXInputTick = GetTickCount();

    // 1) mistura os toques do overlay
    HostBlock h;
    if (ReadHost(&h)) {
        if (ret != ERROR_SUCCESS) { memset(st, 0, sizeof *st); ret = ERROR_SUCCESS; }
        PadState& p = st->Gamepad;
        p.wButtons |= h.buttons;
        if (h.lt > p.bLeftTrigger) p.bLeftTrigger = h.lt;
        if (h.rt > p.bRightTrigger) p.bRightTrigger = h.rt;
        p.sThumbLX = Stronger(p.sThumbLX, h.lx); p.sThumbLY = Stronger(p.sThumbLY, h.ly);
        p.sThumbRX = Stronger(p.sThumbRX, h.rx); p.sThumbRY = Stronger(p.sThumbRY, h.ry);
    }
    if (ret != ERROR_SUCCESS) return ret;

    // 2) setas viram direcao dentro de veiculo
    if (g_steerOn) {
        if (!SteeringContext()) { g_steer = 0.0; g_last.QuadPart = 0; }
        else {
            WORD b = st->Gamepad.wButtons;
            int dir = ((b & DPAD_RIGHT) ? 1 : 0) - ((b & DPAD_LEFT) ? 1 : 0);
            double s = Advance(dir);
            if (dir != 0 || s != 0.0) {
                st->Gamepad.wButtons = b & ~(DPAD_LEFT | DPAD_RIGHT);
                st->Gamepad.sThumbLX = (SHORT)(s * 32767.0);
            }
        }
    }

    // 3) o leitor so processa pacotes novos: numera quando o que entregamos mudou
    if (memcmp(&g_lastOut, &st->Gamepad, sizeof(PadState)) != 0) { g_packetBump++; g_lastOut = st->Gamepad; }
    st->dwPacketNumber += g_packetBump;
    return ret;
}

#define DETOUR(n) static DWORD WINAPI Det##n(DWORD i, XState* s) { return Filter(g_hooks[n].orig(i, s), i, s); }
struct Hooked { XGetState_t orig; };
static Hooked g_hooks[12];
static int g_hookCount = 0;
DETOUR(0) DETOUR(1) DETOUR(2) DETOUR(3) DETOUR(4) DETOUR(5)
DETOUR(6) DETOUR(7) DETOUR(8) DETOUR(9) DETOUR(10) DETOUR(11)
static XGetState_t g_detours[12] = { Det0, Det1, Det2, Det3, Det4, Det5, Det6, Det7, Det8, Det9, Det10, Det11 };

static void HookTarget(HMODULE m, const char* modName, LPCSTR proc, const char* label) {
    if (g_hookCount >= 12) return;
    void* p = (void*)GetProcAddress(m, proc);
    if (!p) return;
    int n = g_hookCount;
    void* orig = nullptr;
    MH_STATUS s = MH_CreateHook(p, (void*)g_detours[n], &orig);
    if (s == MH_ERROR_ALREADY_CREATED) return;
    if (s != MH_OK) { Log("falha ao preparar %s!%s (%d)", modName, label, (int)s); return; }
    g_hooks[n].orig = (XGetState_t)orig;
    if (MH_EnableHook(p) != MH_OK) { Log("falha ao ativar %s!%s", modName, label); return; }
    g_hookCount++;
    Log("interceptado %s!%s em %p", modName, label, p);
}

static void InstallXInputHooks() {
    const char* mods[] = { "xinput1_3.dll", "xinput1_4.dll", "xinput9_1_0.dll", "xinput1_2.dll", "xinput1_1.dll" };
    for (const char* name : mods) {
        HMODULE m = GetModuleHandleA(name);
        if (!m) m = LoadLibraryA(name);
        if (!m) continue;
        HookTarget(m, name, "XInputGetState", "XInputGetState");
        HookTarget(m, name, (LPCSTR)100, "XInputGetStateEx(100)");
    }
}

// ---------------------------------------------------------------- reserva: CPad::NewState
static void ApplyToNewState(short* ns) {
    HostBlock h;
    if (ReadHost(&h)) {
        struct { WORD bit; int off; } map[] = {
            {0x0001, 0x10}, {0x0002, 0x12}, {0x0004, 0x14}, {0x0008, 0x16}, {0x0010, 0x18}, {0x0020, 0x1A},
            {0x4000, 0x1C}, {0x8000, 0x1E}, {0x1000, 0x20}, {0x2000, 0x22}, {0x0040, 0x24}, {0x0080, 0x26},
            {0x0100, 0x08}, {0x0200, 0x0C} };
        for (auto& m : map) if (h.buttons & m.bit) ns[m.off / 2] = 255;
        if (h.lt) ns[0x0A / 2] = h.lt;
        if (h.rt) ns[0x0E / 2] = h.rt;
        auto st = [](SHORT v) { return (short)(v / 256); };
        if (h.lx) ns[0] = st(h.lx);
        if (h.ly) ns[1] = (short)-st(h.ly);           // CPad: para baixo e positivo
        if (h.rx) ns[2] = st(h.rx);
        if (h.ry) ns[3] = (short)-st(h.ry);
    }
    if (!g_steerOn) return;
    if (!SteeringContext()) { g_steer = 0.0; g_last.QuadPart = 0; return; }
    int dir = (ns[0x16 / 2] ? 1 : 0) - (ns[0x14 / 2] ? 1 : 0);
    double s = Advance(dir);
    if (dir != 0 || s != 0.0) { ns[0x14 / 2] = 0; ns[0x16 / 2] = 0; ns[0] = (short)(s * 128.0); }
}

// ---------------------------------------------------------------- gancho por quadro
typedef void (__cdecl* Void_t)();
static Void_t g_origScripts = nullptr;
static const DWORD kScriptsCall = 0x53C077;            // CALL CTheScripts::Process em CGame::Process
static bool g_firstFrame = true, g_fallbackNoted = false;

static void __cdecl AfterScripts() {
    if (g_origScripts) g_origScripts();
    if (g_firstFrame) {                                 // fora do DllMain: sem LoadLibrary sob o loader lock
        g_firstFrame = false;
        if (MH_Initialize() != MH_OK) Log("MinHook nao iniciou");
        else InstallXInputHooks();
        if (g_hookCount == 0) Log("nenhuma funcao XInput interceptada");
    }
    int ctx = CurrentContext();
    if (ctx != g_lastCtx) { Log("contexto: %d", ctx); g_lastCtx = ctx; }
    bool xinputLive = g_xinputCalls > 0 && GetTickCount() - g_lastXInputTick < 1000;
    WriteGame(ctx, (WORD)(xinputLive ? 1 : 0));

    if (xinputLive) return;
    if (!g_fallbackNoted) { g_fallbackNoted = true; Log("usando o caminho reserva (CPad::NewState)"); }
    short* ns = (short*)(*(char* (__cdecl*)(int))0x53FB70)(0);   // CPad::GetPad(0)
    if (ns) ApplyToNewState(ns);
}

static void InstallFrameHook() {
    BYTE* site = (BYTE*)kScriptsCall;
    if (*site != 0xE8) { Log("gancho por quadro desativado: 0x%X nao e CALL (0x%02X)", kScriptsCall, *site); return; }
    DWORD target = kScriptsCall + 5 + *(int*)(site + 1);
    g_origScripts = (Void_t)target;
    DWORD old;
    VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
    *(int*)(site + 1) = (int)((DWORD)&AfterScripts - (kScriptsCall + 5));
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    Log("gancho por quadro em 0x%X (destino original 0x%X)", kScriptsCall, target);
}

// ---------------------------------------------------------------- entrada
static void LoadConfig(HMODULE self) {
    char path[MAX_PATH];
    GetModuleFileNameA(self, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) slash[1] = 0; else path[0] = 0;
    strcpy(g_dir, path);
    char ini[MAX_PATH];
    snprintf(ini, sizeof ini, "%sGTMTouchSA.ini", g_dir);
    g_steerOn   = GetPrivateProfileIntA("Steer", "Enabled",   1,   ini);
    g_rampMs    = GetPrivateProfileIntA("Steer", "RampMs",    150, ini);
    g_releaseMs = GetPrivateProfileIntA("Steer", "ReleaseMs", 60,  ini);
    g_touchOn   = GetPrivateProfileIntA("Touch", "Enabled",   1,   ini);
    g_log       = GetPrivateProfileIntA("Debug", "Log",       1,   ini);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    LoadConfig(inst);
    QueryPerformanceFrequency(&g_qpf);
    InitializeCriticalSection(&g_io);
    Log("GTMTouchSA 1.0 carregado (direcao=%d rampa=%d retorno=%d toque=%d)", g_steerOn, g_rampMs, g_releaseMs, g_touchOn);
    if (GetModuleHandleA("GTMSteerSA.asi")) Log("AVISO: GTMSteerSA.asi tambem esta carregado; apague ele da pasta scripts");
    if (!IsSupportedExe()) { Log("executavel nao e o 1.0 US: nada foi alterado"); return TRUE; }
    OpenShared();
    InstallFrameHook();
    return TRUE;
}
