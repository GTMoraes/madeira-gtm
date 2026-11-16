// GTMTouchSA.asi 3.12 - controles de toque contextuais para GTA San Andreas 1.0 US no Madeira
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
// 5) Comandos do overlay: 1 salvar, 2 detonar as satchels, 3 voltar para a satchel, 4 recarregar.
// 6) Skin Selector (3.10/3.12): Status + Atirar abre o menu; a skin escolhida anda e fala como o
//    personagem do modelo; o save daqui grava a skin no slot do save.
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
//   [Debug]    Log=1  TouchTrace=0  RagdollTrace=0  RagdollCount=0 (diagnosticos)

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <tlhelp32.h>
#include <stdint.h>
#include "MinHook.h"

// ---------------------------------------------------------------- configuracao e log
static int  g_steerOn = 1, g_rampMs = 150, g_releaseMs = 60, g_touchOn = 1, g_log = 1;
static int  g_traceTouch = 0, g_traceRagdoll = 0, g_countRagdoll = 0, g_audioHash = 0, g_fixX87 = 1, g_skipIntro = 1, g_radarTop = 16, g_weaponGuard = 1;     // diagnosticos ([Debug] TouchTrace / RagdollTrace)
static int  g_saveSlot = 8, g_autosaveMin = 5, g_afterMission = 1, g_keepVehicle = 1;
static int  g_ynScripts = 1;
static char g_ignoreScripts[512], g_ignoreTabScripts[512];                       // minusculas, ",nome,nome,"
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

static void X87Check();
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
    S_ACTPHONE     = 1u << 17,  // o TAB (acao) atende o telefone
    S_ACTWEAPON    = 1u << 18,  // o TAB troca pela arma do chao
    S_ACTOTHER     = 1u << 19,  // o jogo pede o TAB para outra coisa (comprar, presente, foto...)
    S_RELOAD       = 1u << 20,  // a pe, a arma na mao pode ser recarregada agora (toque no icone da arma)
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
// textos que pedem o TAB (~k~~PED_ANSWER_PHONE~): telefone, trocar pela arma do chao, ou outra coisa
static volatile DWORD g_helpAct = 0, g_helpActTick = 0;
static bool HasWord(const char* t, const char* w) {          // sem diferenciar maiusculas
    size_t n = strlen(w);
    for (; *t; t++) if (!_strnicmp(t, w, n)) return true;
    return false;
}
static void NoteHelp(const char* text, bool permanent) {
    if (HelpHasYesNo(text)) { g_helpYN = true; g_helpYNPerm = permanent; g_helpYNTick = GetTickCount(); }
    else g_helpYN = false;
    if (text && strstr(text, "PED_ANSWER_PHONE")) {
        DWORD kind = HasWord(text, "phone") ? S_ACTPHONE : HasWord(text, "weapon") ? S_ACTWEAPON : S_ACTOTHER;
        if (kind != g_helpAct) Log("acao: o jogo pede o TAB (%s)", kind == S_ACTPHONE ? "telefone" : kind == S_ACTWEAPON ? "trocar arma" : "outro");
        g_helpAct = kind; g_helpActTick = GetTickCount();
    } else g_helpAct = 0;
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
// scripts que leem o TAB (IS_BUTTON_PRESSED 0 LEFTSHOULDER1): telefone tocando e afins
static volatile DWORD g_scriptTabTick = 0;
static char g_loggedTab[32][9];
static int  g_nLoggedTab = 0;
static char g_skinScripts[128] = ",skin_o,skin_c,skin_n,";             // [Skins] Scripts
static volatile DWORD g_skinTick = 0;                                   // um Skin Selector leu o TAB
static char* volatile g_skinScript = nullptr;                          // CRunningScript do Skin Selector
static char g_skinScriptName[9] = "";
static void NoteScriptTab(const char* script) {
    char name[9] = "";
    if (script) { memcpy(name, script + 8, 8); name[8] = 0; }
    for (char* c = name; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (name[0]) {
        char key[12]; snprintf(key, sizeof key, ",%s,", name);
        if (strstr(g_skinScripts, key)) {
            if (!g_skinTick) Log("skins: Skin Selector ativo (script '%s'); segure o Status e toque no Atirar para abrir o menu dele", name);
            g_skinTick = GetTickCount();
            g_skinScript = (char*)script; memcpy(g_skinScriptName, name, sizeof name);
            return;                                                     // le o TAB o tempo todo: nao e pedido de acao
        }
        if (strstr(g_ignoreTabScripts, key)) return;
    }
    g_scriptTabTick = GetTickCount();
    for (int i = 0; i < g_nLoggedTab; i++) if (!strcmp(g_loggedTab[i], name)) return;
    if (g_nLoggedTab < 32) { strcpy(g_loggedTab[g_nLoggedTab++], name); Log("script '%s' le o TAB (acao)", name); }
}

static int __fastcall GetPadStateDetour(void* self, void* /*edx*/, int pad, int button) {
    int r = g_origGetPadState(self, pad, button);
    int b = button & 0xFFFF;
    if (g_ynScripts && (pad & 0xFFFF) == 0 && (b == 10 || b == 11)) NoteScriptYesNo((const char*)self);
    if ((pad & 0xFFFF) == 0 && b == 4) NoteScriptTab((const char*)self);
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

// ---------------------------------------------------------------- recarregar (comando 4 do overlay)
// O jogo recarrega sozinho quando o pente acaba: CWeapon::Fire (0x73FACB) poe a arma em RELOADING com
// m_nTimeForNextShot = agora + CWeaponInfo::GetWeaponReloadTime, e o CWeapon::Update toca a animacao (se o
// CJ estiver mirando), os sons e enche o pente no fim. O toque no icone da arma faz o mesmo antes do pente acabar.
// A animacao quem comeca e a tarefa de mirar (CTaskSimpleUseGun, 0x62515F); sem mirar o jogo so conta o tempo.
// Sem mirar, o toque comeca a mesma animacao de recarga em pe (id 0xE2 do grupo da arma, que so mexe da coluna
// para cima, entao o CJ continua andando ou correndo); o CWeapon::Update ve a animacao, toca os sons no tempo
// dela e enche o pente quando ela chega a 90%. A agachada (0xE3) mexe nas pernas e nao e usada aqui.
typedef char* (__cdecl* GetWeaponInfo_t)(int type, int skill);
typedef int   (__thiscall* GetWeaponSkill_t)(void* ped, int type);
typedef int   (__thiscall* GetReloadTime_t)(void* info);
static const GetWeaponInfo_t  GetWeaponInfo  = (GetWeaponInfo_t)0x743C60;   // CWeaponInfo::GetWeaponInfo(tipo, habilidade)
static const GetWeaponSkill_t GetWeaponSkill = (GetWeaponSkill_t)0x5E3B60;  // CPed::GetWeaponSkill(tipo)
static const GetReloadTime_t  GetReloadTime  = (GetReloadTime_t)0x743D70;   // CWeaponInfo::GetWeaponReloadTime
typedef void* (__cdecl* BlendAnimation_t)(void* clump, int group, int id, float delta);
typedef void  (__thiscall* AssocStart_t)(void* assoc, float time);
typedef void* (__thiscall* GetTaskUseGun_t)(void* intelligence);
static const BlendAnimation_t BlendAnimation = (BlendAnimation_t)0x4D4610;  // CAnimManager::BlendAnimation
static const AssocStart_t     AssocStart     = (AssocStart_t)0x4CEB70;      // CAnimBlendAssociation::Start
static const GetTaskUseGun_t  GetTaskUseGun  = (GetTaskUseGun_t)0x600F70;   // CPedIntelligence::GetTaskUseGun

// a arma na mao pode ser recarregada agora? (pronta, pente que recarrega e nao esta cheio, municao de reserva)
static bool CanReload(char* ped, int* reloadMs) {
    int slot = Rd<BYTE>(ped, 0x718); if (slot > 12) return false;
    const char* w = ped + 0x5A0 + slot * W_STRIDE;                            // CWeapon
    int type = Rd<int>(w, 0), state = Rd<int>(w, 4), clip = Rd<int>(w, 8), total = Rd<int>(w, 0xC);
    if (type < 22 || type > 38 || state != 0) return false;                  // armas de fogo, WEAPONSTATE_READY
    const char* info = GetWeaponInfo(type, GetWeaponSkill(ped, type) & 0xFF);
    if (!info || !(Rd<DWORD>(info, 0x18) & 0x1000)) return false;             // m_nFlags: bReload
    int size = Rd<short>(info, 0x20);                                         // m_nAmmoClip (ja com a habilidade)
    if (size <= 1) return false;
    int full = total < size ? total : size;                                   // o total inclui o pente
    if (clip >= full) return false;
    if (reloadMs) *reloadMs = GetReloadTime((void*)info);
    return true;
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
    {
        DWORD now = GetTickCount();
        bool shown = *(volatile char*)0xBAA480 != 0;                          // CHud::m_pHelpMessageToPrint
        if (g_helpAct && shown && now - g_helpActTick < 20000) in.state |= g_helpAct;
        else if (g_scriptTabTick && now - g_scriptTabTick < 600) in.state |= S_ACTPHONE;   // script esperando o TAB
    }
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
    else { in.ctx = CTX_FOOT; if (CanReload(ped, nullptr)) in.state |= S_RELOAD; }

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

// Skin Selector: o menu dele abre com TAB + arma anterior + proxima arma juntos (LB + LT + RT).
// Com o Status (TAB) do overlay segurado, um toque no Atirar abre o menu: a pe, sem o jogo pedir o TAB
// para outra coisa e com um Skin Selector rodando. Esse Atirar nao chega ao jogo; os dois gatilhos entram
// juntos ate o Status ser solto. Segurar o Status sozinho continua mostrando o status do CJ.
static int  g_skinOn = 1, g_skinAnims = 1, g_skinVoice = 1;
static bool g_skinCombo = false;
static void SkinCombo(WORD buttons, PadState& p, DWORD now) {
    if (!g_skinOn) return;
    const WORD LB = 0x0100, FIRE = 0x2000;
    if (!(buttons & LB)) { g_skinCombo = false; return; }
    if (!g_skinCombo) {
        if (!(buttons & FIRE)) return;
        bool armed = g_skinTick && now - g_skinTick < 2000 && g_ctxCache == CTX_FOOT &&
                     !(g_info.state & (S_ACTPHONE | S_ACTWEAPON | S_ACTOTHER));
        if (!armed) return;
        g_skinCombo = true;
        Log("skins: Status + Atirar, abrindo o menu do Skin Selector");
    }
    p.wButtons &= (WORD)~FIRE;
    p.bLeftTrigger = p.bRightTrigger = 255;
}

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
        SkinCombo(h.buttons, p, now);
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

    // diagnostico: o que o jogo recebeu, quadro a quadro (so quando muda)
    if (g_traceTouch) {
        static WORD lastB = 0xFFFF; static BYTE lastLT = 0xFF, lastRT = 0xFF;
        const PadState& p = st->Gamepad;
        if (p.wButtons != lastB || p.bLeftTrigger != lastLT || p.bRightTrigger != lastRT) {
            lastB = p.wButtons; lastLT = p.bLeftTrigger; lastRT = p.bRightTrigger;
            Log("toque: quadro %u botoes 0x%04X LT %u RT %u (overlay 0x%04X)", *(volatile unsigned*)0xB7CB4C,
                (unsigned)p.wButtons, (unsigned)p.bLeftTrigger, (unsigned)p.bRightTrigger, host ? (unsigned)h.buttons : 0u);
        }
    }

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

// Skin Selector (o): guarda a skin de cada slot em cleo\skin_o\skin_o.sav (assinatura 0x43010253 e 8
// registros de 32 bytes: tipo 8@, numero 9@, 0, nome da skin adicional 24@..28@). O script so grava quando o
// menu do jogo abre, no ultimo slot escolhido no menu (FrontEndMenuManager.m_bSelectedSaveGame, 0xBA68A7).
// O save daqui nao passa pelo menu: grava o registro do slot do save com a skin atual do script e marca esse
// slot no menu, para o script continuar gravando no mesmo lugar.
static void SkinSaveRecord(int slotIdx) {
    char* sc = g_skinScript;
    if (!sc || !g_skinTick || GetTickCount() - g_skinTick > 3000 || strcmp(g_skinScriptName, "skin_o")) return;
    if (slotIdx < 0 || slotIdx > 7) return;
    const int* lv = (const int*)(sc + 0x3C);                                  // CRunningScript::m_aLocalVars
    int rec[8] = { lv[8], lv[9], 0, lv[24], lv[25], lv[26], lv[27], lv[28] };
    if (rec[0] < 0 || rec[0] > 3) { Log("skins: Skin Selector ainda iniciando; skin nao gravada no save"); return; }
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* sl = strrchr(path, '\\'); if (sl) sl[1] = 0; else path[0] = 0;
    char dir[MAX_PATH]; snprintf(dir, sizeof dir, "%scleo\\skin_o", path);
    CreateDirectoryA(dir, nullptr);
    strncat(path, "cleo\\skin_o\\skin_o.sav", MAX_PATH - strlen(path) - 1);
    const DWORD kMagic = 0x43010253;
    FILE* f = fopen(path, "r+b");
    DWORD magic = 0;
    if (f && (fread(&magic, 4, 1, f) != 1 || magic != kMagic)) { fclose(f); f = nullptr; }
    if (!f) {                                                                 // arquivo novo, como o script cria
        f = fopen(path, "wb");
        if (!f) { Log("skins: nao abriu %s", path); return; }
        int zero[8] = {};
        fwrite(&kMagic, 4, 1, f);
        for (int i = 0; i < 8; i++) fwrite(zero, sizeof zero, 1, f);
    }
    fseek(f, 4 + slotIdx * 32, SEEK_SET);
    bool ok = fwrite(rec, sizeof rec, 1, f) == 1;
    fclose(f);
    *(volatile BYTE*)0xBA68A7 = (BYTE)slotIdx;
    static const char* kKind[4] = { "CJ", "pedestre", "especial", "adicional" };
    Log("skins: skin gravada no slot %d (%s %d)%s", slotIdx + 1, kKind[rec[0]], rec[1], ok ? "" : " ERRO ao gravar");
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
    // CPools::SavePedPool (0x5D4B40) so grava o CJ se ele NAO estiver num veiculo (bInVehicle, bit 0x100
    // de +0x46C). Salvando dentro do carro, o save sai sem o jogador e o jogo trava ao carregar. CPed::Save
    // guarda so posicao, vida, colete, armas e area: durante o save o CJ conta como fora do carro, na
    // posicao em que esta; ao carregar, o veiculo e recriado pelo .gtmveh e o CJ volta para dentro.
    char* sp = PlayerPed();
    DWORD* pedFlags = sp ? (DWORD*)(sp + 0x46C) : nullptr;
    bool inVeh = pedFlags && (*pedFlags & 0x100);
    if (inVeh) *pedFlags &= ~0x100u;
    bool ok = GenericSave(0);
    if (inVeh) *pedFlags |= 0x100u;
    SetDir("");
    if (ok) WriteVehicleSidecar(t);
    if (ok) SkinSaveRecord(g_saveSlot - 1);
    g_saveInfo.saveResult = ok ? 0 : 3;
    g_lastAutosaveTick = GetTickCount();
    Log("salvar (%d) no slot %d: %s%s (%s)", kind, g_saveSlot, ok ? "ok" : "ERRO", inVeh ? ", CJ gravado fora do veiculo" : "", SaveFileName);
}

static void DoReload() {
    char* ped = PlayerPed();
    if (!ped || PlayerVehicle() || g_info.ctx != CTX_FOOT) { Log("recarregar: so a pe"); return; }
    int ms = 0;
    if (!CanReload(ped, &ms)) { Log("recarregar: a arma nao precisa ou nao da agora"); return; }
    int slot = Rd<BYTE>(ped, 0x718);
    char* w = ped + 0x5A0 + slot * W_STRIDE;
    *(int*)(w + 4) = 2;                                                       // WEAPONSTATE_RELOADING
    *(unsigned*)(w + 0x10) = *TimeMs + (unsigned)ms;                          // m_nTimeForNextShot
    const char* anim = "a do jogo, mirando";
    void* intel = Rd<void*>(ped, 0x47C);                                      // CPed::m_pIntelligence
    if (!intel || !GetTaskUseGun(intel)) {                                    // sem mirar: comeca a animacao aqui
        int type = *(int*)w;
        const char* info = GetWeaponInfo(type, GetWeaponSkill(ped, type) & 0xFF);
        int grp = info ? Rd<int>(info, 0x1C) : -1;                            // m_nAnimToPlay (grupo da arma)
        const char* base = *(char**)0xB4EA34;                                 // CAnimManager::ms_aAnimAssocGroups
        void* clump = Rd<void*>(ped, 0x18);                                   // CEntity::m_pRwClump
        anim = "nenhuma (animacoes da arma fora da memoria)";
        if (clump && base && grp > 0 && grp < 118) {
            const char* g = base + grp * 0x14;
            int first = Rd<int>(g, 0xC), count = Rd<int>(g, 8);
            if (Rd<char*>(g, 4) && 0xE2 >= first && 0xE2 < first + count) {
                void* a = BlendAnimation(clump, grp, 0xE2, 8.0f);             // igual a tarefa de mirar
                if (a) { AssocStart(a, 0.0f); anim = "tocada aqui, sem mirar"; }
            }
        }
    }
    Log("recarregar: arma %d, pente com %d de %d, %d ms, animacao %s", *(int*)w, *(int*)(w + 8), *(int*)(w + 0xC), ms, anim);
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
                case 4: DoReload(); break;
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

// ---------------------------------------------------------------- diagnostico do Ragdoll
// (3.2) Pontos do codigo do jogo que o RagDoll_physics.bin altera (lidos do proprio .bin):
// confere se os desvios dele estao la, para onde apontam e, com RagdollCount=1, conta
// quantas vezes cada um roda (um contador intermediario no caminho, sem tocar no .bin).
struct RdSite { DWORD addr; BYTE orig[5]; };
static const RdSite kRdSites[] = {
    { 0x538090, {0x6A,0xFF,0x68,0x31,0xC9} }, { 0x53BCA0, {0xE8,0x2B,0x6E,0x07,0x00} },
    { 0x532B20, {0x56,0x8B,0xF1,0x8B,0x46} }, { 0x53E293, {0xE8,0x28,0x16,0x0D,0x00} },
    { 0x6D6263, {0xC2,0x04,0x00,0x90,0x90} }, { 0x6E2D90, {0x8B,0x4C,0x24,0x04,0x56} },
    { 0x5E4760, {0x8B,0x4C,0x24,0x04,0x56} }, { 0x681AF0, {0x53,0x8B,0x5C,0x24,0x08} },
    { 0x73B550, {0x6A,0xFF,0x68,0x50,0x8E} }, { 0x59F660, {0x6A,0xFF,0x68,0x28,0xD2} },
    { 0x5A1D69, {0xC3,0x90,0x90,0x90,0x90} }, { 0x5A1DDE, {0xC2,0x08,0x00,0x90,0x90} },
    { 0x5A1ED3, {0xC2,0x04,0x00,0x90,0x90} },
};
static const int kRdN = sizeof kRdSites / sizeof kRdSites[0];
static char  g_rdSnap[kRdN][96];
static bool  g_rdSnapped = false;
static BYTE* g_rdStubs = nullptr;                 // 16 bytes por ponto
static DWORD g_rdTarget[kRdN];                    // destino real (no .bin) de cada contador
static volatile LONG g_rdCount[kRdN];
static LONG  g_rdPrinted[kRdN];
static bool  g_rdCounting = false;

static void ModuleOf(DWORD a, char* out, int n) {
    HMODULE m = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m) && m) {
        char path[MAX_PATH]; path[0] = 0;
        GetModuleFileNameA(m, path, MAX_PATH);
        const char* b = strrchr(path, '\\');
        snprintf(out, n, "%s", b ? b + 1 : path);
    } else snprintf(out, n, "memoria sem modulo");
}

static void DescribeSite(int i, char* out, int n) {
    const BYTE* s = (const BYTE*)kRdSites[i].addr;
    BYTE b[5]; memcpy(b, s, 5);
    if (!memcmp(b, kRdSites[i].orig, 5)) { snprintf(out, n, "original do jogo (sem desvio)"); return; }
    if (b[0] == 0xE8 || b[0] == 0xE9) {
        DWORD t = kRdSites[i].addr + 5 + *(int*)(b + 1);
        bool viaCount = g_rdStubs && t == (DWORD)(g_rdStubs + i * 16);
        if (viaCount) t = g_rdTarget[i];
        char mod[64]; ModuleOf(t, mod, sizeof mod);
        snprintf(out, n, "%s -> 0x%08lX (%s)%s", b[0] == 0xE8 ? "CALL" : "JMP", (unsigned long)t, mod, viaCount ? " [contando]" : "");
        return;
    }
    snprintf(out, n, "alterado: %02X %02X %02X %02X %02X", b[0], b[1], b[2], b[3], b[4]);
}

static void ListMods() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) { Log("ragdoll: nao listou os modulos (erro %lu)", GetLastError()); return; }
    MODULEENTRY32 me; me.dwSize = sizeof me;
    for (BOOL ok = Module32First(snap, &me); ok; ok = Module32Next(snap, &me)) {
        char low[MAX_PATH]; snprintf(low, sizeof low, "%s", me.szExePath);
        for (char* c = low; *c; c++) *c = (char)tolower((unsigned char)*c);
        if (strstr(low, "\\windows\\")) continue;
        Log("ragdoll: modulo carregado %s em 0x%08lX (%lu KB)", me.szExePath, (unsigned long)(DWORD_PTR)me.modBaseAddr, (unsigned long)(me.modBaseSize / 1024));
    }
    CloseHandle(snap);
}

static void InstallRdCounters() {
    HMODULE rd = GetModuleHandleA("RagDoll_physics.bin");
    if (!rd) { Log("ragdoll: RagDoll_physics.bin nao esta carregado; contadores nao instalados"); return; }
    g_rdStubs = (BYTE*)VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_rdStubs) { Log("ragdoll: sem memoria para os contadores"); return; }
    int n = 0;
    for (int i = 0; i < kRdN; i++) {
        BYTE* site = (BYTE*)kRdSites[i].addr;
        if (site[0] != 0xE8 && site[0] != 0xE9) continue;
        DWORD t = kRdSites[i].addr + 5 + *(int*)(site + 1);
        HMODULE m = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)t, &m) || m != rd) continue;
        BYTE* st = g_rdStubs + i * 16;
        st[0] = 0x9C;                                                  // pushfd
        st[1] = 0xF0; st[2] = 0xFF; st[3] = 0x05;                      // lock inc dword [contador]
        *(DWORD*)(st + 4) = (DWORD)&g_rdCount[i];
        st[8] = 0x9D;                                                  // popfd
        st[9] = 0xE9; *(int*)(st + 10) = (int)(t - (DWORD)(st + 14));  // jmp destino original
        g_rdTarget[i] = t;
        FlushInstructionCache(GetCurrentProcess(), st, 16);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        *(int*)(site + 1) = (int)((DWORD)st - (kRdSites[i].addr + 5));
        VirtualProtect(site, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
        n++;
    }
    g_rdCounting = n > 0;
    Log("ragdoll: %d contadores instalados", n);
}

static void CheckRagdollHooks(bool full, const char* when) {
    if (full) {
        HMODULE rd = GetModuleHandleA("RagDoll_physics.bin");
        Log("ragdoll: conferindo os desvios (%s); RagDoll_physics.bin %s 0x%08lX", when,
            rd ? "carregado em" : "NAO carregado", (unsigned long)(DWORD_PTR)rd);
    }
    bool any = false;
    for (int i = 0; i < kRdN; i++) {
        char d[96]; DescribeSite(i, d, sizeof d);
        if (full || !g_rdSnapped || strcmp(d, g_rdSnap[i])) {
            Log("ragdoll: desvio 0x%06lX: %s%s", (unsigned long)kRdSites[i].addr, d, (!full && g_rdSnapped) ? " (MUDOU)" : "");
            any = true;
        }
        snprintf(g_rdSnap[i], sizeof g_rdSnap[i], "%s", d);
    }
    g_rdSnapped = true;
    (void)any;
}

static void PrintRdCounts(const char* when) {
    if (!g_rdCounting) return;
    char line[700]; int o = 0;
    for (int i = 0; i < kRdN && o < (int)sizeof line - 40; i++) {
        if (!g_rdTarget[i]) continue;
        LONG c = g_rdCount[i];
        o += snprintf(line + o, sizeof line - o, " %06lX=%ld(+%ld)", (unsigned long)kRdSites[i].addr, (long)c, (long)(c - g_rdPrinted[i]));
        g_rdPrinted[i] = c;
    }
    Log("ragdoll: contagem (%s):%s", when, line);
}


// (3.3) configuracao que o Ragdoll realmente leu: valores na memoria do .bin
// (enderecos tirados do codigo dele) e o que o Windows/Wine devolve do RagDoll_physics.ini
struct RdCfg { const char* name; DWORD rva; char kind; };   // i int, d double, f float
static const RdCfg kRdCfg[] = {
    {"iNumSubsteps",0xC2570,'i'}, {"fSimulationSpeed",0xBA890,'d'}, {"fRagdollLinearDamping",0xBA8A8,'f'},
    {"fRagdollAngularDamping",0xBA884,'f'}, {"fRagdollGravity",0xBA8A0,'f'}, {"fBulletPower",0xBA8B0,'f'},
    {"fExplosionPower",0xBA89C,'f'}, {"fSurfaceFriction",0xBA8B4,'f'}, {"fRagDollFriction",0xBA870,'f'},
    {"fRagDollRestitution",0xC254C,'f'}, {"fRagDollJointDamping",0xBA8AC,'f'}, {"fRagDollGetUpThreshold",0xBA8B8,'f'},
    {"bVehicleShapeUseSpheres",0xBA87C,'i'}, {"bUseDynamicObjects",0xBA8A4,'i'}, {"fRagDollShapeMargin",0xC2560,'f'},
    {"fWorldShapeMargin",0xC2540,'f'}, {"fDynamicObjectMass",0xBA898,'f'}, {"fWorldMinX",0xBA880,'f'},
    {"fWorldMinY",0xBA88C,'f'}, {"fWorldMaxX",0xBA874,'f'}, {"fWorldMaxY",0xBA888,'f'}, {"iNumIPLInstances",0xBA878,'i'},
};
static void RagdollConfig() {
    HMODULE rd = GetModuleHandleA("RagDoll_physics.bin");
    wchar_t exe[MAX_PATH]; exe[0] = 0;
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t ini[MAX_PATH + 32]; wcscpy(ini, exe);
    wchar_t* sl = wcsrchr(ini, L'\\'); if (sl) *sl = 0;
    wcscat(ini, L"\\RagDoll_physics.ini");
    char exeA[MAX_PATH * 2], iniA[MAX_PATH * 2];
    WideCharToMultiByte(CP_UTF8, 0, exe, -1, exeA, sizeof exeA, nullptr, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, ini, -1, iniA, sizeof iniA, nullptr, nullptr);
    DWORD attr = GetFileAttributesW(ini);
    LONG size = -1;
    HANDLE f = CreateFileW(ini, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) { size = (LONG)GetFileSize(f, nullptr); CloseHandle(f); }
    Log("ragdoll: exe do jogo %s; ini que o Ragdoll procura %s: %s (%ld bytes); codepage %u",
        exeA, iniA, attr == INVALID_FILE_ATTRIBUTES ? "NAO EXISTE" : "existe", (long)size, GetACP());
    {   // caminho que o RagDoll_physics.asi entrega ao .bin (buffer em +0x3008 do .asi)
        wchar_t cwd[MAX_PATH]; cwd[0] = 0; GetCurrentDirectoryW(MAX_PATH, cwd);
        char cwdA[MAX_PATH * 2]; WideCharToMultiByte(CP_UTF8, 0, cwd, -1, cwdA, sizeof cwdA, nullptr, nullptr);
        HMODULE asi = GetModuleHandleA("RagDoll_physics.asi");
        char buf[600]; int o = 0;
        if (asi) {
            const wchar_t* w = (const wchar_t*)((const char*)asi + 0x3008);
            for (int k = 0; k < 260 && w[k] && o < (int)sizeof buf - 8; k++) {
                wchar_t c = w[k];
                if (c >= 32 && c < 127) buf[o++] = (char)c;
                else o += snprintf(buf + o, sizeof buf - o, "<%04X>", (unsigned)c);
            }
        }
        buf[o] = 0;
        Log("ragdoll: pasta atual %s; caminho que o RagDoll_physics.asi montou: pasta do exe + \"%s\"%s",
            cwdA, buf, asi ? "" : " (RagDoll_physics.asi nao encontrado)");
    }
    static const wchar_t* keys[] = { L"iNumSubsteps", L"fSimulationSpeed", L"fRagdollGravity", L"bUseDynamicObjects", L"fWorldMinX" };
    for (const wchar_t* k : keys) {
        wchar_t v[256]; v[0] = 0;
        GetPrivateProfileStringW(L"CONFIG", k, L"<faltou>", v, 255, ini);
        char kA[64], vA[512];
        WideCharToMultiByte(CP_UTF8, 0, k, -1, kA, sizeof kA, nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, v, -1, vA, sizeof vA, nullptr, nullptr);
        Log("ragdoll: ini [CONFIG] %s = \"%s\"", kA, vA);
    }
    if (!rd) { Log("ragdoll: RagDoll_physics.bin nao carregado; sem valores da memoria"); return; }
    char line[1400]; int o = 0;
    for (const RdCfg& c : kRdCfg) {
        const char* a = (const char*)rd + c.rva;
        if (c.kind == 'i')      o += snprintf(line + o, sizeof line - o, " %s=%d", c.name, *(const int*)a);
        else if (c.kind == 'd') o += snprintf(line + o, sizeof line - o, " %s=%g", c.name, *(const double*)a);
        else                    o += snprintf(line + o, sizeof line - o, " %s=%g", c.name, *(const float*)a);
        if (o > (int)sizeof line - 80) break;
    }
    Log("ragdoll: valores em uso pelo Ragdoll:%s", line);
}

// tudo de uma vez, alguns segundos depois do CJ aparecer (vale para jogo novo e save)
static void RagdollStartupCheck() {
    static DWORD s_at = 0; static bool s_done = false;
    if (s_done) return;
    if (!s_at) { s_at = GetTickCount() + 3000; return; }
    if (GetTickCount() < s_at) return;
    s_done = true;
    ListMods();
    RagdollConfig();
    CheckRagdollHooks(true, "3 s depois do CJ aparecer");
    if (g_countRagdoll) { InstallRdCounters(); CheckRagdollHooks(false, "com contadores"); }
}

// ossos do pedestre: posicao do primeiro osso, maior distancia de um osso ao corpo,
// espalhamento entre ossos e quantos valores invalidos (NaN)
static void BoneInfo(const char* p, V3 q, char* out, int n) {
    void* clump = Rd<void*>(p, 0x18);
    if (!clump) { snprintf(out, n, " ossos -"); return; }
    char* h = (char*)((void* (__cdecl*)(void*))0x734A40)(clump);    // GetAnimHierarchyFromSkinClump
    if (!h) { snprintf(out, n, " ossos sem-hierarquia"); return; }
    int nb = Rd<int>(h, 4);
    const char* mats = Rd<const char*>(h, 8);
    if (!mats || nb <= 0 || nb > 64) { snprintf(out, n, " ossos invalidos(%d)", nb); return; }
    const float* r = (const float*)(mats + 48);
    float farD = 0.f, spread = 0.f; int nan = 0;
    for (int b = 0; b < nb; b++) {
        const float* m = (const float*)(mats + b * 64);
        for (int k = 0; k < 16; k++) if (m[k] != m[k]) nan++;
        float x = m[12], y = m[13], z = m[14];
        float d = sqrtf((x - q.x) * (x - q.x) + (y - q.y) * (y - q.y) + (z - q.z) * (z - q.z));
        float e = sqrtf((x - r[0]) * (x - r[0]) + (y - r[1]) * (y - r[1]) + (z - r[2]) * (z - r[2]));
        if (d > farD || d != d) farD = d;
        if (e > spread || e != e) spread = e;
    }
    snprintf(out, n, " ossos %d raiz (%.2f, %.2f, %.2f) longe %.1f esp %.1f nan %d fl 0x%X",
             nb, r[0], r[1], r[2], farD, spread, nan, Rd<int>(h, 0));
}

// Acompanha os pedestres perto do CJ: quando um morre ou "salta" mais de 3 m num quadro,
// registra posicao, velocidade, estado e visibilidade quadro a quadro por alguns segundos.
struct PedTrack { bool have, alive; V3 prev; DWORD until; DWORD start; };
static PedTrack g_ped[256];
static int g_ragLines = 0;
static void RagdollTrace() {
    char* player = PlayerPed();
    if (!player || g_ragLines > 9000) return;
    RagdollStartupCheck();
    static DWORD s_countAt = 0;
    if (g_rdCounting && !s_countAt) s_countAt = GetTickCount() + 10000;
    if (s_countAt && GetTickCount() >= s_countAt) { s_countAt = 0xFFFFFFFF; PrintRdCounts("10 s depois de instalar"); }
    const char* pool = *(const char* volatile*)0xB74490;            // CPools::ms_pPedPool
    if (!pool) return;
    const char* objs = Rd<const char*>(pool, 0);
    const BYTE* map  = Rd<const BYTE*>(pool, 4);
    int size = Rd<int>(pool, 8);
    if (!objs || !map || size <= 0 || size > 256) return;
    V3 pp = EntityPos(player);
    DWORD now = GetTickCount();
    unsigned frame = *(volatile unsigned*)0xB7CB4C;                  // CTimer::m_FrameCounter
    for (int i = 0; i < size; i++) {
        PedTrack& t = g_ped[i];
        if (map[i] & 0x80) { t.have = false; continue; }
        const char* p = objs + i * 0x7C4;
        if (p == player) continue;
        V3 q = EntityPos(p);
        float hp = Rd<float>(p, 0x540);
        int st = Rd<int>(p, 0x530);
        bool dead = hp <= 0.f || (st >= 54 && st <= 56);
        float dx = q.x - pp.x, dy = q.y - pp.y, dz = q.z - pp.z;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (!t.have) { t = {}; t.have = true; t.prev = q; t.alive = !dead; continue; }
        float jx = q.x - t.prev.x, jy = q.y - t.prev.y, jz = q.z - t.prev.z;
        float jump = sqrtf(jx * jx + jy * jy + jz * jz);
        bool nearP = dist < 60.f || (t.until && now < t.until);
        if (nearP && t.alive && dead) {
            t.until = now + 5000; t.start = now;
            Log("ragdoll: pedestre #%d (modelo %d) morreu em (%.2f, %.2f, %.2f) a %.1f m do CJ; estado %d vida %.1f",
                i, (int)Rd<short>(p, 0x22), q.x, q.y, q.z, dist, st, hp);
            g_ragLines++;
            CheckRagdollHooks(false, "morte");
            PrintRdCounts("na morte");
        }
        if (nearP && (jump > 3.f || q.x != q.x)) {
            if (!t.until || now >= t.until) t.start = now;
            t.until = now + 4000;
            Log("ragdoll: pedestre #%d SALTOU %.1f m num quadro: (%.2f, %.2f, %.2f) -> (%.2f, %.2f, %.2f)",
                i, jump, t.prev.x, t.prev.y, t.prev.z, q.x, q.y, q.z);
            g_ragLines++;
        }
        if (t.until && now < t.until) {
            float vx = Rd<float>(p, 0x44), vy = Rd<float>(p, 0x48), vz = Rd<float>(p, 0x4C);
            bool vis = (Rd<BYTE>(p, 0x1C) & 0x80) != 0;
            bool hasMat = Rd<const void*>(p, 0x14) != nullptr;
            char bones[128]; BoneInfo(p, q, bones, sizeof bones);
            Log("rd #%d t=%4lu q=%u pos (%.2f, %.2f, %.2f) vel (%.3f, %.3f, %.3f) dist %.1f est %d vida %.1f vis %d mat %d%s",
                i, (unsigned long)(now - t.start), frame, q.x, q.y, q.z, vx, vy, vz, dist, st, hp, vis ? 1 : 0, hasMat ? 1 : 0, bones);
            g_ragLines++;
        }
        t.prev = q;
        t.alive = !dead;
    }
    if (g_ragLines > 9000) Log("ragdoll: limite de linhas atingido; diagnostico parado");
}

// ---------------------------------------------------------------- gancho por quadro
static Void_t g_origFrameCall = nullptr;
static const DWORD kFrameCall = 0x53C077;              // CALL CCranes::UpdateCranes em CGame::Process (todo quadro sem pausa)
static bool g_firstFrame = true, g_fallbackNoted = false, g_lastYN = false;

static void WeaponGuardTick();
static void SkinVoiceTick();
static void __cdecl FrameTick() {
    if (g_origFrameCall) g_origFrameCall();
    if (g_firstFrame) {                                 // reserva, se a thread de inicio nao conseguiu
        g_firstFrame = false;
        X87Check();
        if (g_hookCount == 0) InstallXInputHooks();
        if (g_hookCount == 0) Log("nenhuma funcao XInput interceptada");
    }
    ComputeInfo(g_info);
    g_ctxCache = g_info.ctx;
    if (g_weaponGuard) WeaponGuardTick();
    if (g_skinVoice) SkinVoiceTick();
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
    if (g_traceRagdoll) RagdollTrace();

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

// ---------------------------------------------------------------- radar no alto ([Hud] RadarTop=16)
// O jogo desenha o radar a SCREEN_HEIGHT - 104 * (altura/448) (a borda de cima do radar, 104 unidades
// acima do fundo numa tela de 448). Como SCREEN_HEIGHT = 448 unidades, trocar 104 por (448 - T) poe a
// borda de cima a T unidades do topo. O 104.0f (0x866B70) tambem serve ao nome do bairro, ao nome do
// veiculo e ao "pular viagem" (canto de baixo a direita), entao so as instrucoes do radar passam a ler
// um float nosso: CRadar::TransformRadarPointToScreenSpace (mapa, icones, norte) e CHud::DrawRadar
// (moldura, aro do aviao, altimetro). O painel de status do TAB (CHud::DrawVitalStats, 140 do fundo,
// 0x865374, que tambem e usado em outro lugar) vai para logo abaixo do radar.
static float g_radarK = 104.0f, g_statsK = 140.0f;
// tudo ou nada: se algum ponto nao for o original (outro mod mexeu), nao muda nenhum, para o radar nao
// ficar com metade em cima e metade embaixo
static int PatchFloatRefs(const DWORD* sites, int n, DWORD oldAddr, float* newVar) {
    for (int i = 0; i < n; i++) {
        BYTE* p = (BYTE*)sites[i];
        bool op = (p[0] == 0xD8 && p[1] == 0x0D) || (p[0] == 0xD9 && p[1] == 0x05);   // fmul / fld dword [abs]
        if (!op || *(DWORD*)(p + 2) != oldAddr) { Log("radar: 0x%X nao e o codigo original; nada foi mudado neste grupo", sites[i]); return 0; }
    }
    int ok = 0;
    for (int i = 0; i < n; i++) {
        BYTE* p = (BYTE*)sites[i];
        DWORD old; VirtualProtect(p + 2, 4, PAGE_EXECUTE_READWRITE, &old);
        *(DWORD*)(p + 2) = (DWORD)newVar;
        VirtualProtect(p + 2, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, 6);
        ok++;
    }
    return ok;
}
static void ApplyRadarTop(int top) {
    if (top < 0) top = 0;
    if (top > 300) top = 300;
    g_radarK = 448.0f - (float)top;
    g_statsK = 448.0f - (float)(top + 84);                     // radar tem 76 de altura + folga
    static const DWORD radarSites[] = { 0x5834FE, 0x58A497, 0x58A60C, 0x58A71C, 0x58A7C5, 0x58A866, 0x58A911, 0x58A9C5 };
    static const DWORD statsSites[] = { 0x589799, 0x58981B, 0x58984D, 0x5898C5 };
    int r = PatchFloatRefs(radarSites, 8, 0x866B70, &g_radarK);
    int st = PatchFloatRefs(statsSites, 4, 0x865374, &g_statsK);
    Log("radar: no alto (borda a %d de 448), %d/8 pontos do radar e %d/4 do painel de status", top, r, st);
}

// ---------------------------------------------------------------- pular os videos de abertura ([Fixes] SkipIntro=1)
// O laco principal (0x748A90) passa pelos estados 1 a 4: toca movies\Logo.mpg, espera acabar ou uma tecla,
// toca movies\GTAtitles.mpg e espera de novo. Sem os arquivos, o video nunca "acaba" e o jogo fica parado
// esperando um toque. O estado 1 passa a ir direto para o 5 (inicializacao e menu), sem tocar nada.
static void ApplySkipIntro() {
    static const BYTE orig[] = { 0x39,0x1D,0xCC,0x20,0xC9,0x00, 0x75,0x0E, 0x68,0x04,0x4C,0x87,0x00, 0x56, 0xE8 };
    BYTE repl[15];
    repl[0] = 0xC7; repl[1] = 0x05; *(DWORD*)(repl + 2) = 0xC8D4C0; *(DWORD*)(repl + 6) = 5;  // mov [gGameState], 5
    repl[10] = 0xE9; *(int*)(repl + 11) = (int)(0x748DA3 - (0x748AF2 + 15));                    // jmp fim do switch
    if (memcmp((void*)0x748AF2, orig, sizeof orig)) { Log("abertura: codigo em 0x748AF2 nao e o original, nao mexi"); return; }
    DWORD old; VirtualProtect((void*)0x748AF2, sizeof repl, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)0x748AF2, repl, sizeof repl);
    VirtualProtect((void*)0x748AF2, sizeof repl, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)0x748AF2, sizeof repl);
    Log("abertura: videos pulados (direto para o menu)");
}


// ---------------------------------------------------------------- protecao da arma ([Fixes] WeaponGuard=1)
// Se o modelo de uma arma do CJ ou o pacote de animacoes dela sai da memoria, o icone some, o CJ fica de
// maos vazias e, ao mirar, o jogo trava: CAnimBlendAssocGroup::GetAnimation (0x4CE090) devolve um ponteiro
// a partir de zero e CAnimManager::BlendAnimation le o endereco 0xA (0x4D464E).
// 1) GetAnimation e CopyAnimation de um grupo que nao esta na memoria passam a devolver a animacao parada do grupo padrao
//    (que esta sempre carregado) em vez de um ponteiro invalido, e anota o grupo.
// 2) A cada quadro: pede de novo ao jogo o pacote anotado e os modelos das armas do CJ que estiverem fora
//    da memoria; quando o modelo da arma na mao volta, poe a arma de volta na mao.
static volatile BYTE g_animMiss[256];
static volatile int  g_animMissAny = 0;
extern "C" void* __cdecl AnimMiss(char* group, int id) {
    char* base = *(char**)0xB4EA34;                                   // CAnimManager::ms_aAnimAssocGroups
    int g = base ? (int)(group - base) / 0x14 : -1;
    if (g >= 0 && g < 256) { g_animMiss[g] = 1; g_animMissAny = 1; }
    if (base) {
        char* a0 = *(char**)(base + 4); int n0 = *(int*)(base + 8), off0 = *(int*)(base + 0xC);
        int i = 3 - off0;                                              // ANIM_ID_IDLE do grupo padrao
        if (a0 && i >= 0 && i < n0) return a0 + i * 0x14;
    }
    return (char*)0 + (id - *(int*)(group + 0xC)) * 0x14;              // como o original
}
extern "C" void AnimGetStub();
asm(".text\n.globl _AnimGetStub\n_AnimGetStub:\n"
    "  movl 4(%ecx), %eax\n  testl %eax, %eax\n  jz 1f\n"
    "  movl 4(%esp), %edx\n  subl 12(%ecx), %edx\n  leal (%edx,%edx,4), %edx\n  leal (%eax,%edx,4), %eax\n  ret $4\n"
    "1:\n  pushl 4(%esp)\n  pushl %ecx\n  call _AnimMiss\n  addl $8, %esp\n  ret $4\n");

static bool PatchBytes(DWORD addr, const BYTE* expect, int nExpect, const BYTE* repl, int nRepl, const char* what);
static void ApplyWeaponGuard() {
    static const BYTE orig[] = { 0x8B,0x51,0x0C, 0x8B,0x44,0x24,0x04, 0x8B,0x49,0x04, 0x2B,0xC2, 0x8D,0x04,0x80, 0x8D,0x04,0x81, 0xC2,0x04,0x00 };
    BYTE repl[5]; repl[0] = 0xE9; *(int*)(repl + 1) = (int)((DWORD)&AnimGetStub - (0x4CE090 + 5));
    if (!PatchBytes(0x4CE090, orig, sizeof orig, repl, sizeof repl, "arma: protecao das animacoes (GetAnimation)")) return;
    // CAnimBlendAssocGroup::CopyAnimation (0x4CE130) faz a mesma conta por dentro: passa a usar a de cima
    static const BYTE orig2[] = { 0x8B,0x51,0x0C, 0x8B,0x49,0x04, 0x2B,0xC2, 0x8D,0x04,0x80, 0x56, 0x8D,0x34,0x81 };
    BYTE r2[9];
    r2[0] = 0x56;                                                      // push esi
    r2[1] = 0x50;                                                      // push eax (id)
    r2[2] = 0xE8; *(int*)(r2 + 3) = (int)((DWORD)&AnimGetStub - (0x4CE149 + 2 + 5));   // call (ecx = grupo)
    r2[7] = 0x89; r2[8] = 0xC6;                                        // mov esi, eax
    PatchBytes(0x4CE149, orig2, sizeof orig2, r2, sizeof r2, "arma: protecao das animacoes (CopyAnimation)");
}

typedef int  (__cdecl *AnimBlockIndex_t)(const char* name);
static const AnimBlockIndex_t GameAnimBlockIndex = (AnimBlockIndex_t)0x4D3990;  // CAnimManager::GetAnimationBlockIndex
static bool ModelLoaded(int m) { return *(volatile BYTE*)(0x8E4CC0 + m * 0x14 + 0x10) == 1; }   // ms_aInfoForModel[m].m_nLoadState
static const int kKeep = 0x02 | 0x10;                                    // GAME_REQUIRED | PRIORITY_REQUEST

static void AddWeaponModel(char* ped, int model) {
    __asm__ volatile("pushl %1\n\tcall *%2" : "+c"(ped) : "r"(model), "r"((DWORD)0x5E5ED0) : "eax", "edx", "memory");
}

static DWORD g_animMissLog[256], g_weapReq[64], g_weapSince[64];
static int   g_weapFix = 0;                                              // tipo da arma esperando voltar para a mao

static void WeaponGuardTick() {
    DWORD now = GetTickCount();
    if (g_animMissAny) {
        g_animMissAny = 0;
        for (int g = 0; g < 256; g++) {
            if (!g_animMiss[g]) continue;
            g_animMiss[g] = 0;
            const char* block = (const char*)(0x8AA5B8 + g * 0x30);       // ms_aAnimAssocDefinitions[g].blockName
            int idx = GameAnimBlockIndex(block);
            bool loaded = idx >= 0 && *(volatile BYTE*)(0xB5D4B0 + idx * 0x20);   // ms_aAnimBlocks[idx].bLoaded
            if (idx >= 0 && !loaded) RequestModel(idx + 25575, kKeep);
            if (now - g_animMissLog[g] > 10000 || !g_animMissLog[g]) {
                g_animMissLog[g] = now;
                Log("arma: o jogo pediu as animacoes do grupo %d (%.16s) sem elas estarem na memoria; usei a animacao parada no lugar%s",
                    g, block, idx >= 0 && !loaded ? " e pedi o pacote de novo" : "");
            }
        }
    }
    char* ped = PlayerPed();
    if (!ped) return;
    int cur = (signed char)Rd<BYTE>(ped, 0x718);                           // m_nActiveWeaponSlot
    for (int slot = 0; slot < 13; slot++) {
        int type = Rd<int>(ped, 0x5A0 + slot * 0x1C);                      // m_aWeapons[slot].m_eWeaponType
        if (type <= 0 || type > 46) continue;
        const char* info = (const char*)(0xC8AAB8 + type * 0x70);          // aWeaponInfo[type] (habilidade normal)
        bool missing = false;
        for (int k = 0; k < 2; k++) {
            int m = *(const int*)(info + 0xC + k * 4);                     // m_nModelId1 / m_nModelId2
            if (m > 0 && m < 20000 && !ModelLoaded(m)) missing = true;
        }
        if (!missing) { g_weapSince[type] = 0; g_weapReq[type] = 0; continue; }
        if (!g_weapSince[type]) g_weapSince[type] = now;
        if (now - g_weapSince[type] < 1000) continue;                       // logo depois de carregar o jogo e normal
        if (slot == cur) g_weapFix = type;
        if (g_weapReq[type] && now - g_weapReq[type] < 500) continue;
        for (int k = 0; k < 2; k++) {
            int m = *(const int*)(info + 0xC + k * 4);
            if (m <= 0 || m >= 20000 || ModelLoaded(m)) continue;
            if (!g_weapReq[type]) Log("arma: o modelo %d da arma %d (posicao %d) nao estava na memoria; pedi de novo", m, type, slot);
            RequestModel(m, kKeep);
        }
        g_weapReq[type] = now;
    }
    if (g_weapFix && cur >= 0 && cur < 13) {
        int type = Rd<int>(ped, 0x5A0 + cur * 0x1C);
        if (type != g_weapFix) { g_weapFix = 0; return; }                  // trocou de arma: o jogo cuida
        int m = *(const int*)(0xC8AAB8 + type * 0x70 + 0xC);
        if (g_info.ctx != CTX_FOOT || m <= 0 || !ModelLoaded(m)) return;
        if (!Rd<char*>(ped, 0x4F4) && !Rd<BYTE>(ped, 0x5A0 + cur * 0x1C + 0x15)) {   // m_pWeaponObject, m_bNoModel
            AddWeaponModel(ped, m);
            Log("arma: modelo da arma %d de volta na mao", type);
        }
        g_weapFix = 0;
    }
}

// ---------------------------------------------------------------- animacoes e voz da skin ([Skins] Anims=1 Voice=1)
// Animacoes: CPlayerPed::ProcessAnimGroups (0x6098F0, todo quadro) escolhe o jeito de andar do jogador. Para o
// jogador 1 usa sempre o do CJ pelos status (normal, gordo ou forte); so o jogador 2 do modo de dois jogadores
// usa o do modelo (m_nAnimType). Os desvios em 0x609A1A/0x609A1F fazem o jogador 1 com uma skin usar o jeito de
// andar do modelo dela (os do animgrp.dat: man, woman, gang1...; as mesmas 6 animacoes do grupo do CJ). Com o
// modelo do CJ, nas armas pesadas, de jetpack etc. o caminho original continua. Se o pacote do jeito de andar
// nao estiver na memoria, pede de novo e usa o do CJ ate ele chegar.
// Voz: CAEPedSpeechAudioEntity::Initialise (0x4E68D0) escolhe a voz pelo modelo, mas o jogo so chama ao criar o
// personagem. Quando o modelo do CJ muda, chama de novo; se o modelo nao tem voz, volta a voz anterior.
static DWORD g_skinAnimReq[160];
extern "C" int __cdecl SkinAnimGroup(char* ped) {
    if (!g_skinAnims || !ped || Rd<int>(ped, 0x598) != 0) return -1;         // so o jogador 1 (m_nPedType)
    int model = Rd<short>(ped, 0x22);
    if (model <= 0 || model >= 20000) return -1;                              // 0 = CJ
    const char* mi = ((char**)0xA9B0C8)[model];                               // CModelInfo::ms_modelInfoPtrs
    if (!mi) return -1;
    int g = Rd<int>(mi, 0x24);                                                // CPedModelInfo::m_nAnimType
    if (g < 0x76 || g >= 145) return -1;                                      // so os jeitos de andar do animgrp.dat
    const char* base = *(char**)0xB4EA34;                                     // CAnimManager::ms_aAnimAssocGroups
    if (!base) return -1;
    const char* grp = base + g * 0x14;
    if (Rd<int>(grp, 8) != 6) return -1;                                      // m_nNumAnimations do ciclo de andar
    if (!Rd<char*>(grp, 4)) {                                                 // pacote fora da memoria
        DWORD now = GetTickCount();
        if (now - g_skinAnimReq[g] > 1000) {
            const char* block = (const char*)(0x8AA5B8 + g * 0x30);           // ms_aAnimAssocDefinitions[g].blockName
            int idx = block[0] ? GameAnimBlockIndex(block) : -1;
            if (idx >= 0) RequestModel(idx + 25575, kKeep);
            if (!g_skinAnimReq[g]) Log("skins: animacoes do grupo %d (%.16s) fora da memoria; pedi o pacote", g, block);
            g_skinAnimReq[g] = now;
        }
        return -1;
    }
    return g;
}
// entrada em 0x609A1A: edi = grupo atual (caminho em que o jogo mantem o grupo)
extern "C" int __cdecl SkinAnimGroupKeep(char* ped, int cur) {
    int g = SkinAnimGroup(ped);
    if (g >= 0 && (cur == 0x36 || cur == g)) return g;
    if (cur >= 0x76) return -2;                                               // sobrou o andar de uma skin: volta ao do CJ
    return -1;
}
extern "C" void AnimGroupStubA();
extern "C" void AnimGroupStubB();
asm(".text\n"
    ".globl _AnimGroupStubA\n_AnimGroupStubA:\n"
    "  pushl %edi\n  pushl %esi\n  call _SkinAnimGroupKeep\n  addl $8, %esp\n"
    "  testl %eax, %eax\n  jge 2f\n"
    "  cmpl $-2, %eax\n  jne 1f\n  movl $0x36, %edi\n"
    "1:\n  cmpl $0x36, %edi\n  jne 3f\n"                                     // original: cmp edi, 0x36 / jne 0x609A3B
    ".globl _AnimGroupStubB\n_AnimGroupStubB:\n"
    "  pushl %esi\n  call _SkinAnimGroup\n  addl $4, %esp\n"
    "  testl %eax, %eax\n  jge 2f\n"
    "  cmpl $1, 0x598(%esi)\n  jne 3f\n"                                      // original: jogador 2 usa o do modelo
    "  pushl $0x609A28\n  ret\n"
    "2:\n  pushl $0x609A44\n  ret\n"                                          // grava eax como grupo e reaplica
    "3:\n  pushl $0x609A3B\n  ret\n");                                        // grupo do CJ pelos status

static void ApplySkinAnims() {
    static const BYTE orig[] = { 0x83,0xFF,0x36, 0x75,0x1C, 0x83,0xBE,0x98,0x05,0x00,0x00,0x01, 0x75,0x13 };
    BYTE r[10];
    r[0] = 0xE9; *(int*)(r + 1) = (int)((DWORD)&AnimGroupStubA - (0x609A1A + 5));
    r[5] = 0xE9; *(int*)(r + 6) = (int)((DWORD)&AnimGroupStubB - (0x609A1F + 5));
    PatchBytes(0x609A1A, orig, sizeof orig, r, sizeof r, "skins: jeito de andar da skin (CPlayerPed::ProcessAnimGroups)");
}

typedef void (__thiscall* SpeechInit_t)(void* speech, void* entity);
static const SpeechInit_t SpeechInit = (SpeechInit_t)0x4E68D0;               // CAEPedSpeechAudioEntity::Initialise
static char* g_voicePed = nullptr;
static int   g_voiceModel = 0;
static bool  g_voiceChanged = false;
static void SkinVoiceTick() {
    char* ped = PlayerPed();
    if (!ped) return;
    if (ped != g_voicePed) { g_voicePed = ped; g_voiceModel = 0; g_voiceChanged = false; }   // CJ novo (jogo carregado): voz do CJ
    int model = Rd<short>(ped, 0x22);
    if (model == g_voiceModel) return;
    if (model == 0 && !g_voiceChanged) { g_voiceModel = 0; return; }
    char* sp = ped + 0x294;                                                   // CPed::m_pedSpeech (0x100 bytes)
    if (Rd<BYTE>(sp, 0x98)) return;                                           // falando agora: espera terminar
    g_voiceModel = model;
    BYTE snap[0x100]; memcpy(snap, sp, sizeof snap);
    sp[0x90] = 0;
    SpeechInit(sp, ped);
    if (!sp[0x90]) {                                                          // o modelo nao tem voz: fica a anterior
        memcpy(sp, snap, sizeof snap);
        Log("skins: o modelo %d nao tem voz propria; ficou a voz anterior", model);
        return;
    }
    g_voiceChanged = model != 0;
    Log("skins: voz do modelo %d (tipo %d, voz %d)", model, Rd<short>(sp, 0x92), Rd<short>(sp, 0x94));
}

// ---------------------------------------------------------------- precisao do x87 ([Fixes] X87=1)
// No Windows, o Direct3D 9 poe o x87 do jogo em precisao simples (24 bits) e o jogo depende disso.
// No iPhone o emulador (FEX) calcula com mais precisao e ignora esse ajuste. Dois lugares quebram:
//  1) 0x4D9CC0 CAEAudioUtility::GetBankAndSoundFromScriptSlotAudioEvent: banco = floor((id-2000)*0.005f).
//     Com mais precisao, 200*0.005f da 0,99999998 em vez de 1, e todo som de numero 0 de um banco
//     (multiplo de 200) vem do banco anterior: fala trocada do Ryder, telefone com fala de pedestre,
//     musica da festa no carro do Smoke, maquina do barbeiro muda. Troca por divisao inteira por 200,
//     que da exatamente o resultado do Windows.
//  2) 0x72B89D CWeather::Update: compara o valor novo da interpolacao (ainda no registrador, com mais
//     precisao) com o valor guardado em float. Quando o arredondamento do float sobe, o jogo acha que a
//     hora virou e troca o clima: o clima cicla sozinho. Passa a comparar o valor ja arredondado em float.
static bool PatchBytes(DWORD addr, const BYTE* expect, int nExpect, const BYTE* repl, int nRepl, const char* what) {
    if (memcmp((void*)addr, expect, nExpect)) {
        char got[64]; int o = 0;
        for (int i = 0; i < nExpect && i < 16; i++) o += snprintf(got + o, sizeof got - o, "%02X", ((BYTE*)addr)[i]);
        Log("%s nao aplicada: codigo em 0x%X nao e o original (%s...)", what, addr, got);
        return false;
    }
    DWORD old;
    VirtualProtect((void*)addr, nExpect, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)addr, repl, nRepl);
    for (int i = nRepl; i < nExpect; i++) ((BYTE*)addr)[i] = 0x90;
    VirtualProtect((void*)addr, nExpect, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, nExpect);
    Log("%s aplicada em 0x%X", what, addr);
    return true;
}

static void ApplyX87Fixes() {
    // 1) som de missao: 0x4D9D38..0x4D9D81
    static const BYTE bankOrig[] = {
        0x05,0x30,0xF8,0xFF,0xFF, 0x89,0x44,0x24,0x08, 0xDB,0x44,0x24,0x08, 0x83,0xEC,0x08,
        0xD8,0x0D,0x4C,0x8B,0x85,0x00, 0xDD,0x1C,0x24, 0xE8,0x9A,0x7C,0x34,0x00, 0x83,0xC4,0x08,
        0xE8,0xE2,0x7D,0x34,0x00, 0x8B,0x4C,0x24,0x0C, 0x05,0x93,0x00,0x00,0x00, 0x89,0x01,
        0x8B,0x06, 0x2D,0xD0,0x07,0x00,0x00, 0x99, 0xB9,0xC8,0x00,0x00,0x00, 0xF7,0xF9,
        0x8B,0x44,0x24,0x10, 0x5E, 0x89,0x10, 0xB0,0x01, 0xC3 };
    static const BYTE bankNew[] = {
        0x2D,0xD0,0x07,0x00,0x00,       // sub  eax, 2000
        0x99,                           // cdq
        0xB9,0xC8,0x00,0x00,0x00,       // mov  ecx, 200
        0xF7,0xF9,                      // idiv ecx
        0x05,0x93,0x00,0x00,0x00,       // add  eax, 0x93 (primeiro banco de script)
        0x8B,0x4C,0x24,0x0C,            // mov  ecx, [esp+0xC] (banco)
        0x89,0x01,                      // mov  [ecx], eax
        0x8B,0x44,0x24,0x10,            // mov  eax, [esp+0x10] (som)
        0x89,0x10,                      // mov  [eax], edx
        0xB0,0x01,                      // mov  al, 1
        0x5E,                           // pop  esi
        0xC3 };                         // ret
    PatchBytes(0x4D9D38, bankOrig, sizeof bankOrig, bankNew, sizeof bankNew, "x87: correcao do som de missao");

    // 2) clima: fst [esp+8] ; fcomp [InterpolationValue]  ->  call stub ; nop x5
    static BYTE* stub = nullptr;
    if (!stub) stub = (BYTE*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!stub) { Log("x87: sem memoria para a correcao do clima"); return; }
    static const BYTE stubCode[] = {
        0xD9,0x5C,0x24,0x0C,            // fstp dword [esp+0xC]  (o [esp+8] de quem chamou)
        0xD9,0x44,0x24,0x0C,            // fld  dword [esp+0xC]  (o mesmo valor, ja arredondado em float)
        0xD8,0x1D,0x0C,0x13,0xC8,0x00,  // fcomp dword [0xC8130C] (CWeather::InterpolationValue)
        0xC3 };                         // ret
    memcpy(stub, stubCode, sizeof stubCode);
    FlushInstructionCache(GetCurrentProcess(), stub, sizeof stubCode);
    static const BYTE wOrig[] = { 0xD9,0x54,0x24,0x08, 0xD8,0x1D,0x0C,0x13,0xC8,0x00 };
    BYTE wNew[5]; wNew[0] = 0xE8; *(int*)(wNew + 1) = (int)((DWORD)stub - (0x72B89D + 5));
    PatchBytes(0x72B89D, wOrig, sizeof wOrig, wNew, sizeof wNew, "x87: correcao do clima");
}

// confere, na thread do jogo, se o x87 arredonda como no Windows
static void X87Check() {
    unsigned short cw = 0; double r = 0; int x = 200; float c = 0.005f;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    __asm__ volatile("fildl %1\n\tfmuls %2\n\tfstpl %0" : "=m"(r) : "m"(x), "m"(c));
    Log("x87: palavra de controle 0x%04X (precisao %s); 200 x 0,005f = %.10f (%s)", cw,
        ((cw >> 8) & 3) == 0 ? "simples, 24 bits" : ((cw >> 8) & 3) == 2 ? "dupla, 53 bits" : "estendida, 64 bits",
        r, r >= 1.0 ? "como no Windows" : "diferente do Windows: as correcoes x87 sao necessarias aqui");
}

// ---------------------------------------------------------------- diagnostico do audio ([Debug] AudioHash=1)
// Para cada arquivo de audio\CONFIG e audio\SFX: tamanho exato, CRC32 do arquivo inteiro e de cada bloco
// de 64 MB (lidos do jeito simples). Depois, nos arquivos de SFX, le 64 trechos de cada um tambem do jeito
// que o jogo le (FILE_FLAG_OVERLAPPED + NO_BUFFERING, posicao no OVERLAPPED, sem evento) e compara com a
// leitura simples: se divergir, quem le errado e o sistema (Madeira/Wine), nao o arquivo.
static uint32_t g_crcTab[256];
static void CrcInit() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        g_crcTab[i] = c;
    }
}
static uint32_t CrcAdd(uint32_t c, const unsigned char* p, DWORD n) {
    c = ~c;
    while (n--) c = g_crcTab[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}

static void HashOneFile(const wchar_t* path, const char* shown) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) { Log("audio: %s nao abriu (erro %lu)", shown, GetLastError()); return; }
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    const DWORD BUF = 1 << 20;
    const long long BLOCK = 64ll << 20;
    unsigned char* b = (unsigned char*)VirtualAlloc(nullptr, BUF, MEM_COMMIT, PAGE_READWRITE);
    uint32_t whole = 0, blk = 0; long long done = 0, inBlk = 0; DWORD t0 = GetTickCount();
    char blocks[1600]; int bo = 0; blocks[0] = 0;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(h, b, BUF, &got, nullptr)) { Log("audio: %s erro de leitura %lu em %lld", shown, GetLastError(), done); break; }
        if (!got) break;
        whole = CrcAdd(whole, b, got);
        DWORD off = 0;
        while (off < got) {
            long long room = BLOCK - inBlk; DWORD take = (long long)(got - off) < room ? (got - off) : (DWORD)room;
            blk = CrcAdd(blk, b + off, take);
            off += take; inBlk += take;
            if (inBlk == BLOCK) {
                if (bo < (int)sizeof blocks - 12) bo += snprintf(blocks + bo, sizeof blocks - bo, " %08X", blk);
                blk = 0; inBlk = 0;
            }
        }
        done += got;
    }
    if (inBlk && bo < (int)sizeof blocks - 12) bo += snprintf(blocks + bo, sizeof blocks - bo, " %08X", blk);
    VirtualFree(b, 0, MEM_RELEASE);
    CloseHandle(h);
    Log("audio: %s tamanho %lld lidos %lld crc %08X (%lu ms) blocos de 64 MB:%s",
        shown, sz.QuadPart, done, whole, GetTickCount() - t0, blocks);
}

// le o mesmo trecho dos dois jeitos e compara
static void CompareReads(const wchar_t* path, const char* shown) {
    HANDLE hs = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    HANDLE ho = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING, nullptr);
    if (hs == INVALID_HANDLE_VALUE || ho == INVALID_HANDLE_VALUE) {
        Log("audio: %s comparacao nao abriu (erro %lu)", shown, GetLastError());
        if (hs != INVALID_HANDLE_VALUE) CloseHandle(hs);
        if (ho != INVALID_HANDLE_VALUE) CloseHandle(ho);
        return;
    }
    LARGE_INTEGER sz; GetFileSizeEx(hs, &sz);
    const DWORD LEN = 32768;
    unsigned char* a = (unsigned char*)VirtualAlloc(nullptr, LEN, MEM_COMMIT, PAGE_READWRITE);
    unsigned char* o = (unsigned char*)VirtualAlloc(nullptr, LEN, MEM_COMMIT, PAGE_READWRITE);
    long long sectors = sz.QuadPart / 2048;
    int bad = 0, tested = 0; uint32_t seed = 12345;
    for (int i = 0; i < 64 && sectors > 16; i++) {
        long long sec;
        if (i == 0) sec = 0;
        else if (i == 1) sec = sectors - 16;
        else { seed = seed * 1103515245u + 12345u; sec = (long long)(((unsigned long long)seed << 15 ^ (seed >> 3)) % (unsigned long long)(sectors - 16)); }
        long long pos = sec * 2048;
        LARGE_INTEGER li; li.QuadPart = pos;
        SetFilePointerEx(hs, li, nullptr, FILE_BEGIN);
        DWORD gs = 0; ReadFile(hs, a, LEN, &gs, nullptr);
        memset(o, 0xCD, LEN);
        OVERLAPPED ov = {}; ov.Offset = (DWORD)pos; ov.OffsetHigh = (DWORD)(pos >> 32);
        DWORD go = 0;
        BOOL ok = ReadFile(ho, o, LEN, &go, &ov);
        DWORD err = ok ? 0 : GetLastError();
        if (!ok && err == ERROR_IO_PENDING) { ok = GetOverlappedResult(ho, &ov, &go, TRUE); err = ok ? 0 : GetLastError(); }
        tested++;
        if (!ok || go != gs || memcmp(a, o, gs)) {
            bad++;
            if (bad <= 6) {
                DWORD firstDiff = 0; while (firstDiff < gs && a[firstDiff] == o[firstDiff]) firstDiff++;
                Log("audio: %s DIVERGE na posicao %lld: simples %lu bytes, como o jogo %lu bytes (ok=%d erro %lu), 1a diferenca no byte %lu",
                    shown, pos, gs, go, ok, err, firstDiff);
            }
        }
    }
    VirtualFree(a, 0, MEM_RELEASE); VirtualFree(o, 0, MEM_RELEASE);
    CloseHandle(hs); CloseHandle(ho);
    Log("audio: %s leitura como o jogo: %d trechos testados, %d diferentes", shown, tested, bad);
}

static DWORD WINAPI AudioHashThread(LPVOID) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    Sleep(8000);
    CrcInit();
    wchar_t root[MAX_PATH]; GetModuleFileNameW(nullptr, root, MAX_PATH);
    wchar_t* sl = wcsrchr(root, L'\\'); if (sl) sl[1] = 0;
    Log("audio: conferencia dos arquivos de audio comecou (a leitura roda em segundo plano)");
    DWORD t0 = GetTickCount();
    static const wchar_t* dirs[] = { L"audio\\CONFIG\\", L"audio\\SFX\\" };
    for (int pass = 0; pass < 2; pass++) {
        for (const wchar_t* d : dirs) {
            if (pass == 1 && d == dirs[0]) continue;
            wchar_t pat[MAX_PATH]; _snwprintf(pat, MAX_PATH, L"%s%s*", root, d);
            WIN32_FIND_DATAW fd; HANDLE f = FindFirstFileW(pat, &fd);
            if (f == INVALID_HANDLE_VALUE) { Log("audio: pasta nao encontrada (erro %lu)", GetLastError()); continue; }
            // em ordem alfabetica, para os dois logs ficarem na mesma ordem
            wchar_t names[64][MAX_PATH]; int n = 0;
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                if (n < 64) wcscpy(names[n++], fd.cFileName);
            } while (FindNextFileW(f, &fd));
            FindClose(f);
            for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++)
                if (_wcsicmp(names[j], names[i]) < 0) { wchar_t t[MAX_PATH]; wcscpy(t, names[i]); wcscpy(names[i], names[j]); wcscpy(names[j], t); }
            for (int i = 0; i < n; i++) {
                wchar_t full[MAX_PATH]; _snwprintf(full, MAX_PATH, L"%s%s%s", root, d, names[i]);
                char shown[MAX_PATH]; char dn[32];
                WideCharToMultiByte(CP_UTF8, 0, d, -1, dn, sizeof dn, nullptr, nullptr);
                char nn[MAX_PATH]; WideCharToMultiByte(CP_UTF8, 0, names[i], -1, nn, sizeof nn, nullptr, nullptr);
                snprintf(shown, sizeof shown, "%s%s", dn, nn);
                if (pass == 0) HashOneFile(full, shown); else CompareReads(full, shown);
            }
        }
    }
    Log("audio: conferencia terminou em %lu s", (GetTickCount() - t0) / 1000);
    return 0;
}

// fora do DllMain (sem o loader lock): ganchos do MinHook e XInput logo no inicio,
// para o menu principal e o de pausa ja terem controles
static DWORD WINAPI StartupThread(LPVOID) {
    HookGame(0x618E90, (void*)&AfterLoadDetour, (void**)&g_origAfterLoad, "de carregamento");
    HookGame(0x485B10, (void*)&GetPadStateDetour, (void**)&g_origGetPadState, "das setas dos scripts");
    HookGame(0x588BE0, (void*)&SetHelpDetour, (void**)&g_origSetHelp, "do texto de ajuda");
    HookGame(0x588E30, (void*)&SetHelpNumDetour, (void**)&g_origSetHelpNum, "do texto de ajuda com numero");
    InstallXInputHooks();
    if (g_audioHash) { HANDLE t = CreateThread(nullptr, 0, AudioHashThread, nullptr, 0, nullptr); if (t) CloseHandle(t); }
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
    g_traceTouch   = GetPrivateProfileIntA("Debug", "TouchTrace",   0, ini);
    g_traceRagdoll = GetPrivateProfileIntA("Debug", "RagdollTrace", 0, ini);
    g_countRagdoll = GetPrivateProfileIntA("Debug", "RagdollCount", 0, ini);
    g_audioHash    = GetPrivateProfileIntA("Debug", "AudioHash",    0, ini);
    g_fixX87       = GetPrivateProfileIntA("Fixes", "X87",          1, ini);
    g_skipIntro    = GetPrivateProfileIntA("Fixes", "SkipIntro",    1, ini);
    g_radarTop     = GetPrivateProfileIntA("Hud",   "RadarTop",     16, ini);
    g_weaponGuard  = GetPrivateProfileIntA("Fixes", "WeaponGuard",  1, ini);
    g_skinOn       = GetPrivateProfileIntA("Skins", "Menu",         1, ini);
    g_skinAnims    = GetPrivateProfileIntA("Skins", "Anims",        1, ini);
    g_skinVoice    = GetPrivateProfileIntA("Skins", "Voice",        1, ini);
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
    GetPrivateProfileStringA("Skins", "Scripts", "skin_o,skin_c,skin_n", buf, sizeof buf, ini);
    snprintf(g_skinScripts, sizeof g_skinScripts, ",%s,", buf);
    for (char* c = g_skinScripts; *c; c++) { *c = (char)tolower((unsigned char)*c); if (*c == ' ' || *c == ';') *c = ','; }
    GetPrivateProfileStringA("Action", "IgnoreScripts", "", buf, sizeof buf, ini);
    snprintf(g_ignoreTabScripts, sizeof g_ignoreTabScripts, ",%s,", buf);
    for (char* c = g_ignoreTabScripts; *c; c++) { *c = (char)tolower((unsigned char)*c); if (*c == ' ' || *c == ';') *c = ','; }
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
    Log("GTMTouchSA 3.12 carregado (direcao=%d rampa=%d retorno=%d toque=%d sim/nao por script=%d diag toque=%d ragdoll=%d contagem=%d audio=%d x87=%d pular abertura=%d radar no alto=%d protecao da arma=%d menu de skins=%d andar da skin=%d voz da skin=%d)",
        g_steerOn, g_rampMs, g_releaseMs, g_touchOn, g_ynScripts, g_traceTouch, g_traceRagdoll, g_countRagdoll, g_audioHash, g_fixX87, g_skipIntro, g_radarTop, g_weaponGuard, g_skinOn, g_skinAnims, g_skinVoice);
    if (GetModuleHandleA("GTMSteerSA.asi")) Log("AVISO: GTMSteerSA.asi tambem esta carregado; apague ele da pasta scripts");
    if (!IsSupportedExe()) { Log("executavel nao e o 1.0 US: nada foi alterado"); return TRUE; }
    OpenShared();
    InstallFrameHook();
    if (g_fixX87) ApplyX87Fixes();
    if (g_skipIntro) ApplySkipIntro();
    if (g_radarTop > 0) ApplyRadarTop(g_radarTop);
    if (g_weaponGuard) ApplyWeaponGuard();
    if (g_skinAnims) ApplySkinAnims();
    g_mhReady = (MH_Initialize() == MH_OK);
    if (!g_mhReady) { Log("MinHook nao iniciou"); return TRUE; }
    HANDLE t = CreateThread(nullptr, 0, StartupThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t); else Log("nao criou a thread de inicio (erro %lu)", GetLastError());
    return TRUE;
}
