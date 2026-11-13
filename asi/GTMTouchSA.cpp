// GTMTouchSA.asi 3.0 - controles de toque contextuais para GTA San Andreas 1.0 US no Madeira
//
// 1) Contexto: a cada quadro escreve em C:\gtmtouch.bin em que situacao o CJ esta (a pe,
//    nadando, de jetpack, carro, moto, helicoptero, aviao, barco, passageiro, cutscene,
//    menu de pausa) e o que esta acontecendo: pergunta Sim/Nao pendente, arma na mao,
//    satchel/detonador, veiculo perto (e de que tipo), nitro, hidraulica, veiculo de
//    submissao, armas do veiculo, grupo. O overlay (GTMTouch.dylib) mostra so o que serve.
// 2) Toques: le do mesmo arquivo os botoes e analogicos do overlay e mistura no controle
//    que o GInput le (XInputGetState), como se fosse um pad. Sim/Nao vem num campo
//    separado para nao virar direcao dentro do carro.
// 3) Direcao: dentro de veiculo, D-pad esquerda/direita vira esterco total com rampa.
// 4) Salvar: na hora (botao), autosave a cada N minutos e ao passar missao, sempre no
//    mesmo slot. Se salvou dentro de um veiculo, recria o veiculo ao carregar.
// 5) Comandos do overlay: 1 salvar, 2 detonar as satchels, 3 voltar para a satchel.
//
// Interceptacoes: XInputGetState (instalada logo na carga, para valer no menu tambem),
// CALL em 0x53C077 dentro de CGame::Process (por quadro: CCranes::UpdateCranes, depois dos
// scripts e das armas, antes do CWorld::Process), 0x618E90 (depois de carregar),
// 0x485B10 CRunningScript::GetPadState e 0x588BE0/0x588E30 CHud::SetHelpMessage* (para
// saber quando o jogo espera Sim/Nao).
//
// GTMTouchSA.ini (opcional, mesma pasta):
//   [Steer]    Enabled=1  RampMs=150  ReleaseMs=60
//   [Touch]    Enabled=1
//   [Save]     Slot=8  AutosaveMinutes=5  AfterMission=1  KeepVehicle=1
//   [YesNo]    Scripts=1  IgnoreScripts=
//   [Vehicles] MissionModels=...  FireModels=...  Fire2Models=...
//   [Debug]    Log=1

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include "MinHook.h"

// ---------------------------------------------------------------- configuracao e log
static int  g_steerOn = 1, g_rampMs = 150, g_releaseMs = 60, g_touchOn = 1, g_log = 1;
static int  g_saveSlot = 8, g_autosaveMin = 5, g_afterMission = 1, g_keepVehicle = 1;
static int  g_ynScripts = 1;
static char g_ignoreScripts[512];                       // minusculas, ",nome,nome,"
static char g_dir[MAX_PATH];

static short g_missionModels[64], g_fireModels[64], g_fire2Models[32];
static int   g_nMission = 0, g_nFire = 0, g_nFire2 = 0;

// taxi, cabbie, policia LS/SF/LV, ranger, moto da policia, enforcer, FBI rancher, FBI truck,
// SWAT, rhino, hunter, seasparrow, police maverick, predator, ambulancia, bombeiros,
// broadway (cafetao), trens freight/brown streak, boxburg (roubo)
static const char* kDefMission = "420,438,596,597,598,599,523,427,490,528,601,432,425,447,497,430,416,407,544,575,537,538,609";
// atiram com B: rhino, RC tiger, RC bandit, bombeiros, SWAT, predator, hunter (foguetes), hydra (flares)
static const char* kDefFire    = "432,564,441,407,601,430,425,520";
// atiram com LB: hunter (metralhadora), seasparrow, RC baron, rustler, hydra (misseis)
static const char* kDefFire2   = "425,447,464,476,520";

static CRITICAL_SECTION g_logCs;
static void Log(const char* fmt, ...) {
    if (!g_log) return;
    EnterCriticalSection(&g_logCs);
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%sGTMTouchSA.log", g_dir);
    FILE* f = fopen(path, "a");
    if (f) {
        SYSTEMTIME t; GetLocalTime(&t);
        fprintf(f, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
        fputc('\n', f);
        fclose(f);
    }
    LeaveCriticalSection(&g_logCs);
}

static int ParseList(const char* s, short* out, int max) {
    int n = 0;
    while (*s && n < max) {
        while (*s && !isdigit((unsigned char)*s)) s++;
        if (!*s) break;
        out[n++] = (short)strtol(s, (char**)&s, 10);
    }
    return n;
}
static bool InList(const short* list, int n, int v) {
    for (int i = 0; i < n; i++) if (list[i] == v) return true;
    return false;
}

// ---------------------------------------------------------------- memoria do jogo (1.0 US)
template <class T> static inline T Rd(const void* base, int off) { return *(volatile T*)((const char*)base + off); }

static bool IsSupportedExe() {
    DWORD v = *(DWORD*)0x401000;
    return v == 0x53EC8B55 /* 1.0 US compact */ || v == 0x16197BE9 /* 1.0 US hoodlum */;
}
static bool MenuActive() { return *(volatile bool*)(0xBA6748 + 0x5C); }   // FrontEndMenuManager.m_bMenuActive

static char* PlayerInfo() {
    unsigned char focus = *(volatile unsigned char*)0xB7CD74;                 // CWorld::PlayerInFocus
    if (focus > 1) focus = 0;
    return (char*)0xB7CD98 + focus * 0x190;                                   // CWorld::Players[focus]
}
static char* PlayerPed() { return Rd<char*>(PlayerInfo(), 0); }              // m_pPed
static char* PlayerVehicle() {
    char* ped = PlayerPed();
    if (!ped || !(Rd<BYTE>(ped, 0x46D) & 1)) return nullptr;                  // bInVehicle
    return Rd<char*>(ped, 0x58C);                                             // m_pVehicle
}
struct V3 { float x, y, z; };
static V3 EntityPos(const char* e) {
    const float* m = Rd<const float*>(e, 0x14);                               // CMatrix*: pos em +0x30
    if (m) return { m[12], m[13], m[14] };
    return { Rd<float>(e, 4), Rd<float>(e, 8), Rd<float>(e, 12) };
}

enum Context {
    CTX_NONE = 0, CTX_FOOT, CTX_CAR, CTX_BIKE, CTX_HELI, CTX_PLANE, CTX_BOAT, CTX_PASSENGER,
    CTX_MENU, CTX_SWIM, CTX_JETPACK, CTX_CUTSCENE
};
static const char* kCtxName[] = { "nenhum", "a pe", "carro", "moto", "helicoptero", "aviao", "barco",
                                  "passageiro", "menu", "nadando", "jetpack", "cutscene" };

// o que o overlay precisa saber alem do contexto
enum State : DWORD {
    S_YESNO        = 1u << 0,   // o jogo espera Sim/Nao
    S_WEAPONS      = 1u << 1,   // tem arma para trocar
    S_AIM          = 1u << 2,   // a arma na mao mira / trava alvo
    S_DETONATE     = 1u << 3,   // tem detonador e esta com outra coisa na mao
    S_MORESATCHEL  = 1u << 4,   // esta com o detonador e ainda tem satchel
    S_VEHNEAR      = 1u << 5,   // veiculo perto (classe em nearVeh)
    S_FIRE         = 1u << 6,   // no veiculo, B faz algo (drive-by, arma, bomba)
    S_FIRE2        = 1u << 7,   // no veiculo, LB atira (aeronaves armadas)
    S_NITRO        = 1u << 8,
    S_HYDRAULICS   = 1u << 9,
    S_SUBMISSION   = 1u << 10,  // R3 inicia submissao (taxi, policia...)
    S_GROUP        = 1u << 11,  // mirando um pedestre ou com grupo: recrutar/seguir
    S_TARGETPED    = 1u << 12,
    S_ARMED        = 1u << 13,  // algo na mao (nao e soco)
    S_REMOTE       = 1u << 14,  // dirigindo um veiculo de controle remoto
    S_GROUPMEMBERS = 1u << 15,
    S_DRIVEBY      = 1u << 16,  // da para atirar da janela (olhar para o lado + B)
};

struct Info { int ctx; DWORD state; BYTE weapon, nearVeh; };
static Info g_info = {};                                // calculado no quadro (thread do jogo)
static volatile int g_ctxCache = CTX_NONE;              // copia para a thread do XInput

// ---------------------------------------------------------------- arquivo compartilhado (v3)
// 128 bytes. [0..63] escrito pelo jogo, [64..127] escrito pelo overlay.
#pragma pack(push, 1)
struct GameBlock {
    DWORD magic; WORD version; WORD flags; DWORD seq; DWORD context;
    DWORD saveSeq;      // conta cada tentativa de salvar
    LONG  saveResult;   // 0 salvo, 1 em missao, 2 agora nao da, 3 erro
    DWORD saveKind;     // 1 botao, 2 autosave por tempo, 3 autosave depois da missao
    BYTE  onMission, canSave, slot, pad0;
    DWORD autosaveIn;   // segundos ate o proximo autosave (0 = desligado)
    DWORD state;        // S_*
    BYTE  weapon;       // tipo da arma na mao (eWeaponType)
    BYTE  nearVeh;      // classe do veiculo perto + 1 (0 = nenhum)
    BYTE  pad1[2];
    BYTE  pad[20];
};
struct HostBlock {
    DWORD magic; DWORD seq; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry;
    DWORD cmdSeq; DWORD cmd;   // 1 salvar, 2 detonar, 3 satchel
    WORD  rawButtons;          // somados depois da direcao (Sim/Nao no carro)
    WORD  version;
    BYTE  pad[32];
};
#pragma pack(pop)
static_assert(sizeof(GameBlock) == 64 && sizeof(HostBlock) == 64, "layout");
static const DWORD GAME_MAGIC = 0x544D5447;   // "GTMT"
static const DWORD HOST_MAGIC = 0x484D5447;   // "GTMH"

static HANDLE g_file = INVALID_HANDLE_VALUE;
static bool g_mhReady = false;
static CRITICAL_SECTION g_io;
static DWORD g_gameSeq = 0;
static int   g_lastCtx = -1;

static HostBlock g_host = {};
static DWORD g_hostSeqSeen = 0, g_hostSeqTick = 0;
static bool  g_hostEverSeen = false;

static bool OpenShared() {
    const char* root = "C:\\gtmtouch.bin";
    g_file = CreateFileA(root, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_file == INVALID_HANDLE_VALUE) { Log("nao abriu %s (erro %lu)", root, GetLastError()); return false; }
    LARGE_INTEGER sz; GetFileSizeEx(g_file, &sz);
    if (sz.QuadPart < 128) {
        BYTE zero[128] = {};
        OVERLAPPED o = {}; DWORD n;
        WriteFile(g_file, zero, 128, &n, &o);
    }
    Log("arquivo compartilhado aberto: %s", root);
    return true;
}

static GameBlock g_saveInfo = {};   // campos de save repassados ao overlay

static void WriteGame(const Info& in, WORD flags) {
    if (g_file == INVALID_HANDLE_VALUE) return;
    EnterCriticalSection(&g_io);
    GameBlock g = g_saveInfo;
    g.magic = GAME_MAGIC; g.version = 3; g.flags = flags; g.seq = ++g_gameSeq; g.context = (DWORD)in.ctx;
    g.state = in.state; g.weapon = in.weapon; g.nearVeh = in.nearVeh;
    OVERLAPPED o = {}; DWORD n;
    WriteFile(g_file, &g, sizeof g, &n, &o);
    LeaveCriticalSection(&g_io);
}

// le o bloco do overlay; devolve true se ele esta vivo (mudou nos ultimos 300 ms)
static bool ReadHost(HostBlock* out) {
    if (!g_touchOn || g_file == INVALID_HANDLE_VALUE) return false;
    HostBlock h = {};
    EnterCriticalSection(&g_io);
    OVERLAPPED o = {}; o.Offset = 64; DWORD n = 0;
    BOOL ok = ReadFile(g_file, &h, sizeof h, &n, &o);
    DWORD now = GetTickCount();
    if (ok && n == sizeof h && h.magic == HOST_MAGIC) {
        if (h.seq != g_hostSeqSeen) {
            g_hostSeqSeen = h.seq; g_hostSeqTick = now;
            if (!g_hostEverSeen) { g_hostEverSeen = true; Log("overlay de toque detectado (protocolo %u)", (unsigned)h.version); }
        }
        g_host = h;
    }
    bool alive = g_hostEverSeen && (now - g_hostSeqTick) < 300;
    if (alive) *out = g_host;
    LeaveCriticalSection(&g_io);
    return alive;
}

// ---------------------------------------------------------------- Sim/Nao pendente
static volatile bool  g_helpYN = false, g_helpYNPerm = false;
static volatile DWORD g_helpYNTick = 0, g_scriptYNTick = 0;
static char g_scriptYNName[9] = "";
static const char* g_ynWhy = "";
static const char  kWhyScript[] = "script";

static bool HelpHasYesNo(const char* t) {
    return t && (strstr(t, "CONVERSATION_YES") || strstr(t, "CONVERSATION_NO"));
}
static void NoteHelp(const char* text, bool permanent) {
    if (HelpHasYesNo(text)) { g_helpYN = true; g_helpYNPerm = permanent; g_helpYNTick = GetTickCount(); }
    else g_helpYN = false;
}

typedef void (__cdecl* SetHelp_t)(const char*, bool, bool, bool);
typedef void (__cdecl* SetHelpNum_t)(const char*, int, bool, bool);
static SetHelp_t    g_origSetHelp = nullptr;
static SetHelpNum_t g_origSetHelpNum = nullptr;
static void __cdecl SetHelpDetour(const char* text, bool quick, bool permanent, bool brief) {
    NoteHelp(text, permanent);
    g_origSetHelp(text, quick, permanent, brief);
}
static void __cdecl SetHelpNumDetour(const char* text, int number, bool quick, bool permanent) {
    NoteHelp(text, permanent);
    g_origSetHelpNum(text, number, quick, permanent);
}

// scripts que leem as setas esquerda/direita (IS_BUTTON_PRESSED 0 DPADLEFT/DPADRIGHT)
static char g_loggedScripts[32][9];
static int  g_nLoggedScripts = 0;
static void NoteScriptYesNo(const char* script) {
    char name[9] = "";
    if (script) { memcpy(name, script + 8, 8); name[8] = 0; }               // CRunningScript::m_szName
    for (char* c = name; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (name[0]) {
        char key[12]; snprintf(key, sizeof key, ",%s,", name);
        if (strstr(g_ignoreScripts, key)) return;
    }
    g_scriptYNTick = GetTickCount();
    memcpy(g_scriptYNName, name, sizeof name);
    for (int i = 0; i < g_nLoggedScripts; i++) if (!strcmp(g_loggedScripts[i], name)) return;
    if (g_nLoggedScripts < 32) { strcpy(g_loggedScripts[g_nLoggedScripts++], name); Log("script '%s' le as setas esquerda/direita (Sim/Nao)", name); }
}

// retorna int: o EAX do original passa inteiro (o chamador pode testar o registrador todo)
typedef int (__thiscall* GetPadState_t)(void*, int, int);
static GetPadState_t g_origGetPadState = nullptr;
static int __fastcall GetPadStateDetour(void* self, void* /*edx*/, int pad, int button) {
    int r = g_origGetPadState(self, pad, button);
    int b = button & 0xFFFF;
    if (g_ynScripts && (pad & 0xFFFF) == 0 && (b == 10 || b == 11)) NoteScriptYesNo((const char*)self);
    return r;
}

static bool YesNoPending() {
    if (*(volatile DWORD*)0x969A20 == 1) { g_ynWhy = "pedestre puxou conversa"; return true; }   // CPedToPlayerConversations: PEDHASOPENED
    for (int i = 0; i < 14; i++)                                                                  // CConversations: WAITINGFORINPUT
        if (*(volatile DWORD*)(0x9691D8 + i * 0x1C + 0x14) == 3) { g_ynWhy = "conversa de missao"; return true; }
    DWORD now = GetTickCount();
    if (g_helpYN) {
        bool shown = *(volatile char*)0xBAA480 != 0;                          // CHud::m_pHelpMessageToPrint
        if (now - g_helpYNTick < 15000 || (g_helpYNPerm && shown)) { g_ynWhy = "texto de ajuda"; return true; }
    }
    if (g_ynScripts && g_scriptYNTick && now - g_scriptYNTick < 600) { g_ynWhy = kWhyScript; return true; }
    return false;
}

// ---------------------------------------------------------------- leitura do estado
typedef void* (__thiscall* GetTask_t)(void*);
static const GetTask_t GetTaskSwim    = (GetTask_t)0x601070;   // CPedIntelligence::GetTaskSwim
static const GetTask_t GetTaskJetPack = (GetTask_t)0x601110;   // CPedIntelligence::GetTaskJetPack

static const int W_STRIDE = 0x1C;                               // CWeapon: tipo +0, municao total +0xC
static int WeaponType(const char* ped, int slot) { return Rd<int>(ped, 0x5A0 + slot * W_STRIDE); }
static int WeaponAmmo(const char* ped, int slot) { return Rd<int>(ped, 0x5A0 + slot * W_STRIDE + 0xC); }

static bool IsRC(int model) { return model == 441 || model == 464 || model == 465 || model == 501 || model == 564 || model == 594; }

static int ClassToCtx(int cls) {
    switch (cls) {
        case 9: case 10: return CTX_BIKE;     // BIKE, BMX
        case 3: case 7:  return CTX_HELI;     // HELI, FHELI
        case 4: case 8:  return CTX_PLANE;    // PLANE, FPLANE
        case 5:          return CTX_BOAT;
        default:         return CTX_CAR;      // carro, monster truck, quadriciclo, trem
    }
}

static int GroupMembers(const char* ped) {
    const char* pd = Rd<const char*>(ped, 0x480);                            // CPlayerData*
    if (!pd) return 0;
    unsigned idx = Rd<unsigned>(pd, 0x38);                                    // m_nPlayerGroup
    if (idx >= 8) return 0;
    const char* members = (const char*)0xC09920 + idx * 0x2D4 + 0x8 + 0x4;    // ms_groups[idx].m_groupMembership.m_apMembers
    int n = 0;
    for (int i = 0; i < 7; i++) if (Rd<const char*>(members, i * 4)) n++;     // [7] e o lider
    return n;
}

// classe do veiculo mais perto que da para entrar, ou -1
static int NearestVehicleClass(const char* ped) {
    const char* pool = *(const char* volatile*)0xB74494;                      // CPools::ms_pVehiclePool
    if (!pool) return -1;
    const char* objs = Rd<const char*>(pool, 0);
    const BYTE* map  = Rd<const BYTE*>(pool, 4);
    int size = Rd<int>(pool, 8);
    if (!objs || !map || size <= 0 || size > 4096) return -1;
    char** mi = *(char***)0x40CD67;                                           // CModelInfo::ms_modelInfoPtrs
    V3 pp = EntityPos(ped);
    float best = 1e9f; int bestCls = -1;
    for (int i = 0; i < size; i++) {
        if (map[i] & 0x80) continue;                                          // vazio
        const char* v = objs + i * 0xA18;
        BYTE ts = Rd<BYTE>(v, 0x36);
        if ((ts & 7) != 2 || (ts >> 3) == 5) continue;                        // nao e veiculo / destruido
        int cls = Rd<int>(v, 0x590);
        if (cls < 0 || cls > 10) continue;                                    // reboques etc.
        int model = Rd<short>(v, 0x22);
        if (IsRC(model)) continue;
        V3 vp = EntityPos(v);
        if (fabsf(vp.z - pp.z) > 4.f) continue;
        float dx = vp.x - pp.x, dy = vp.y - pp.y;
        float r = 3.f;
        if (mi && model > 0 && model < 20000 && mi[model]) {
            const char* col = Rd<const char*>(mi[model], 0x14);               // CBaseModelInfo::m_pColModel
            if (col) { float rr = Rd<float>(col, 0x24); if (rr > 0.5f && rr < 80.f) r = rr; }   // m_boundSphere.radius
        }
        float gap = sqrtf(dx * dx + dy * dy) - r;
        if (gap < 2.5f && gap < best) { best = gap; bestCls = cls; }
    }
    return bestCls;
}

static void ComputeInfo(Info& in) {
    in = {};
    if (MenuActive()) { in.ctx = CTX_MENU; return; }
    char* ped = PlayerPed();
    if (!ped) return;
    if (*(volatile signed char*)0xB5F851) { in.ctx = CTX_CUTSCENE; return; }   // CCutsceneMgr::ms_running
    char* info = PlayerInfo();

    int slot = Rd<BYTE>(ped, 0x718); if (slot > 12) slot = 0;                // m_nSelectedWepSlot
    int active = WeaponType(ped, slot), activeAmmo = WeaponAmmo(ped, slot);
    in.weapon = (BYTE)((active >= 0 && active < 64) ? active : 0);
    int owned = 0;
    for (int s = 1; s <= 12; s++) {
        int t = WeaponType(ped, s);
        if (t > 0 && (WeaponAmmo(ped, s) > 0 || t <= 15 || t >= 40)) owned++;
    }
    if (owned > 0) in.state |= S_WEAPONS;
    if (active != 0) in.state |= S_ARMED;
    if (active != 40 && active != 44 && active != 45 && active != 46) in.state |= S_AIM;
    if (YesNoPending()) in.state |= S_YESNO;
    bool driveByWeapon = (active == 28 || active == 29 || active == 32) && activeAmmo > 0 && Rd<BYTE>(info, 0x153);   // m_bCanDoDriveBy

    char* remote = Rd<char*>(info, 0xB0);                                     // m_pRemoteVehicle
    char* veh = remote ? remote : PlayerVehicle();
    if (veh) {
        int cls = Rd<int>(veh, 0x590);
        bool driver = remote || Rd<char*>(veh, 0x460) == ped;                 // m_pDriver
        if (remote) in.state |= S_REMOTE;
        if (!driver) { in.ctx = CTX_PASSENGER; if (driveByWeapon) in.state |= S_FIRE | S_DRIVEBY; return; }
        in.ctx = ClassToCtx(cls);
        int model = Rd<short>(veh, 0x22);
        DWORD hf = Rd<DWORD>(veh, 0x38C);                                     // handlingFlags
        bool automobile = cls >= 0 && cls <= 2;                               // carro, monster truck, quadriciclo
        if (automobile && (hf & 0x80000) && Rd<BYTE>(veh, 0x48A) > 0) in.state |= S_NITRO;   // bNosInst, m_nNitroBoosts
        if (automobile && (hf & 0x20000)) in.state |= S_HYDRAULICS;          // bHydraulicInst
        if (InList(g_missionModels, g_nMission, model)) in.state |= S_SUBMISSION;
        bool air = cls == 3 || cls == 4 || cls == 7 || cls == 8;
        int bomb = Rd<BYTE>(veh, 0x4A8) & 7;                                  // m_nBombOnBoard: 1 timer, 2 ignicao (armar com B)
        if (!air && driveByWeapon) in.state |= S_FIRE | S_DRIVEBY;
        if (InList(g_fireModels, g_nFire, model) || bomb == 1 || bomb == 2) in.state |= S_FIRE;
        if (InList(g_fire2Models, g_nFire2, model)) in.state |= S_FIRE2;
        return;
    }

    void* intel = Rd<void*>(ped, 0x47C);                                      // CPedIntelligence*
    if (intel && GetTaskJetPack(intel)) {
        in.ctx = CTX_JETPACK;
        // no jetpack so atiram pistolas, escopeta de cano serrado e submetralhadoras
        bool jetGun = (active >= 22 && active <= 24) || active == 26 || active == 28 || active == 29 || active == 32;
        if (!(jetGun && activeAmmo > 0)) in.state &= ~(DWORD)S_ARMED;
    }
    else if (intel && GetTaskSwim(intel)) in.ctx = CTX_SWIM;
    else in.ctx = CTX_FOOT;

    bool hasDet = WeaponType(ped, 12) == 40 && WeaponAmmo(ped, 12) > 0;
    bool hasSatchel = WeaponType(ped, 8) == 39 && WeaponAmmo(ped, 8) > 0;
    if (hasDet && active != 40) in.state |= S_DETONATE;
    if (active == 40 && hasSatchel) in.state |= S_MORESATCHEL;

    const char* tgt = Rd<const char*>(ped, 0x71C);                            // m_pTargetedObject
    if (tgt && (Rd<BYTE>(tgt, 0x36) & 7) == 3) in.state |= S_TARGETPED;
    if (GroupMembers(ped) > 0) in.state |= S_GROUPMEMBERS;
    if (in.state & (S_TARGETPED | S_GROUPMEMBERS)) in.state |= S_GROUP;

    int nearCls = NearestVehicleClass(ped);
    if (nearCls >= 0) { in.state |= S_VEHNEAR; in.nearVeh = (BYTE)(nearCls + 1); }
}

static bool SteeringContext() {
    if (MenuActive()) return false;
    int c = g_ctxCache;
    return c == CTX_CAR || c == CTX_BIKE || c == CTX_HELI || c == CTX_PLANE || c == CTX_BOAT;
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
static volatile DWORD g_lastXInputTick = 0, g_lastMenuWrite = 0;
static DWORD g_packetBump = 0;
static PadState g_lastOut = {};
static bool g_menuNoted = false;

static SHORT Stronger(SHORT a, SHORT b) { return (abs((int)b) > abs((int)a)) ? b : a; }

static DWORD Filter(DWORD ret, DWORD idx, XState* st) {
    if (idx != 0 || !st) return ret;
    InterlockedIncrement(&g_xinputCalls);
    DWORD now = GetTickCount();
    g_lastXInputTick = now;

    // no menu o quadro do jogo para: o contexto sai daqui
    if (MenuActive()) {
        g_ctxCache = CTX_MENU;
        if (now - g_lastMenuWrite >= 30) {
            g_lastMenuWrite = now;
            Info m = {}; m.ctx = CTX_MENU;
            WriteGame(m, 1);
            if (!g_menuNoted) { g_menuNoted = true; Log("menu: contexto enviado pelo XInput"); }
        }
    }

    // 1) mistura os toques do overlay
    HostBlock h;
    bool host = ReadHost(&h);
    if (host) {
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
    // 3) Sim/Nao do overlay: setas de verdade, mesmo dentro do carro
    if (host) st->Gamepad.wButtons |= h.rawButtons;

    // 4) o leitor so processa pacotes novos: numera quando o que entregamos mudou
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
static CRITICAL_SECTION g_hookCs;

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
    if (!g_mhReady) return;
    EnterCriticalSection(&g_hookCs);
    const char* mods[] = { "xinput1_3.dll", "xinput1_4.dll", "xinput9_1_0.dll", "xinput1_2.dll", "xinput1_1.dll" };
    for (const char* name : mods) {
        HMODULE m = GetModuleHandleA(name);
        if (!m) m = LoadLibraryA(name);
        if (!m) continue;
        HookTarget(m, name, "XInputGetState", "XInputGetState");
        HookTarget(m, name, (LPCSTR)100, "XInputGetStateEx(100)");
    }
    LeaveCriticalSection(&g_hookCs);
}

// ---------------------------------------------------------------- reserva: CPad::NewState
static void ApplyToNewState(short* ns) {
    HostBlock h;
    bool host = ReadHost(&h);
    struct { WORD bit; int off; } map[] = {
        {0x0001, 0x10}, {0x0002, 0x12}, {0x0004, 0x14}, {0x0008, 0x16}, {0x0010, 0x18}, {0x0020, 0x1A},
        {0x4000, 0x1C}, {0x8000, 0x1E}, {0x1000, 0x20}, {0x2000, 0x22}, {0x0040, 0x24}, {0x0080, 0x26},
        {0x0100, 0x08}, {0x0200, 0x0C} };
    if (host) {
        for (auto& m : map) if (h.buttons & m.bit) ns[m.off / 2] = 255;
        if (h.lt) ns[0x0A / 2] = h.lt;
        if (h.rt) ns[0x0E / 2] = h.rt;
        auto st = [](SHORT v) { return (short)(v / 256); };
        if (h.lx) ns[0] = st(h.lx);
        if (h.ly) ns[1] = (short)-st(h.ly);           // CPad: para baixo e positivo
        if (h.rx) ns[2] = st(h.rx);
        if (h.ry) ns[3] = (short)-st(h.ry);
    }
    if (g_steerOn) {
        if (!SteeringContext()) { g_steer = 0.0; g_last.QuadPart = 0; }
        else {
            int dir = (ns[0x16 / 2] ? 1 : 0) - (ns[0x14 / 2] ? 1 : 0);
            double s = Advance(dir);
            if (dir != 0 || s != 0.0) { ns[0x14 / 2] = 0; ns[0x16 / 2] = 0; ns[0] = (short)(s * 128.0); }
        }
    }
    if (host) for (auto& m : map) if (h.rawButtons & m.bit) ns[m.off / 2] = 255;
}

// ---------------------------------------------------------------- salvar / autosave / veiculo
typedef void  (__cdecl* Void_t)();
typedef bool  (__cdecl* Bool_t)();
typedef void  (__cdecl* VoidInt_t)(int);
typedef int   (__cdecl* Int_t)();
typedef void  (__cdecl* SetDir_t)(const char*);
typedef char* (__cdecl* CreateCar_t)(int, V3, unsigned char);
typedef float (__cdecl* GroundZ_t)(float, float, float, bool*, void**);
typedef void  (__cdecl* SetPedInCar_t)(void*, void*, int, bool);
typedef void  (__thiscall* VehInt_t)(void*, int);
typedef int   (__thiscall* VehGetInt_t)(void*);
typedef void  (__thiscall* SetHeading_t)(void*, float);

static const Bool_t   IsPlayerOnAMission   = (Bool_t)0x464D50;
static const VoidInt_t MakeValidSaveName   = (VoidInt_t)0x5D0E90;
static const Int_t    SetDirMyDocuments    = (Int_t)0x538860;
static const SetDir_t SetDir               = (SetDir_t)0x5387D0;
typedef bool (__cdecl* GenericSave_t)(int);
static const GenericSave_t GenericSave   = (GenericSave_t)0x5D13E0;
static const Void_t   RemovePickupObjects  = (Void_t)0x455470;
static const CreateCar_t CreateCarForScript = (CreateCar_t)0x431F80;
typedef void  (__cdecl* RequestModel_t)(int, int);
typedef void  (__cdecl* LoadAll_t)(bool);
static const RequestModel_t RequestModel   = (RequestModel_t)0x4087E0;
static const LoadAll_t LoadAllRequestedModels = (LoadAll_t)0x40EA10;
static const GroundZ_t FindGroundZFor3DCoord = (GroundZ_t)0x5696C0;
static const SetPedInCar_t SetPedInCarDirect = (SetPedInCar_t)0x650280;
static const VehInt_t AddVehicleUpgrade    = (VehInt_t)0x6E3290;
static const VehGetInt_t GetRemapIndex     = (VehGetInt_t)0x6D0B70;
static const VehInt_t SetRemap             = (VehInt_t)0x6D0C00;
static const SetHeading_t SetHeading       = (SetHeading_t)0x43E0C0;
static char* const    SaveFileName         = (char*)0xC16DB8;        // CGenericGameStorage::ms_SaveFileName
static volatile unsigned* const TimeMs     = (unsigned*)0xB7CB84;    // CTimer::m_snTimeInMilliseconds
static volatile int*  const StatMissionsPassed = (int*)(0xB79000 + (147 - 120) * 4);

// detonador e troca de arma
typedef bool (__thiscall* WeaponFire_t)(void*, void*, V3*, V3*, void*, V3*, V3*);
typedef void (__cdecl* UseDetonator_t)(void*);
typedef void (__thiscall* SetCurrentWeapon_t)(void*, int);
static const WeaponFire_t      WeaponFire       = (WeaponFire_t)0x742300;      // CWeapon::Fire
static const UseDetonator_t    UseDetonator     = (UseDetonator_t)0x5660B0;    // CWorld::UseDetonator
static const SetCurrentWeapon_t SetCurrentWeapon = (SetCurrentWeapon_t)0x5E61F0; // CPed::SetCurrentWeapon(slot)

#pragma pack(push, 1)
struct VehicleSave {            // GTASAsfN.b.gtmveh
    DWORD magic;                // 'GTMV'
    DWORD version;
    unsigned timeMs;            // CTimer no instante do save: identifica o save carregado
    int   model;
    float x, y, z, heading;
    BYTE  colors[4];
    float health;
    short upgrades[15];
    int   remap;
    int   vehClass;
};
#pragma pack(pop)
static const DWORD VEH_MAGIC = 0x564D5447;

static bool  g_pendingRestore = false;
static unsigned g_loadedTimeMs = 0;
static DWORD g_lastAutosaveTick = 0, g_missionEndTick = 0, g_hostCmdSeen = 0;
static int   g_lastMissionsPassed = -1;
static bool  g_wasOnMission = false, g_hostCmdInit = false;

static bool CanSaveNow(const char** why) {
    char* ped = PlayerPed();
    if (!ped)                                  { *why = "sem jogador"; return false; }
    if (MenuActive())                          { *why = "menu aberto"; return false; }
    if (IsPlayerOnAMission())                  { *why = "em missao"; return false; }
    if (*(volatile signed char*)0xB5F851)      { *why = "cutscene"; return false; }  // CCutsceneMgr::ms_running
    char* info = PlayerInfo();
    if (Rd<BYTE>(info, 0xDC) != 0)             { *why = "jogador morto/preso"; return false; }
    if (Rd<float>(ped, 0x540) <= 0.f)          { *why = "sem vida"; return false; }
    if (Rd<char*>(info, 0xB0))                 { *why = "controle remoto"; return false; }
    if (!PlayerVehicle()) {                    // a pe: so com os pes no chao ou nadando
        if (g_info.ctx == CTX_JETPACK)         { *why = "de jetpack"; return false; }
        if (g_info.ctx != CTX_SWIM && !(Rd<BYTE>(ped, 0x46C) & 1)) { *why = "no ar"; return false; }   // bIsStanding
    }
    return true;
}

static void SidecarPath(char* out) {
    // ms_SaveFileName ja tem o caminho completo do save do slot (GTASAsfN.b)
    snprintf(out, MAX_PATH, "%s.gtmveh", SaveFileName);
}

static void WriteVehicleSidecar(unsigned timeMs) {
    char path[MAX_PATH]; SidecarPath(path);
    char* veh = (g_keepVehicle && g_info.ctx != CTX_PASSENGER) ? PlayerVehicle() : nullptr;
    if (!veh) { DeleteFileA(path); return; }
    VehicleSave v = {};
    v.magic = VEH_MAGIC; v.version = 1; v.timeMs = timeMs;
    v.model = *(short*)(veh + 0x22);
    float* m = *(float**)(veh + 0x14);                 // CMatrix*: up(frente) em +0x10, pos em +0x30
    if (m) { v.x = m[12]; v.y = m[13]; v.z = m[14]; v.heading = atan2f(-m[4], m[5]); }
    else   { v.x = *(float*)(veh + 0x4); v.y = *(float*)(veh + 0x8); v.z = *(float*)(veh + 0xC); v.heading = *(float*)(veh + 0x10); }
    memcpy(v.colors, veh + 0x434, 4);
    v.health = *(float*)(veh + 0x4C0);
    memcpy(v.upgrades, veh + 0x43A, sizeof v.upgrades);
    v.remap = GetRemapIndex(veh);
    v.vehClass = *(int*)(veh + 0x590);
    FILE* f = fopen(path, "wb");
    if (f) { fwrite(&v, sizeof v, 1, f); fclose(f); }
    Log("veiculo guardado: modelo %d em (%.1f, %.1f, %.1f) %s", v.model, v.x, v.y, v.z, path);
}

static void DoSave(int kind) {
    const char* why = "";
    g_saveInfo.saveSeq++;
    g_saveInfo.saveKind = (DWORD)kind;
    if (!CanSaveNow(&why)) {
        g_saveInfo.saveResult = IsPlayerOnAMission() ? 1 : 2;
        Log("salvar (%d) recusado: %s", kind, why);
        return;
    }
    unsigned t = *TimeMs;
    RemovePickupObjects();                             // como o save normal: os objetos dos pickups voltam sozinhos
    MakeValidSaveName(g_saveSlot - 1);
    SetDirMyDocuments();
    bool ok = GenericSave(0);
    SetDir("");
    if (ok) WriteVehicleSidecar(t);
    g_saveInfo.saveResult = ok ? 0 : 3;
    g_lastAutosaveTick = GetTickCount();
    Log("salvar (%d) no slot %d: %s (%s)", kind, g_saveSlot, ok ? "ok" : "ERRO", SaveFileName);
}

static void DoDetonate() {
    char* ped = PlayerPed();
    if (!ped || PlayerVehicle()) { Log("detonar: so a pe"); return; }
    if (WeaponType(ped, 12) != 40 || WeaponAmmo(ped, 12) <= 0) { Log("detonar: sem detonador"); return; }
    void* det = ped + 0x5A0 + 12 * W_STRIDE;
    V3 pos = EntityPos(ped);
    bool fired = WeaponFire(det, ped, &pos, &pos, nullptr, nullptr, nullptr);   // o mesmo que apertar atirar com o detonador
    if (!fired) UseDetonator(ped);
    Log("detonar: %s", fired ? "ok" : "ok (direto)");
}

static void DoSelectSatchel() {
    char* ped = PlayerPed();
    if (!ped || PlayerVehicle()) return;
    if (WeaponType(ped, 8) != 39 || WeaponAmmo(ped, 8) <= 0) { Log("satchel: acabou"); return; }
    SetCurrentWeapon(ped, 8);
    Log("satchel de volta na mao");
}

static void RestoreVehicle() {
    char path[MAX_PATH];
    MakeValidSaveName(g_saveSlot - 1);                 // so monta o nome; nao grava nada
    SidecarPath(path);
    FILE* f = fopen(path, "rb");
    if (!f) return;
    VehicleSave v = {};
    size_t n = fread(&v, sizeof v, 1, f); fclose(f);
    if (n != 1 || v.magic != VEH_MAGIC) return;
    if (v.timeMs != g_loadedTimeMs) { Log("save carregado nao e o do slot %d com veiculo (t=%u, arquivo t=%u)", g_saveSlot, g_loadedTimeMs, v.timeMs); return; }
    char* ped = PlayerPed();
    if (!ped || PlayerVehicle()) return;

    RequestModel(v.model, 2);
    for (short u : v.upgrades) if (u > 0) RequestModel(u, 2);
    LoadAllRequestedModels(false);

    bool found = false;
    float gz = FindGroundZFor3DCoord(v.x, v.y, v.z + 2.f, &found, nullptr);
    bool air = (v.vehClass == 3 || v.vehClass == 4 || v.vehClass == 7 || v.vehClass == 8);
    V3 pos = { v.x, v.y, v.z };
    if (v.vehClass != 5 && found && (air || fabsf(gz - v.z) < 4.f)) pos.z = gz;   // barco fica onde estava
    char* veh = CreateCarForScript(v.model, pos, 0);
    if (!veh) { Log("nao consegui recriar o veiculo %d", v.model); return; }
    SetHeading(veh, v.heading);
    memcpy(veh + 0x434, v.colors, 4);
    *(float*)(veh + 0x4C0) = v.health;
    for (short u : v.upgrades) if (u > 0) AddVehicleUpgrade(veh, u);
    if (v.remap >= 0) SetRemap(veh, v.remap);
    *(BYTE*)(veh + 0x428) &= ~0x08;                    // bIsLocked = false
    *(BYTE*)(veh + 0x428) |= 0x10;                     // bEngineOn = true
    *(BYTE*)(veh + 0x4A4) = 1;                         // m_nCreatedBy = RANDOM_VEHICLE (some como um carro normal)
    SetPedInCarDirect(ped, veh, 0, true);
    Log("veiculo restaurado: modelo %d", v.model);
}

typedef void (__cdecl* AfterLoad_t)();
static AfterLoad_t g_origAfterLoad = nullptr;
static void __cdecl AfterLoadDetour() {
    if (g_origAfterLoad) g_origAfterLoad();
    g_loadedTimeMs = *TimeMs;
    g_pendingRestore = g_keepVehicle != 0;
    g_lastAutosaveTick = GetTickCount();
    g_lastMissionsPassed = -1;
    g_missionEndTick = 0;
    Log("jogo carregado (t=%u)", g_loadedTimeMs);
}

static void SaveTick() {
    DWORD now = GetTickCount();
    if (g_lastAutosaveTick == 0) g_lastAutosaveTick = now;
    if (g_pendingRestore && PlayerPed()) { g_pendingRestore = false; RestoreVehicle(); }

    // comandos do overlay
    HostBlock h;
    if (ReadHost(&h)) {
        if (!g_hostCmdInit) { g_hostCmdSeen = h.cmdSeq; g_hostCmdInit = true; }
        else if (h.cmdSeq != g_hostCmdSeen) {
            g_hostCmdSeen = h.cmdSeq;
            switch (h.cmd) {
                case 1: DoSave(1); break;
                case 2: DoDetonate(); break;
                case 3: DoSelectSatchel(); break;
                default: break;
            }
        }
    }

    bool onMission = IsPlayerOnAMission();
    int passed = *StatMissionsPassed;
    if (g_lastMissionsPassed < 0) g_lastMissionsPassed = passed;
    if (g_wasOnMission && !onMission) g_missionEndTick = now;
    g_wasOnMission = onMission;

    // depois de passar uma missao: espera a tela de "missao cumprida" sumir
    if (g_afterMission && passed > g_lastMissionsPassed && !onMission && g_missionEndTick && now - g_missionEndTick > 6000) {
        const char* why;
        if (CanSaveNow(&why)) { g_lastMissionsPassed = passed; g_missionEndTick = 0; DoSave(3); }
    }
    if (passed < g_lastMissionsPassed) g_lastMissionsPassed = passed;

    // por tempo (nao logo depois de uma missao acabar: da tempo para o "Retry?")
    if (g_autosaveMin > 0) {
        DWORD period = (DWORD)g_autosaveMin * 60000;
        DWORD elapsed = now - g_lastAutosaveTick;
        bool justEnded = g_missionEndTick && now - g_missionEndTick < 15000;
        if (elapsed >= period && !justEnded) {
            const char* why;
            if (CanSaveNow(&why)) DoSave(2);
        }
        g_saveInfo.autosaveIn = elapsed >= period ? 0 : (period - elapsed) / 1000;
    }
    const char* why;
    g_saveInfo.onMission = onMission ? 1 : 0;
    g_saveInfo.canSave = CanSaveNow(&why) ? 1 : 0;
    g_saveInfo.slot = (BYTE)g_saveSlot;
}

// ---------------------------------------------------------------- gancho por quadro
static Void_t g_origFrameCall = nullptr;
static const DWORD kFrameCall = 0x53C077;              // CALL CCranes::UpdateCranes em CGame::Process (todo quadro sem pausa)
static bool g_firstFrame = true, g_fallbackNoted = false, g_lastYN = false;

static void __cdecl FrameTick() {
    if (g_origFrameCall) g_origFrameCall();
    if (g_firstFrame) {                                 // reserva, se a thread de inicio nao conseguiu
        g_firstFrame = false;
        if (g_hookCount == 0) InstallXInputHooks();
        if (g_hookCount == 0) Log("nenhuma funcao XInput interceptada");
    }
    ComputeInfo(g_info);
    g_ctxCache = g_info.ctx;
    if (g_info.ctx != g_lastCtx) { Log("contexto: %s", g_info.ctx >= 0 && g_info.ctx <= CTX_CUTSCENE ? kCtxName[g_info.ctx] : "?"); g_lastCtx = g_info.ctx; }
    bool yn = (g_info.state & S_YESNO) != 0;
    if (yn != g_lastYN) {
        g_lastYN = yn;
        if (yn) Log("Sim/Nao na tela (%s%s%s)", g_ynWhy, g_ynWhy == kWhyScript ? " " : "", g_ynWhy == kWhyScript ? g_scriptYNName : "");
        else Log("Sim/Nao fora da tela");
    }
    SaveTick();
    bool xinputLive = g_xinputCalls > 0 && GetTickCount() - g_lastXInputTick < 1000;
    WriteGame(g_info, (WORD)(xinputLive ? 1 : 0));

    if (xinputLive) return;
    if (!g_fallbackNoted) { g_fallbackNoted = true; Log("usando o caminho reserva (CPad::NewState)"); }
    short* ns = (short*)(*(char* (__cdecl*)(int))0x53FB70)(0);   // CPad::GetPad(0)
    if (ns) ApplyToNewState(ns);
}

static void InstallFrameHook() {
    BYTE* site = (BYTE*)kFrameCall;
    if (*site != 0xE8) { Log("gancho por quadro desativado: 0x%X nao e CALL (0x%02X)", kFrameCall, *site); return; }
    DWORD target = kFrameCall + 5 + *(int*)(site + 1);
    g_origFrameCall = (Void_t)target;
    DWORD old;
    VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
    *(int*)(site + 1) = (int)((DWORD)&FrameTick - (kFrameCall + 5));
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    Log("gancho por quadro em 0x%X (destino original 0x%X)", kFrameCall, target);
}

static void HookGame(DWORD addr, void* detour, void** orig, const char* what) {
    if (MH_CreateHook((void*)addr, detour, orig) == MH_OK && MH_EnableHook((void*)addr) == MH_OK)
        Log("gancho %s em 0x%X", what, addr);
    else
        Log("falha no gancho %s (0x%X)", what, addr);
}

// fora do DllMain (sem o loader lock): ganchos do MinHook e XInput logo no inicio,
// para o menu principal e o de pausa ja terem controles
static DWORD WINAPI StartupThread(LPVOID) {
    HookGame(0x618E90, (void*)&AfterLoadDetour, (void**)&g_origAfterLoad, "de carregamento");
    HookGame(0x485B10, (void*)&GetPadStateDetour, (void**)&g_origGetPadState, "das setas dos scripts");
    HookGame(0x588BE0, (void*)&SetHelpDetour, (void**)&g_origSetHelp, "do texto de ajuda");
    HookGame(0x588E30, (void*)&SetHelpNumDetour, (void**)&g_origSetHelpNum, "do texto de ajuda com numero");
    InstallXInputHooks();
    return 0;
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
    g_saveSlot  = GetPrivateProfileIntA("Save",  "Slot",      8,   ini);
    g_autosaveMin  = GetPrivateProfileIntA("Save", "AutosaveMinutes", 5, ini);
    g_afterMission = GetPrivateProfileIntA("Save", "AfterMission",    1, ini);
    g_keepVehicle  = GetPrivateProfileIntA("Save", "KeepVehicle",     1, ini);
    g_ynScripts    = GetPrivateProfileIntA("YesNo", "Scripts",        1, ini);
    if (g_saveSlot < 1 || g_saveSlot > 8) g_saveSlot = 8;

    char buf[512];
    GetPrivateProfileStringA("YesNo", "IgnoreScripts", "", buf, sizeof buf, ini);
    snprintf(g_ignoreScripts, sizeof g_ignoreScripts, ",%s,", buf);
    for (char* c = g_ignoreScripts; *c; c++) { *c = (char)tolower((unsigned char)*c); if (*c == ' ' || *c == ';') *c = ','; }
    GetPrivateProfileStringA("Vehicles", "MissionModels", kDefMission, buf, sizeof buf, ini);
    g_nMission = ParseList(buf, g_missionModels, 64);
    GetPrivateProfileStringA("Vehicles", "FireModels", kDefFire, buf, sizeof buf, ini);
    g_nFire = ParseList(buf, g_fireModels, 64);
    GetPrivateProfileStringA("Vehicles", "Fire2Models", kDefFire2, buf, sizeof buf, ini);
    g_nFire2 = ParseList(buf, g_fire2Models, 32);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    InitializeCriticalSection(&g_logCs);
    InitializeCriticalSection(&g_io);
    InitializeCriticalSection(&g_hookCs);
    LoadConfig(inst);
    QueryPerformanceFrequency(&g_qpf);
    Log("GTMTouchSA 3.0 carregado (direcao=%d rampa=%d retorno=%d toque=%d sim/nao por script=%d)",
        g_steerOn, g_rampMs, g_releaseMs, g_touchOn, g_ynScripts);
    if (GetModuleHandleA("GTMSteerSA.asi")) Log("AVISO: GTMSteerSA.asi tambem esta carregado; apague ele da pasta scripts");
    if (!IsSupportedExe()) { Log("executavel nao e o 1.0 US: nada foi alterado"); return TRUE; }
    OpenShared();
    InstallFrameHook();
    g_mhReady = (MH_Initialize() == MH_OK);
    if (!g_mhReady) { Log("MinHook nao iniciou"); return TRUE; }
    HANDLE t = CreateThread(nullptr, 0, StartupThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t); else Log("nao criou a thread de inicio (erro %lu)", GetLastError());
    return TRUE;
}
