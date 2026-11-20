// GTMTouchSA.asi 3.23 - controles de toque contextuais para GTA San Andreas 1.0 US no Madeira
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
// 5) Comandos do overlay: 1 salvar, 2 detonar as satchels, 3 voltar para a satchel, 4 recarregar, 5 centralizar.
// 6) Skin Selector (3.10/3.12): Status + Atirar abre o menu; a skin escolhida anda e fala como o
//    personagem do modelo; o save daqui grava a skin no slot do save.
// 7) Garagem pelo telefone (3.13/3.14): Status + Pular lista os carros das garagens; um membro da Grove entrega,
//    busca e leva para a garagem; venda no mercado negro; amassados do LocalDent salvos com o carro.
//    3.15: patios da policia fora. 3.16: capacidade pelo tamanho de cada garagem (o carro que nao cabe fica
//    seu, "sem vaga"), lugar livre ao voltar, e "Trocar placa" (teclado do iPhone pelo overlay 3.3, ou do PC).
// 8) Arquivo compartilhado, parte do teclado: [128..191] pedido do jogo, [192..255] resposta do overlay.
// 9) Giroscopio (3.17, overlay 3.4): [96..127] escrito pelo overlay; gira so a camera, na mira e, se quiser,
//    tambem a pe e dirigindo. 3.18/3.19: fora da mira a camera vira e fica, como o mouse (o plugin segura o
//    angulo); Olhar atras e Centralizar voltam ela para tras.
// 10) Traficante de armas (3.20): Status + Pular abre o telefone com "Carro da garagem" e "Traficante de armas";
//    ele vem numa van preta, abre as portas de tras e vende armas, municao avulsa e equipamento. Precos pela fase
//    da AmmuNation (o mesmo calculo do ammu.scm), Las Venturas libera as pesadas, a policia reage a van aberta.
//    3.21: atras da van, parado 1 s, ele pergunta "Quer comprar?" (Sim abre o menu, Nao dispensa); "Dispensar o
//    traficante" no menu; dispensado, sai com calma; as portas de tras fecham de verdade ao sair.
// 11) Dar a arma (3.21): Status + Correr, a pe, da ao pedestre na sua frente a arma que voce esta segurando
//    (municao de sobra), depois de um cumprimento de gangue.
// 3.22/3.23: Atirar aparece no drive-by de missao (o script poe o CJ atirando da garupa ou da janela; ex.: Just Business).
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
static int  g_noMouseSteer = 1, g_tsDeg = 10, g_tsStick = 1;                 // sem volante pelo mouse; trocar de alvo
static volatile SHORT g_touchRX = 0;                                         // analogico direito do overlay (troca de alvo)
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
    S_VIG          = 1u << 21,  // Vigilante rodando: botoes Reforco e Viatura (overlay 3.7, protocolo 6)
    S_VIGBACKUP    = 1u << 22,  // o reforco pode vir agora (botao aceso)
    S_VIGCAR       = 1u << 23,  // a viatura para voce pode vir agora (botao aceso)
    S_RECRUITCOP   = 1u << 24,  // mira travada num policial que da para recrutar: botao Recrutar policial
    S_VIG64        = 1u << 25,  // nivel 64: botao Vigilante (comecar/encerrar)
    S_CARWEAPONS   = 1u << 26,  // no veiculo com arma para trocar (deslizar Trocar arma, comandos 10 e 11)
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
static volatile bool  g_dealerYN = false;                                    // o traficante pergunta "Quer comprar?"
static volatile bool  g_vigYN = false, g_vigEatYN = false;                   // Vigilante: Sim/Nao chamam a policia (3.34)
static volatile WORD  g_vigRaw = 0;                                          // setas do Sim/Nao do overlay, so para nos
static volatile bool  g_rcYN = false;                                        // mira num policial: Sim recruta (3.41)
static volatile DWORD g_tripSkipTick = 0;                                    // o jogo desenhou a caixa do Trip Skip
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

static bool NewButtons() { return g_hostEverSeen && g_host.version >= 6; }   // overlay 3.7: Reforco, Viatura...
static bool NewButtonsAlive() { return NewButtons() && GetTickCount() - g_hostSeqTick < 300; }
static bool YesNoPending() {
    g_vigEatYN = false;
    if (g_dealerYN) { g_ynWhy = "traficante"; return true; }
    if (GetTickCount() - g_tripSkipTick < 500) { g_ynWhy = "trip skip"; return true; }
    if (*(volatile DWORD*)0x969A20 == 1) { g_ynWhy = "pedestre puxou conversa"; return true; }   // CPedToPlayerConversations: PEDHASOPENED
    for (int i = 0; i < 14; i++)                                                                  // CConversations: WAITINGFORINPUT
        if (*(volatile DWORD*)(0x9691D8 + i * 0x1C + 0x14) == 3) { g_ynWhy = "conversa de missao"; return true; }
    DWORD now = GetTickCount();
    if (g_helpYN) {
        bool shown = *(volatile char*)0xBAA480 != 0;                          // CHud::m_pHelpMessageToPrint
        if (now - g_helpYNTick < 15000 || (g_helpYNPerm && shown)) { g_ynWhy = "texto de ajuda"; return true; }
    }
    if (g_ynScripts && g_scriptYNTick && now - g_scriptYNTick < 600) { g_ynWhy = kWhyScript; return true; }
    if (NewButtons()) return false;                                           // overlay 3.7: botoes proprios
    if (g_rcYN) { g_ynWhy = "recrutar policial"; g_vigEatYN = true; return true; }
    if (g_vigYN) { g_ynWhy = "vigilante"; g_vigEatYN = true; return true; }
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

// drive-by de missao (TASK_DRIVE_BY no CJ, ex.: Just Business na garupa do Big Smoke): o script poe um
// CTaskSimpleGangDriveBy (tipo 0x3FE) como tarefa PRIMARIA do CJ (o handler do 0713 olha CPedIntelligence+0x10,
// isto e, CTaskManager::m_aPrimaryTasks[3], e entrega pelo 0x465C20). Enquanto ela roda, o m_bCanDoDriveBy fica zerado
// (0x621AEF), entao o teste normal de drive-by falha. Atirar vale. Olho tambem a secundaria de ataque, por garantia.
static bool GangDriveBy(char* ped) {
    char* intel = Rd<char*>(ped, 0x47C);                                     // CPedIntelligence*
    if (!intel) return false;
    typedef int (__thiscall* GetTaskType_t)(void*);
    void* tasks[2] = { Rd<void*>(intel, 0x10), Rd<void*>(intel, 0x18) };   // primaria (tm+0xC) e secundaria de ataque (tm+0x14)
    for (void* t : tasks)
        if (t && ((GetTaskType_t)(*(void***)t)[0x10 / 4])(t) == 0x3FE) return true;   // TASK_SIMPLE_GANG_DRIVEBY
    return false;
}
static DWORD VigButtonState();
static int  g_cwOn = 1, g_fwOn = 1, g_fwGlass = 1, g_cwBikeAnim = 1;         // g_cwBikeAnim: [ArmasNoVeiculo] AnimacaoNaMoto (3.48)
static int  g_vgNever = 1, g_vgBikeShoot = 1;                                // [Vigilante] NuncaProcurado, PolicialDeMotoAtira
static bool CwAllowed(int t) { return t >= 22 && t <= 33; }                    // pistolas, escopetas, SMGs, fuzis, rifle de caca
static bool CwHasAny(const char* ped) {
    if (!g_cwOn) return false;
    for (int sl = 2; sl <= 6; sl++) if (CwAllowed(WeaponType(ped, sl)) && WeaponAmmo(ped, sl) > 0) return true;
    return false;
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
    in.state |= VigButtonState();
    {
        DWORD now = GetTickCount();
        bool shown = *(volatile char*)0xBAA480 != 0;                          // CHud::m_pHelpMessageToPrint
        if (g_helpAct && shown && now - g_helpActTick < 20000) in.state |= g_helpAct;
        else if (g_scriptTabTick && now - g_scriptTabTick < 600) in.state |= S_ACTPHONE;   // script esperando o TAB
    }
    bool driveByWeapon = (CwAllowed(active) ? g_cwOn != 0 : (active == 28 || active == 29 || active == 32)) && activeAmmo > 0
                         && Rd<BYTE>(info, 0x153);                            // m_bCanDoDriveBy

    char* remote = Rd<char*>(info, 0xB0);                                     // m_pRemoteVehicle
    char* veh = remote ? remote : PlayerVehicle();
    if (veh && !remote && CwHasAny(ped)) in.state |= S_CARWEAPONS;
    if (veh && !remote && g_cwOn && CwAllowed(active) && CanReload(ped, nullptr)) in.state |= S_RELOAD;   // 3.46: toque recarrega
    if (veh) {
        int cls = Rd<int>(veh, 0x590);
        bool driver = remote || Rd<char*>(veh, 0x460) == ped;                 // m_pDriver
        if (remote) in.state |= S_REMOTE;
        bool gangDriveBy = GangDriveBy(ped);
        {
            static bool noted = false;
            if (gangDriveBy && !noted) Log("drive-by de missao: botao Atirar ligado (%s)", driver ? "dirigindo" : "passageiro");
            noted = gangDriveBy;
        }
        if (!driver) { in.ctx = CTX_PASSENGER; if (driveByWeapon) in.state |= S_FIRE | S_DRIVEBY; if (gangDriveBy) in.state |= S_FIRE; return; }
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
        if (gangDriveBy) in.state |= S_FIRE;
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
    if (tgt && (Rd<BYTE>(tgt, 0x36) & 7) == 3 && Rd<int>(tgt, 0x598) == 8 && Rd<float>(tgt, 0x540) > 0.f)
        in.state |= S_TARGETPED;                                              // so membro da Grove da para recrutar (3.43)
    if (GroupMembers(ped) > 0) in.state |= S_GROUPMEMBERS;
    if (in.state & S_RECRUITCOP) in.state &= ~(DWORD)S_TARGETPED;            // policial: o botao e o Recrutar policial
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
static int  g_skinOn = 1, g_skinAnims = 1, g_skinVoice = 1, g_skinClothes = 1;
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

static void GarageCombo(WORD buttons, PadState& p);
static void GiveCombo(WORD buttons, PadState& p);
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
        g_touchRX = h.rx;
        SkinCombo(h.buttons, p, now);
        GarageCombo(h.buttons, p);
        GiveCombo(h.buttons, p);
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
    if (host) {
        if (g_vigEatYN) g_vigRaw = h.rawButtons;                              // Vigilante: o Sim/Nao e nosso
        else st->Gamepad.wButtons |= h.rawButtons;
    }

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
    if (host && g_vigEatYN) g_vigRaw = h.rawButtons;
    else if (host) for (auto& m : map) if (h.rawButtons & m.bit) ns[m.off / 2] = 255;
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
static char* g_saveVeh = nullptr;                                          // veiculo do CJ durante o DoSave
static void  GarOnLoad();
static void  ProtOnLoad();
static void  RewardsOnLoad();
static int   VigLevel();                                                      // recompensas do Vigilante (3.41)
static bool  RcIsRecruit(const char* p);
static int   RcCount();
static void  RcReleaseAll(const char* why);
static int   FemCopModel(int level);
static bool  ArmLegalNow();
static bool  Vig64CanStart(const char** why);
static void  Vig64Start(const char* how);
static void  Vig64Stop();
static bool  VigActiveNow();
static bool  g_dlLegal = false;                                               // o "traficante" desta vez e o armeiro da policia
static int   GarageCopDriver(V3 at, int* pedType);
static void  DoRecenter();
static unsigned g_loadedTimeMs = 0;
static DWORD g_lastAutosaveTick = 0, g_missionEndTick = 0, g_hostCmdSeen = 0;
static int   g_lastMissionsPassed = -1;
static bool  g_wasOnMission = false, g_hostCmdInit = false;
// checkpoint do Vigilante (3.47): codigo no modulo do Vigilante
struct VgCkState { char* ms; BYTE ext; int onm; bool never, ignore; };
static int g_vgSaveOn = 1;                                                    // [Vigilante] SalvarNaMissao
static struct { bool check, pending; int level, kills, backup, backupLeft; DWORD tLoad, tBackup; } g_vgRes = {};
static bool VgCanCheckpoint(const char** why);
static void VgCkBegin(VgCkState& st);
static void VgCkEnd(VgCkState& st);
static void VgCkWrite(unsigned timeMs);
static void VgCkDelete();
static void VgCkLoad();
static void VgResumeTick();
static void GarHelp(const char* fmt, ...);

static bool CanSaveNow(const char** why, bool manual = false) {
    char* ped = PlayerPed();
    if (!ped)                                  { *why = "sem jogador"; return false; }
    if (MenuActive())                          { *why = "menu aberto"; return false; }
    if (IsPlayerOnAMission()) {                // 3.47: no Vigilante, salvar pelo botao vira checkpoint
        const char* vw = nullptr;
        if (!manual || !VgCanCheckpoint(&vw)) { *why = vw ? vw : "em missao"; return false; }
    }
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
    if (!CanSaveNow(&why, kind == 1)) {
        g_saveInfo.saveResult = IsPlayerOnAMission() ? 1 : 2;
        Log("salvar (%d) recusado: %s", kind, why);
        if (kind == 1 && !strncmp(why, "Vigilante:", 10)) GarHelp("%s", why);
        return;
    }
    const char* vw = nullptr;
    bool vigCk = IsPlayerOnAMission() && VgCanCheckpoint(&vw);
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
    g_saveVeh = inVeh ? Rd<char*>(sp, 0x58C) : nullptr;
    VgCkState vst = {};
    if (vigCk) VgCkBegin(vst);
    bool ok = GenericSave(0);
    if (vigCk) VgCkEnd(vst);
    g_saveVeh = nullptr;
    if (inVeh) *pedFlags |= 0x100u;
    SetDir("");
    if (ok) WriteVehicleSidecar(t);
    if (ok) SkinSaveRecord(g_saveSlot - 1);
    if (ok) { if (vigCk) VgCkWrite(t); else VgCkDelete(); }
    g_saveInfo.saveResult = ok ? 0 : 3;
    g_lastAutosaveTick = GetTickCount();
    Log("salvar (%d) no slot %d: %s%s (%s)", kind, g_saveSlot, ok ? "ok" : "ERRO", inVeh ? ", CJ gravado fora do veiculo" : "", SaveFileName);
}

static void CwReloadNow(char* ped);
static void DoReload() {
    char* ped = PlayerPed();
    if (ped && PlayerVehicle() && g_cwOn) { CwReloadNow(ped); return; }      // 3.46: no veiculo tambem (sem animacao)
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
    g_vgRes.check = true; g_vgRes.pending = false; g_vgRes.backupLeft = 0; g_vgRes.tLoad = GetTickCount();
    GarOnLoad();
    ProtOnLoad();
    RewardsOnLoad();
    g_lastAutosaveTick = GetTickCount();
    g_lastMissionsPassed = -1;
    g_missionEndTick = 0;
    Log("jogo carregado (t=%u)", g_loadedTimeMs);
}

static void VgCmdBackup();
static void VgCmdCar();
static void RcCmdRecruit();
static void Vig64Toggle(const char* how);
static void CwCycle(int dir);
static void SaveTick() {
    DWORD now = GetTickCount();
    if (g_lastAutosaveTick == 0) g_lastAutosaveTick = now;
    if (g_pendingRestore && PlayerPed()) { g_pendingRestore = false; RestoreVehicle(); }
    if (g_vgRes.check && !g_pendingRestore && PlayerPed()) { g_vgRes.check = false; VgCkLoad(); }
    VgResumeTick();

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
                case 5: DoRecenter(); break;
                case 6: VgCmdBackup(); break;                                // Reforco (overlay 3.7)
                case 7: VgCmdCar(); break;                                   // Viatura
                case 8: RcCmdRecruit(); break;                               // Recrutar policial
                case 9: Vig64Toggle("pelo botao Vigilante"); break;          // nivel 64: comecar/encerrar
                case 10: CwCycle(-1); break;                                 // trocar arma no veiculo (overlay 3.8)
                case 11: CwCycle(1); break;
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
    g_saveInfo.canSave = CanSaveNow(&why, true) ? 1 : 0;
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
static void SkinClothesTick();
static void GarageTick();
static void GiveTick();
static void GyroTick();
static void ProtTick();
static void TargetStickTick();
static void ApplyNoMouseSteer();
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
    if (g_skinClothes) SkinClothesTick();
    GarageTick();
    GiveTick();
    ProtTick();
    TargetStickTick();
    GyroTick();
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

// O Skin Selector so volta para o CJ numa loja (barbeiro etc.) quando o CJ nao esta tocando "WALK_player"
// (opcode 0611), para nao trocar o modelo no meio da ida ate a cadeira. Com o andar da skin, essa animacao nunca
// toca e ele trocava na hora errada (CJ parado em pe, loja travada). No 0611 (chamada em 0x491703), se o script
// pergunta por "WALK_player" no jogador com o andar de uma skin, vale a animacao de andar dela (ANIM_ID_WALK).
typedef void* (__cdecl* AssocByName_t)(void* clump, const char* name);
typedef void* (__cdecl* AssocById_t)(void* clump, int id);
static const AssocByName_t AssocByName = (AssocByName_t)0x4D6870;            // RpAnimBlendClumpGetAssociation(nome)
static const AssocById_t   AssocById   = (AssocById_t)0x4D68B0;              // RpAnimBlendClumpGetAssociation(id)
extern "C" void* __cdecl ScriptAnimLookup(void* clump, const char* name) {
    void* r = AssocByName(clump, name);
    if (r || !g_skinAnims || !name || _stricmp(name, "WALK_player")) return r;
    char* ped = PlayerPed();
    if (!ped || Rd<void*>(ped, 0x18) != clump || Rd<int>(ped, 0x4D4) < 0x76) return r;
    return AssocById(clump, 0);                                               // ANIM_ID_WALK do grupo da skin
}

static void ApplySkinAnims() {
    static const BYTE orig[] = { 0x83,0xFF,0x36, 0x75,0x1C, 0x83,0xBE,0x98,0x05,0x00,0x00,0x01, 0x75,0x13 };
    BYTE r[10];
    r[0] = 0xE9; *(int*)(r + 1) = (int)((DWORD)&AnimGroupStubA - (0x609A1A + 5));
    r[5] = 0xE9; *(int*)(r + 6) = (int)((DWORD)&AnimGroupStubB - (0x609A1F + 5));
    PatchBytes(0x609A1A, orig, sizeof orig, r, sizeof r, "skins: jeito de andar da skin (CPlayerPed::ProcessAnimGroups)");
    static const BYTE origCall[] = { 0xE8, 0x68, 0x51, 0x04, 0x00 };          // call 0x4D6870
    BYTE c[5];
    c[0] = 0xE8; *(int*)(c + 1) = (int)((DWORD)&ScriptAnimLookup - (0x491703 + 5));
    PatchBytes(0x491703, origCall, sizeof origCall, c, sizeof c, "skins: WALK_player do Skin Selector com o andar da skin (opcode 0611)");
}

// ---------------------------------------------------------------- roupas de missao com skin ([Skins] KeepSkin=1)
// CClothes::RebuildPlayer (0x5A82C0) monta o CJ com as roupas dele. Missoes (balaclava na Home Invasion etc.)
// chamam isso pelo script (070D) sem saber da skin: as roupas do CJ vao por cima do modelo da skin e o corpo
// fica torto (deitado, flutuando). Com uma skin, a reconstrucao fica guardada; quando o modelo volta a ser o do
// CJ (o Skin Selector volta para ele em cutscenes e lojas, ou voce escolhe o CJ), ela e feita com as roupas da vez.
typedef void (__cdecl* RebuildPlayer_t)(void* ped, int ignoreFatMuscle);
static const RebuildPlayer_t RebuildPlayer = (RebuildPlayer_t)0x5A82C0;
static volatile bool g_rebuildPending = false;
static volatile bool g_rebuildNow = false;
extern "C" int __cdecl SkinBlockRebuild(char* ped) {
    if (!g_skinClothes || g_rebuildNow || !ped) return 0;
    int model = Rd<short>(ped, 0x22);
    if (model == 0) { g_rebuildPending = false; return 0; }
    if (!g_rebuildPending) Log("skins: o jogo pediu as roupas do CJ com a skin %d; fica para quando o CJ voltar", model);
    g_rebuildPending = true;
    return 1;
}
extern "C" void RebuildStub();
asm(".text\n"
    ".globl _RebuildStub\n_RebuildStub:\n"
    "  pushl 4(%esp)\n  call _SkinBlockRebuild\n  addl $4, %esp\n"
    "  testl %eax, %eax\n  jnz 1f\n"
    "  pushl %esi\n  movl 8(%esp), %esi\n  movl 0x18(%esi), %eax\n"      // instrucoes originais
    "  pushl $0x5A82C8\n  ret\n"
    "1:\n  ret\n");
static void ApplySkinClothes() {
    static const BYTE orig[] = { 0x56, 0x8B,0x74,0x24,0x08, 0x8B,0x46,0x18 };
    BYTE r[5];
    r[0] = 0xE9; *(int*)(r + 1) = (int)((DWORD)&RebuildStub - (0x5A82C0 + 5));
    PatchBytes(0x5A82C0, orig, sizeof orig, r, sizeof r, "skins: roupas de missao guardadas enquanto ha skin (CClothes::RebuildPlayer)");
}
static void SkinClothesTick() {
    if (!g_rebuildPending) return;
    char* ped = PlayerPed();
    if (!ped || Rd<short>(ped, 0x22) != 0) return;
    g_rebuildPending = false;
    g_rebuildNow = true;
    RebuildPlayer(ped, 0);
    g_rebuildNow = false;
    Log("skins: CJ de volta; roupas reconstruidas");
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

// ---------------------------------------------------------------- garagem pelo telefone ([Garage] Enabled=1)
// Carros guardados nas garagens das casas do CJ viram "meus". Status + Pular: o personagem pega o telefone e
// abre a lista (menu do jogo, como o do Skin Selector: analogico escolhe, Atirar confirma, Pular volta). Cada carro
// tem as acoes dele: Chamar, Mandar para a garagem, Vender (mercado negro, com confirmacao) e Repor (destruido).
// O carro escolhido aparece numa rua fora da vista, dirigido por um membro da Grove, que para perto, desce e vira
// um pedestre comum. Durante a entrega o carro nao sofre dano nenhum (a prova de bala, fogo, explosao, colisao,
// golpe e pneu; o LocalDent respeita a prova de colisao); depois volta as marcas que ele tinha.
// Um carro meu na rua nao some: fica marcado como carro de script; longe (350 m) ele sai da memoria com posicao,
// pecas e danos anotados e volta quando voce chega perto (180 m). Destruido: repor na vaga por $1250 pela lista.
// Capacidade (3.16): cada garagem guarda ate 4 carros, quantos couberem dentro dela (o jogo apaga o que nao cabe).
// Guardar outro carro enquanto um seu esta fora nao tira o seu de voce: se nao sobrar espaco para ele voltar, ele
// fica seu "sem vaga" (chamar e vender continuam) e so entra quando abrir espaco. Ao voltar, ele procura um lugar
// livre dentro da garagem (perto de onde estava), sem encostar nos carros que estao la.
// Placa (3.16): "Trocar placa" abre o teclado do iPhone (pelo overlay 3.3) ou usa o teclado do PC. A placa segue o
// carro: na garagem, na rua, longe e entre saves.
// O que e meu fica num arquivo junto de cada save (GTASAsfN.b.gtmgar).
//
// Dados do jogo: CGarages::aCarsInSafeHouse (0x96ABD8): 20 casas x 4 vagas de CStoredCar (0x40 bytes: posicao,
// marcas, modelo +0x12, pecas +0x14, cores +0x32, direcao +0x3C). CStoredCar::StoreCar (0x449760) guarda um
// veiculo numa vaga; CStoredCar::RestoreCar (0x447E40) cria o veiculo da vaga (a garagem limpa a vaga depois).
// Os comandos do jogo (criar motorista, dirigir ate, menu, telefone, dinheiro...) rodam como num script:
// CRunningScript::ProcessOneCommand (0x469EB0) com um CRunningScript nosso, zerado (nao e missao).

typedef void (__thiscall* ProcessOneCommand_t)(void*);
static const ProcessOneCommand_t ProcessOneCommand = (ProcessOneCommand_t)0x469EB0;
struct Cmd {
    BYTE code[200]; int n;
    explicit Cmd(WORD op) : n(2) { *(WORD*)code = op; }
    Cmd& I(int v)   { code[n++] = 0x01; memcpy(code + n, &v, 4); n += 4; return *this; }
    Cmd& F(float v) { code[n++] = 0x06; memcpy(code + n, &v, 4); n += 4; return *this; }
    Cmd& Out(int lv){ code[n++] = 0x03; *(WORD*)(code + n) = (WORD)lv; n += 2; return *this; }
    Cmd& G(int gv)  { code[n++] = 0x02; *(WORD*)(code + n) = (WORD)(gv * 4); n += 2; return *this; }
    Cmd& Key(const char* k) { code[n++] = 0x09; memset(code + n, 0, 8); strncpy((char*)code + n, k, 7); n += 8; return *this; }
    Cmd& Str(const char* s) { int l = (int)strlen(s); if (l > 100) l = 100; code[n++] = 0x0E; code[n++] = (BYTE)l; memcpy(code + n, s, l); n += l; return *this; }
};
static BYTE g_scr[0x100];
static volatile int g_runDepth = 0;                                          // RunCmd nosso: os ganchos de armas nao mexem
static bool RunCmd(Cmd& c, int* out = nullptr, int nOut = 0) {
    memset(g_scr, 0, sizeof g_scr);
    strcpy((char*)g_scr + 8, "gtmgar");
    *(BYTE**)(g_scr + 0x10) = c.code;                                          // m_pBaseIP
    *(BYTE**)(g_scr + 0x14) = c.code;                                          // m_pCurrentIP
    g_runDepth++;
    ProcessOneCommand(g_scr);
    g_runDepth--;
    for (int i = 0; i < nOut; i++) out[i] = ((int*)(g_scr + 0x3C))[i];          // m_aLocalVars
    return g_scr[0xC5] != 0;                                                   // m_bCondResult
}

// pools: handle = (indice << 8) | byte do mapa
static char* PoolAt(DWORD poolVar, int elem, int h) {
    char* pool = *(char**)poolVar;
    if (!pool || h < 0) return nullptr;
    int idx = h >> 8;
    if (idx >= Rd<int>(pool, 8)) return nullptr;
    BYTE b = Rd<BYTE*>(pool, 4)[idx];
    if ((b & 0x80) || b != (BYTE)(h & 0xFF)) return nullptr;
    return Rd<char*>(pool, 0) + idx * elem;
}
static int PoolRef(DWORD poolVar, int elem, const char* e) {
    char* pool = *(char**)poolVar;
    if (!pool || !e) return -1;
    int idx = (int)((e - Rd<char*>(pool, 0)) / elem);
    if (idx < 0 || idx >= Rd<int>(pool, 8)) return -1;
    return (idx << 8) | Rd<BYTE*>(pool, 4)[idx];
}
static char* VehByRef(int h) { return PoolAt(0xB74494, 0xA18, h); }
static int   VehRef(const char* v) { return PoolRef(0xB74494, 0xA18, v); }
static char* PedByRef(int h) { return PoolAt(0xB74490, 0x7C4, h); }
static int   PedRef(const char* p) { return PoolRef(0xB74490, 0x7C4, p); }

typedef char* (__thiscall* RestoreCar_t)(void* stored);
typedef void  (__thiscall* StoreCar_t)(void* stored, void* veh);
typedef void  (__cdecl* WorldAdd_t)(void* e);
typedef const char* (__thiscall* TextGet_t)(void* text, const char* key);
typedef void  (__thiscall* SetupDamage_t)(void* veh);
static RestoreCar_t g_origRestoreCar = (RestoreCar_t)0x447E40;              // CStoredCar::RestoreCar (o gancho troca)
static StoreCar_t   g_origStoreCar = (StoreCar_t)0x449760;                  // CStoredCar::StoreCar
static const WorldAdd_t WorldAdd = (WorldAdd_t)0x563220;                     // CWorld::Add
static const TextGet_t  TextGet  = (TextGet_t)0x6A0050;                      // CText::Get
static const SetupDamage_t SetupDamageAfterLoad = (SetupDamage_t)0x6B3E90;   // CAutomobile::SetupDamageAfterLoad
static char* const SafeCars = (char*)0x96ABD8;                               // CGarages::aCarsInSafeHouse[20][4]

static int g_garOn = 1, g_garPrice = 1250, g_garSellPct = 15;
static const DWORD kProofMask = (1u << 18) | (1u << 19) | (1u << 20) | (1u << 21) | (1u << 23);   // CPhysical +0x40
enum { GS_NONE = 0, GS_LIVE, GS_AWAY, GS_DEAD, GS_DELIVER, GS_TOGARAGE, GS_PICKUP };
static const char* kGarState[] = { "-", "na rua", "longe", "destruido", "a caminho", "indo p/ garagem", "sendo levado" };
enum { ER_NONE = 0, ER_DELIVER, ER_TOGARAGE, ER_SOLD, ER_REPAIR, ER_REPAIRWAIT };   // ER_REPAIR: buscando para consertar                      // servico em andamento (um por vez)
struct GarCar {
    BYTE  state, house, slot, inGarage, playerCar, hasDmg, pad[2];
    int   veh;                     // handle do veiculo vivo
    BYTE  blob[0x40];              // CStoredCar atual (posicao, direcao, pecas, cores, marcas)
    float home[3]; signed char homeDir[3], pad2;   // onde ele fica na garagem
    float health;
    BYTE  dmg[0x18];               // CAutomobile::m_damageManager (+0x5A0)
    // entrega (nao vai para o arquivo)
    int   ped, blip, phase; DWORD t0, tPhase, tTarget; DWORD flags40; BYTE b42B, parked;
    V3    lastTarget;
    BYTE* dent; int dentLen, dentCap;                                        // estado do LocalDent (amassados)
    char  plate[9];                // placa escolhida ("" = a sorteada pelo jogo)
    BYTE  noSpot, waitLogged;      // sem vaga na garagem dele; aviso de espera ja no log
};
static GarCar g_gar[80];
static int    g_nGar = 0;
static DWORD  g_repairAt = 0;                                                 // quando o carro consertado sai da oficina
static int    g_errand = -1, g_errandKind = ER_NONE;                         // servico da Grove/comprador (indice em g_gar)
static bool   g_garInternal = false;                                         // nossas chamadas a StoreCar/RestoreCar

static WORD BlobModel(const BYTE* b) { return *(const WORD*)(b + 0x12); }
// Os patios da policia (garagens 0x21-0x23) usam as vagas 12, 13 e 14 da mesma tabela: o jogo guarda la o seu
// ultimo carro quando voce morre, e preso ou uma missao te leva (CGarages::StoreCarInNearestImpoundingGarage,
// 0x44A3C0). Nao sao garagens suas.
static bool IsImpoundHouse(int h) { return h >= 12 && h <= 14; }
static V3   BlobPos(const BYTE* b) { const float* f = (const float*)b; return { f[0], f[1], f[2] }; }
static float Dist2(V3 a, V3 b) { float dx = a.x - b.x, dy = a.y - b.y; return sqrtf(dx * dx + dy * dy); }
static bool SlotOf(const void* stored, int* h, int* s) {
    const char* p = (const char*)stored;
    if (p < SafeCars || p >= SafeCars + 20 * 0x100 || (p - SafeCars) % 0x40) return false;
    *h = (int)((p - SafeCars) / 0x100); *s = (int)(((p - SafeCars) % 0x100) / 0x40);
    return true;
}
static const char* CarName(int model) {
    if (model <= 0 || model >= 20000) return "?";
    const char* mi = ((char**)0xA9B0C8)[model];
    if (!mi) return "?";
    const char* t = TextGet((void*)0xC1B340, mi + 0x32);                      // CVehicleModelInfo::m_szGameName
    return t && *t ? t : mi + 0x32;
}
static void ArmWords(char* buf, int size) {                                  // armeiro da policia: troca a palavra nas mensagens
    if (!g_dlLegal) return;
    static const char* from[2] = { "traficante", "Traficante" };
    static const char* to[2]   = { "armeiro", "Armeiro" };
    for (int k = 0; k < 2; k++) {
        char* f;
        while ((f = strstr(buf, from[k])) != nullptr) {
            int lf = (int)strlen(from[k]), lt = (int)strlen(to[k]), rest = (int)strlen(f + lf);
            if ((int)(f - buf) + lt + rest + 1 > size) return;
            memmove(f + lt, f + lf, rest + 1); memcpy(f, to[k], lt);
        }
    }
}
static void GarHelp(const char* fmt, ...) {
    static char buf[200];
    va_list a; va_start(a, fmt); vsnprintf(buf, sizeof buf, fmt, a); va_end(a);
    ArmWords(buf, sizeof buf);
    if (g_origSetHelp) g_origSetHelp(buf, true, false, false);
}
static void RequestBlobModels(const BYTE* b) {
    RequestModel(BlobModel(b), 2);
    const short* up = (const short*)(b + 0x14);
    for (int i = 0; i < 15; i++) if (up[i] > 0) RequestModel(up[i], 2);
}
static float AsF(int v) { float f; memcpy(&f, &v, 4); return f; }
static bool BlobModelsLoaded(const BYTE* b) {
    if (!ModelLoaded(BlobModel(b))) return false;
    const short* up = (const short*)(b + 0x14);
    for (int i = 0; i < 15; i++) if (up[i] > 0 && !ModelLoaded(up[i])) return false;
    return true;
}
static bool IsAutomobile(const char* v) { int c = Rd<int>(v, 0x590); return c == 0 || c == 1 || c == 2; }
static char* ModelInfo(int m) { return m > 0 && m < 20000 ? ((char**)0xA9B0C8)[m] : nullptr; }

// ---- placas. Cada carro tem a textura da placa dele (CVehicle +0x588), feita quando ele nasce:
// CVehicle::SetModelIndex (0x6D6A40) -> CustomCarPlate_TextureCreate (0x6D10E0), com o texto pendente do modelo
// (CVehicleModelInfo +0x28, 8 letras, o mesmo do comando 0674) ou, sem texto, a placa sorteada do modelo. O texto
// nao fica guardado no carro: a placa de cada carro seu fica anotada aqui e vai para o modelo antes de ele nascer.
// Letras, numeros e espaco (o resto vira espaco na placa: 0x6FD7C0).
typedef void  (__thiscall* SetPlateText_t)(void* mi, const char* text);
typedef void* (__cdecl* CreatePlate_t)(const char* text, BYTE type);
typedef void  (__thiscall* PlateDestroy_t)(void* veh);
static const SetPlateText_t SetPlateText = (SetPlateText_t)0x4C8980;          // CVehicleModelInfo::SetCustomCarPlateText
static const CreatePlate_t  CreatePlate  = (CreatePlate_t)0x6FDEA0;           // CCustomCarPlateMgr::CreatePlateTexture
static const PlateDestroy_t PlateDestroy = (PlateDestroy_t)0x6D1150;          // CVehicle::CustomCarPlate_TextureDestroy
static char g_slotPlate[20][4][9];                                            // placa de cada vaga das garagens
static bool PlateCapable(int m) { char* mi = ModelInfo(m); int t = mi ? Rd<int>(mi, 0x3C) : -1; return t == 0 || t == 9; }   // carro, moto
static void CleanPlate(const char* in, char* out) {                           // maiusculas, A-Z 0-9 e espaco, ate 8
    int n = 0;
    for (const char* p = in; *p && n < 8; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c -= 32;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == ' ' && n > 0)) out[n++] = c;
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = 0;
}
static void Plate8(const char* p, char* out) {                                // centralizada em 8 casas
    int l = (int)strlen(p); if (l > 8) l = 8;
    memset(out, ' ', 8); memcpy(out + (8 - l) / 2, p, l); out[8] = 0;
}
// placa pendente no modelo so durante a criacao de um carro; o que havia antes (de uma missao) volta depois
struct PlateSave { char* mi; BYTE saved[9]; BYTE savedType; };
static PlateSave PlateBegin(int model, const char* plate) {
    PlateSave ps = {};
    if (!plate || !plate[0]) return ps;
    char* m = ModelInfo(model);
    if (!m || !Rd<void*>(m, 0x24)) return ps;                                  // modelo sem placa (ou nao carregado)
    ps.mi = m; memcpy(ps.saved, m + 0x28, 9); ps.savedType = Rd<BYTE>(m, 0x31);
    char t[9]; Plate8(plate, t); SetPlateText(m, t);
    return ps;
}
static void PlateEnd(const PlateSave& ps) { if (ps.mi) { memcpy(ps.mi + 0x28, ps.saved, 9); *(BYTE*)(ps.mi + 0x31) = ps.savedType; } }
// troca a placa de um carro que ja existe (fora do desenho do quadro); "" volta a placa sorteada do modelo
static bool ApplyPlateNow(char* v, const char* plate) {
    char* mi = ModelInfo(Rd<short>(v, 0x22));
    void* mat = mi ? Rd<void*>(mi, 0x24) : nullptr;
    if (!mat) return false;
    void* tex;
    if (plate && plate[0]) { char t[9]; Plate8(plate, t); tex = CreatePlate(t, 0xFF); }
    else { tex = Rd<void*>(mat, 0); if (tex) ++*(int*)((char*)tex + 0x54); }   // RwTexture::refCount
    if (!tex) return false;
    PlateDestroy(v);
    *(void**)(v + 0x588) = tex;
    return true;
}

// ---- tamanho dos carros e lugar livre dentro de uma garagem
static float g_dimW[212], g_dimL[212];                                        // meia largura / meio comprimento (400-611)
static void NoteDims(const char* v) {                                         // so com o modelo carregado (carro vivo)
    int m = Rd<short>(v, 0x22);
    char* mi = (m >= 400 && m <= 611) ? ModelInfo(m) : nullptr;
    const float* c = mi ? Rd<const float*>(mi, 0x14) : nullptr;               // CColModel: caixa min (0) e max (0xC)
    if (!c) return;
    float w = fmaxf(fabsf(c[0]), fabsf(c[3])), l = fmaxf(fabsf(c[1]), fabsf(c[4]));
    if (w > 0.4f && w < 4.f && l > 0.8f && l < 12.f) { g_dimW[m - 400] = w; g_dimL[m - 400] = l; }
}
static void Dims(int m, float* w, float* l) {
    *w = 1.1f; *l = 2.6f;
    if (m >= 400 && m <= 611 && g_dimW[m - 400] > 0.f) { *w = g_dimW[m - 400]; *l = g_dimL[m - 400]; }
}
struct Box2 { float x, y, fx, fy, w, l; };                                    // centro, frente (unitaria), meias medidas
static void UnitDir(float x, float y, float* fx, float* fy) {
    float n = sqrtf(x * x + y * y);
    if (n < 0.01f) { *fx = 0.f; *fy = 1.f; } else { *fx = x / n; *fy = y / n; }
}
static bool BoxOverlap(const Box2& a, const Box2& b, float gap) {
    const float ax[4][2] = { { a.fx, a.fy }, { -a.fy, a.fx }, { b.fx, b.fy }, { -b.fy, b.fx } };
    float dx = b.x - a.x, dy = b.y - a.y;
    for (const auto& n : ax) {
        float d  = fabsf(dx * n[0] + dy * n[1]);
        float ra = a.l * fabsf(a.fx * n[0] + a.fy * n[1]) + a.w * fabsf(-a.fy * n[0] + a.fx * n[1]);
        float rb = b.l * fabsf(b.fx * n[0] + b.fy * n[1]) + b.w * fabsf(-b.fy * n[0] + b.fx * n[1]);
        if (d > ra + rb + gap) return false;
    }
    return true;
}
// CGarage: canto +0, eixo A +0xC (x,y), eixo B +0x14, largura (A) +0x20, fundo (B) +0x24
static bool BoxInGarage(const char* gr, const Box2& b, float tol) {
    const float* o = (const float*)gr;
    float Ax = Rd<float>(gr, 0xC), Ay = Rd<float>(gr, 0x10), Bx = Rd<float>(gr, 0x14), By = Rd<float>(gr, 0x18);
    float W = Rd<float>(gr, 0x20), D = Rd<float>(gr, 0x24);
    for (int k = 0; k < 4; k++) {
        float sl = (k & 1) ? b.l : -b.l, sw = (k & 2) ? b.w : -b.w;
        float px = b.x + b.fx * sl - b.fy * sw - o[0], py = b.y + b.fy * sl + b.fx * sw - o[1];
        float u = px * Ax + py * Ay, v = px * Bx + py * By;
        if (u < -tol || u > W + tol || v < -tol || v > D + tol) return false;
    }
    return true;
}
// lugar livre para um carro (w, l): primeiro onde ele estava, senao o mais perto disso; no maximo 4 carros
static bool FindFreeSpot(const char* gr, float w, float l, V3 pref, float pfx, float pfy, const Box2* obs, int nObs, Box2* out) {
    if (nObs >= 4) return false;
    const float gap = 0.5f, tol = 0.2f;                                   // folga entre carros; quanto pode passar da parede
    // a porta fica no lado do eixo A (grge: canto, ponto da frente, ponto do fundo): os carros entram ao longo de B
    float hd[3][2]; int nh = 0;
    UnitDir(pfx, pfy, &hd[0][0], &hd[0][1]); nh = 1;
    float Ax = Rd<float>(gr, 0xC), Ay = Rd<float>(gr, 0x10), Bx = Rd<float>(gr, 0x14), By = Rd<float>(gr, 0x18);
    hd[1][0] = Bx; hd[1][1] = By; hd[2][0] = -Bx; hd[2][1] = -By; nh = 3;
    auto ok = [&](const Box2& c) {
        if (!BoxInGarage(gr, c, tol)) return false;
        for (int j = 0; j < nObs; j++) if (BoxOverlap(c, obs[j], gap)) return false;
        return true;
    };
    Box2 c = { pref.x, pref.y, hd[0][0], hd[0][1], w, l };
    if (ok(c)) { *out = c; return true; }
    const float* o = (const float*)gr;
    float W = Rd<float>(gr, 0x20), D = Rd<float>(gr, 0x24);
    float step = fmaxf(0.25f, fmaxf(W, D) / 60.f), best = 1e9f;
    for (int k = 0; k < nh; k++)
        for (float u = 0.f; u <= W; u += step)
            for (float v = 0.f; v <= D; v += step) {
                c = { o[0] + Ax * u + Bx * v, o[1] + Ay * u + By * v, hd[k][0], hd[k][1], w, l };
                if (!ok(c)) continue;
                float dx = c.x - pref.x, dy = c.y - pref.y, sc = sqrtf(dx * dx + dy * dy) + (k ? 1.5f : 0.f);
                if (sc < best) { best = sc; *out = c; }
            }
    return best < 1e8f;
}

// ---- amassados do LocalDent: o LocalDent nosso exporta as batidas aceitas de cada carro e reaplica depois
typedef int (__cdecl* LdExport_t)(void* veh, void* buf, int cap);
typedef int (__cdecl* LdImport_t)(void* veh, const void* buf, int size);
static LdExport_t g_ldExport = nullptr;
static LdImport_t g_ldImport = nullptr;
static bool g_ldChecked = false;
static void LdResolve() {
    if (g_ldChecked) return;
    g_ldChecked = true;
    HMODULE m = GetModuleHandleA("LocalDent.asi");
    if (m) { g_ldExport = (LdExport_t)GetProcAddress(m, "LocalDentExport"); g_ldImport = (LdImport_t)GetProcAddress(m, "LocalDentImport"); }
    if (!g_ldExport || !g_ldImport) g_ldExport = nullptr, g_ldImport = nullptr;
    Log("garagem: amassados do LocalDent %s", g_ldExport ? "salvos com os carros" : m ? "NAO salvos (LocalDent sem LocalDentExport; use o LocalDent da pasta para-iphone)" : "nao salvos (sem LocalDent)");
}

// estado atual do carro vivo para o registro (posicao, pecas, cores, marcas, vida, danos e amassados)
static void CaptureCar(GarCar& g, char* v) {
    if (!v) return;
    float prevHealth = g.health;
    g_garInternal = true; g_origStoreCar(g.blob, v); g_garInternal = false;
    g.health = Rd<float>(v, 0x4C0);
    g.hasDmg = IsAutomobile(v) ? 1 : 0;
    if (g.hasDmg) memcpy(g.dmg, v + 0x5A0, sizeof g.dmg);
    if (!g_ldExport) return;
    int need = g_ldExport(v, nullptr, 0);
    if (need < 0) {
        need = -need;
        if (need > g.dentCap) { BYTE* p = (BYTE*)realloc(g.dent, need); if (!p) return; g.dent = p; g.dentCap = need; }
        int n = g_ldExport(v, g.dent, g.dentCap);
        g.dentLen = n > 0 ? n : 0;
    } else if (g.dentLen && prevHealth > 0.f && g.health > prevHealth + 25.f) {
        g.dentLen = 0;                                                        // consertado (Pay 'n' Spray, TransFender)
    }
}
// cria o carro do registro (modelos ja carregados)
static char* SpawnCar(GarCar& g, const BYTE* blob) {
    BYTE b[0x40]; memcpy(b, blob, sizeof b);
    char* v;
    PlateSave ps = PlateBegin(BlobModel(b), g.plate);
    g_garInternal = true; v = g_origRestoreCar(b); g_garInternal = false;
    PlateEnd(ps);
    if (!v) return nullptr;
    NoteDims(v);
    WorldAdd(v);
    *(BYTE*)(v + 0x4A4) = 2;                                                  // m_nCreatedBy = MISSION_VEHICLE: nao some
    if (g.health > 0.f) *(float*)(v + 0x4C0) = g.health;
    if (g.hasDmg && IsAutomobile(v)) { memcpy(v + 0x5A0, g.dmg, sizeof g.dmg); SetupDamageAfterLoad(v); }
    if (g.dentLen && g_ldImport) g_ldImport(v, g.dent, g.dentLen);           // o LocalDent reaplica no proximo quadro
    Cmd c(0x0249); c.I(BlobModel(b)); RunCmd(c);                              // o carro segura o modelo; o pedido sai
    return v;
}
static void RemoveGar(int i) {
    free(g_gar[i].dent);
    if (g_errand == i) { g_errand = -1; g_errandKind = ER_NONE; }
    else if (g_errand > i) g_errand--;
    memmove(&g_gar[i], &g_gar[i + 1], (g_nGar - i - 1) * sizeof(GarCar));
    g_nGar--;
}
static int FindGarVeh(const char* v) {
    int h = VehRef(v);
    for (int i = 0; i < g_nGar; i++) if ((g_gar[i].state == GS_LIVE || g_gar[i].state == GS_DELIVER || g_gar[i].state == GS_PICKUP) && g_gar[i].veh == h) return i;
    return -1;
}
static GarCar* NewGar(int house, int slot, const BYTE* homeBlob) {
    if (g_nGar >= 80) return nullptr;
    GarCar& g = g_gar[g_nGar++];
    memset(&g, 0, sizeof g);
    g.house = (BYTE)house; g.slot = (BYTE)slot; g.veh = -1; g.ped = -1; g.blip = -1;
    memcpy(g.blob, homeBlob, sizeof g.blob);
    memcpy(g.home, homeBlob, 12); memcpy(g.homeDir, homeBlob + 0x3C, 3);
    return &g;
}

// a garagem cria os carros dela quando voce chega perto: viram meus, vivos, ainda dentro da garagem
// (a placa da vaga vai para o modelo antes; a garagem limpa a vaga depois de criar o carro)
static char* __fastcall RestoreCarDetour(void* stored, void*) {
    BYTE before[0x40]; memcpy(before, stored, sizeof before);
    int h = -1, s = -1;
    bool slot = !g_garInternal && SlotOf(stored, &h, &s);
    char plate[9] = "";
    if (slot) memcpy(plate, g_slotPlate[h][s], 9);
    char* v;
    PlateSave ps = PlateBegin(BlobModel(before), plate);
    v = g_origRestoreCar(stored);
    PlateEnd(ps);
    if (slot && v) {
        NoteDims(v);
        g_slotPlate[h][s][0] = 0;
        if (!IsImpoundHouse(h)) if (GarCar* g = NewGar(h, s, before)) { g->state = GS_LIVE; g->veh = VehRef(v); g->inGarage = 1; memcpy(g->plate, plate, 9); }
    }
    return v;
}
// a garagem guarda os carros que estao dentro dela quando a porta fecha (ou antes de salvar): zera as 4 vagas e
// guarda, em ordem, todo carro que nao e de missao e esta inteiro dentro dela, ate 4; os outros sao apagados
static void __fastcall StoreCarDetour(void* stored, void*, void* v) {
    g_origStoreCar(stored, v);
    int h, s;
    if (g_garInternal || !v || !SlotOf(stored, &h, &s)) return;
    NoteDims((char*)v);
    int i = FindGarVeh((char*)v);
    if (IsImpoundHouse(h)) {                                                  // patio: nao e garagem sua
        if (i >= 0) {                                                         // um carro seu nao vai para o patio
            *(WORD*)((BYTE*)stored + 0x12) = 0;
            if (char* car = VehByRef(g_gar[i].veh)) CaptureCar(g_gar[i], car);
            Log("garagem: %s seu iria para o patio da policia; continua seu, onde estava", CarName(BlobModel(g_gar[i].blob)));
        }
        return;
    }
    g_slotPlate[h][s][0] = 0;
    if (i >= 0) { memcpy(g_slotPlate[h][s], g_gar[i].plate, 9); RemoveGar(i); return; }   // um meu voltou para a garagem
    // carro novo na garagem: os seus que estao fora continuam seus (se faltar espaco, ficam "sem vaga")
    Log("garagem: %s guardado na casa %d vaga %d", CarName(Rd<short>(v, 0x22)), h, s + 1);
}

// ---- garagens das casas
typedef bool (__thiscall* InsideGarage_t)(void* garage, V3 pos);
typedef int  (__cdecl* SafeHouseIdx_t)(int type);
static const InsideGarage_t IsPointInsideGarage = (InsideGarage_t)0x448740;  // CGarage::IsPointInsideGarage
static const SafeHouseIdx_t SafeHouseIndex = (SafeHouseIdx_t)0x4489F0;       // CGarages::FindSafeHouseIndexForGarageType
static bool IsHideoutType(int t) { return t == 0x10 || t == 0x11 || t == 0x12 || (t >= 0x18 && t <= 0x20) || (t >= 0x27 && t <= 0x2A) || t == 0x2D; }   // sem os patios 0x21-0x23
static char* HideoutGarageAt(V3 p) {
    int n = *(int*)0x96C024;
    for (int i = 0; i < n && i < 50; i++) {
        char* gr = (char*)0x96C048 + i * 0xD8;
        if (IsHideoutType(Rd<BYTE>(gr, 0x4C)) && IsPointInsideGarage(gr, p)) return gr;
    }
    return nullptr;
}
static char* GarageOfHouse(int house, V3 nearPos) {
    int n = *(int*)0x96C024; char* best = nullptr; float bd = 1e9f;
    for (int i = 0; i < n && i < 50; i++) {
        char* gr = (char*)0x96C048 + i * 0xD8; int t = Rd<BYTE>(gr, 0x4C);
        if (!IsHideoutType(t) || SafeHouseIndex(t) != house) continue;
        float d = Dist2(*(V3*)gr, nearPos);
        if (d < bd) { bd = d; best = gr; }
    }
    return best;
}

// ---- preco no mercado negro: o valor do modelo no jogo (handling, +0xD8) x porcentagem x estado do carro
static int CarValue(int model) {
    if (model < 400 || model > 611) return 0;
    const char* mi = ((char**)0xA9B0C8)[model];
    if (!mi) return 0;
    return Rd<int>((void*)0xC2B9DC, Rd<BYTE>(mi, 0x4A) * 0xE0 + 0xD8);      // mod_HandlingManager[m_nHandlingId].m_nMonetaryValue
}
static int SellPrice(int model, float health) {
    float f = health <= 0.f ? 1.f : health / 1000.f;
    if (f < .5f) f = .5f;
    if (f > 1.f) f = 1.f;
    int p = (int)((float)CarValue(model) * (float)g_garSellPct / 100.f * f);
    p = (p + 25) / 50 * 50;
    return p < 50 ? 50 : p;
}

// ---------------------------------------------------------------- lista (menu do jogo)
struct GarItem { int kind; int house, slot, gi, model; };                    // kind 0 vaga da garagem, 1 registro
enum { AC_CALL = 1, AC_TOGARAGE, AC_SELL, AC_REPOR, AC_YES, AC_NO, AC_PLATE, AC_ROOT_GAR, AC_ROOT_ARM, AC_REPAIR, AC_ROOT_VIG, AC_ROOT_RC };
static GarItem g_items[90], g_sel;
static int g_nItems = 0, g_page = 0, g_menu = -1, g_level = 0, g_acts[12], g_nActs = 0, g_price = 0;
static bool g_menuOpen = false;
static volatile bool g_garReq = false, g_garSel = false, g_garCancel = false;
static DWORD g_phoneOffAt = 0, g_menuOpenAt = 0;
// traficante de armas (mais abaixo, depois da garagem)
static int  g_armOn = 1, g_armCall = 250, g_armDiscount = 15, g_armLockMult = 10, g_armLockBoxes = 2;
static int  g_armCooldown = 1440, g_armLeave = 40, g_armCopSee = 25, g_armCopAngle = 60, g_armDoorDist = 7;
static bool g_shopOpen = false;
static void DealerRootLabel(char* out, int n);
static void DealerCall();
static void DealerTick();
static void VigTick();
static void WantedPerkTick();
static void ArmSave();
static void ArmOnLoad();

static int PlayerRef() { char* p = PlayerPed(); return p ? PedRef(p) : -1; }
static void Phone(bool on) { int pr = PlayerRef(); if (pr >= 0) { Cmd c(0x0729); c.I(pr).I(on ? 1 : 0); RunCmd(c); } }
static void PlayerControl(bool on) { Cmd c(0x01B4); c.I(0).I(on ? 1 : 0); RunCmd(c); }
static void SetText(const char* key, const char* text) { Cmd c(0x0ADF); c.Str(key).Str(text); RunCmd(c); }
static void AddMoney(int v) { Cmd c(0x0109); c.I(0).I(v); RunCmd(c); }

static BYTE* SlotPtr(int h, int s) { return (BYTE*)SafeCars + h * 0x100 + s * 0x40; }

// ---- espaco nas garagens: carros guardados nas vagas + carros do mundo dentro dela (porta aberta)
static Box2 BlobBox(const BYTE* b) {
    Box2 x; float w, l; Dims(BlobModel(b), &w, &l);
    const float* p = (const float*)b; const signed char* d = (const signed char*)(b + 0x3C);
    x.x = p[0]; x.y = p[1]; UnitDir(d[0], d[1], &x.fx, &x.fy); x.w = w; x.l = l;
    return x;
}
static Box2 VehBox(const char* v) {
    Box2 x; float w, l; Dims(Rd<short>(v, 0x22), &w, &l);
    V3 p = EntityPos(v); x.x = p.x; x.y = p.y; x.w = w; x.l = l;
    const float* m = Rd<const float*>(v, 0x14);
    if (m) UnitDir(m[4], m[5], &x.fx, &x.fy); else { float h = Rd<float>(v, 0x10); x.fx = -sinf(h); x.fy = cosf(h); }
    return x;
}
static int GarageObstacles(int house, const char* gr, Box2* out, int cap) {
    int n = 0;
    for (int s = 0; s < 4 && n < cap; s++) if (BlobModel(SlotPtr(house, s))) out[n++] = BlobBox(SlotPtr(house, s));
    char* pool = *(char**)0xB74494;
    if (!pool) return n;
    int size = Rd<int>(pool, 8);
    for (int i = 0; i < size && n < cap; i++) {
        if (Rd<BYTE*>(pool, 4)[i] & 0x80) continue;
        char* v = Rd<char*>(pool, 0) + i * 0xA18;
        if (!IsPointInsideGarage((void*)gr, EntityPos(v))) continue;
        NoteDims(v); out[n++] = VehBox(v);
    }
    return n;
}
static DWORD g_capLogged = 0;
static void LogCapacity(int house, const char* gr) {                         // uma vez por garagem: quantos cabem
    if (g_capLogged & (1u << house)) return;
    g_capLogged |= 1u << house;
    Box2 obs[4]; int n = 0;
    const float* o = (const float*)gr;
    V3 corner = { o[0], o[1], o[2] };
    while (n < 4 && FindFreeSpot(gr, 1.05f, 2.5f, corner, Rd<float>(gr, 0x14), Rd<float>(gr, 0x18), obs, n, &obs[n])) n++;
    char name[9] = {}; memcpy(name, gr + 0x44, 8);
    Log("garagem: casa %d (%s) tem %.1f x %.1f m: cabem uns %d carros (o jogo guarda ate 4)", house, name[0] ? name : "?", Rd<float>(gr, 0x20), Rd<float>(gr, 0x24), n);
}
static bool IsOutCar(int i) {
    const GarCar& g = g_gar[i];
    if (g.state == GS_PICKUP) return !(g_errand == i && g_errandKind == ER_SOLD);
    return (g.state == GS_LIVE && !g.inGarage) || g.state == GS_AWAY || g.state == GS_TOGARAGE || g.state == GS_DELIVER;
}
// quais carros seus fora da garagem ainda tem lugar para voltar (os que vao para a garagem primeiro)
static void ComputeSpots() {
    for (int h = 0; h < 20; h++) {
        if (IsImpoundHouse(h)) continue;
        int first = -1;
        for (int i = 0; i < g_nGar && first < 0; i++) if (g_gar[i].house == h && IsOutCar(i)) first = i;
        if (first < 0) continue;
        char* gr = GarageOfHouse(h, *(V3*)g_gar[first].home);
        if (gr) LogCapacity(h, gr);
        Box2 obs[12]; int n = gr ? GarageObstacles(h, gr, obs, 8) : 0;
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < g_nGar; i++) {
                GarCar& g = g_gar[i];
                if (g.house != h || !IsOutCar(i) || (g.state == GS_TOGARAGE) != (pass == 0)) continue;
                bool spot = true;
                char* v = VehByRef(g.veh);
                if (gr && !(v && IsPointInsideGarage(gr, EntityPos(v)))) {   // ja la dentro: conta como guardado
                    float w, l; Dims(BlobModel(g.blob), &w, &l);
                    Box2 b;
                    spot = n < 8 && FindFreeSpot(gr, w, l, *(V3*)g.home, g.homeDir[0], g.homeDir[1], obs, n, &b);
                    if (spot) obs[n++] = b;
                }
                if (spot != !g.noSpot) {
                    g.noSpot = !spot;
                    Log("garagem: %s (casa %d) %s", CarName(BlobModel(g.blob)), h, spot ? "tem vaga na garagem de novo" : "ficou sem vaga (nao sobra espaco livre ao lado dos carros que estao la); continua seu");
                }
            }
    }
}
static void BuildItems() {
    ComputeSpots();
    g_nItems = 0;
    for (int h = 0; h < 20; h++) for (int s = 0; s < 4; s++)
        if (!IsImpoundHouse(h)) if (int m = BlobModel(SlotPtr(h, s))) g_items[g_nItems++] = { 0, h, s, -1, m };
    for (int i = 0; i < g_nGar && g_nItems < 88; i++)
        if (g_gar[i].state != GS_DELIVER && g_gar[i].state != GS_PICKUP) g_items[g_nItems++] = { 1, g_gar[i].house, g_gar[i].slot, i, BlobModel(g_gar[i].blob) };
}
static bool ItemValid(const GarItem& it) {
    if (it.kind == 0) return BlobModel(SlotPtr(it.house, it.slot)) == it.model;
    return it.gi >= 0 && it.gi < g_nGar && g_gar[it.gi].house == it.house && g_gar[it.gi].slot == it.slot &&
           BlobModel(g_gar[it.gi].blob) == it.model && g_gar[it.gi].state != GS_DELIVER && g_gar[it.gi].state != GS_PICKUP;
}
static const char* ItemStatus(const GarItem& it) {
    if (it.kind == 0) return "garagem";
    const GarCar& g = g_gar[it.gi];
    if (g_errand == it.gi && g_errandKind == ER_REPAIRWAIT) return "no conserto";
    if (g_errand == it.gi && g_errandKind == ER_REPAIR) return "indo p/ conserto";
    if (g.state == GS_DEAD) return "destruido";
    if (g.state == GS_LIVE && g.inGarage) return "garagem";
    if (g.state == GS_TOGARAGE) return g.noSpot ? "esperando vaga" : kGarState[g.state];
    if (g.noSpot && g.state == GS_LIVE) return "na rua, sem vaga";
    if (g.noSpot && g.state == GS_AWAY) return "longe, sem vaga";
    return kGarState[g.state];
}
static const char* ItemPlate(const GarItem& it) { return it.kind == 0 ? g_slotPlate[it.house][it.slot] : g_gar[it.gi].plate; }
static float ItemHealth(const GarItem& it) {
    if (it.kind == 0) return 1000.f;
    const GarCar& g = g_gar[it.gi];
    if (char* v = VehByRef(g.veh)) return Rd<float>(v, 0x4C0);
    return g.health;
}
static void CloseMenu(bool keepPhone) {
    if (g_menu >= 0) { Cmd c(0x08DA); c.I(g_menu); RunCmd(c); }
    g_menu = -1; g_menuOpen = false; g_level = 0;
    PlayerControl(true);
    if (keepPhone) g_phoneOffAt = GetTickCount() + 2500; else Phone(false);
}
static void ShowRows(const char* title, const char* header, const char* const* rows, int n) {
    if (g_menu >= 0) { Cmd c(0x08DA); c.I(g_menu); RunCmd(c); g_menu = -1; }
    static char keyBuf[12][8];
    const char* keys[12];
    SetText("GTMGT", title);
    SetText("GTMGH", header);
    for (int r = 0; r < 12; r++) {
        snprintf(keyBuf[r], 8, "GTMG%d", r);
        keys[r] = "DUMMY";
        if (r < n) { SetText(keyBuf[r], rows[r]); keys[r] = keyBuf[r]; }
    }
    int id = -1;
    { Cmd c(0x08D4); c.Key("GTMGT").F(29.f).F(140.f).F(220.f).I(1).I(1).I(1).I(0).Out(0); RunCmd(c, &id, 1); }
    g_menu = id;
    Cmd c(0x08DB); c.I(id).I(0).Key("GTMGH");
    for (int r = 0; r < 12; r++) c.Key(keys[r]);
    RunCmd(c);
}
static void ShowList() {
    g_level = 0;
    int first = g_page * 11, n = g_nItems - first; if (n > 11) n = 11;
    static char text[12][96];
    const char* rows[12];
    for (int r = 0; r < n; r++) {
        const GarItem& it = g_items[first + r];
        if (it.kind == 1 && g_gar[it.gi].state == GS_DEAD) snprintf(text[r], 96, "%s - repor $%d", CarName(it.model), g_garPrice);
        else snprintf(text[r], 96, "%s - %s", CarName(it.model), ItemStatus(it));
        rows[r] = text[r];
    }
    int total = n;
    if (g_nItems > 11) { rows[total++] = g_nItems > first + 11 ? "Mais..." : "Voltar ao inicio"; }
    ShowRows("Meus carros", g_page ? "Carro (mais)" : "Carro", rows, total);
}
static void ShowActions() {
    g_level = 1; g_nActs = 0;
    static char text[6][96];
    const char* rows[6];
    int n = 0;
    auto add = [&](int act, const char* t) { g_acts[g_nActs++] = act; snprintf(text[n], 96, "%s", t); rows[n] = text[n]; n++; };
    bool dead = g_sel.kind == 1 && g_gar[g_sel.gi].state == GS_DEAD;
    if (dead) {
        char t[96]; snprintf(t, sizeof t, "Repor na garagem ($%d)", g_garPrice); add(AC_REPOR, t);
    } else {
        add(AC_CALL, "Chamar");
        bool out = g_sel.kind == 1 && g_gar[g_sel.gi].state != GS_TOGARAGE && !(g_gar[g_sel.gi].state == GS_LIVE && g_gar[g_sel.gi].inGarage);
        if (out) add(AC_TOGARAGE, "Mandar para a garagem");
        if (g_sel.kind == 1) add(AC_REPAIR, "Mandar consertar");
        if (PlateCapable(g_sel.model)) add(AC_PLATE, "Trocar placa");
        g_price = SellPrice(g_sel.model, ItemHealth(g_sel));
        char t[96]; snprintf(t, sizeof t, "Vender ($%d)", g_price); add(AC_SELL, t);
    }
    char title[96];
    if (ItemPlate(g_sel)[0]) snprintf(title, sizeof title, "%s (%s)", CarName(g_sel.model), ItemPlate(g_sel));
    else snprintf(title, sizeof title, "%s", CarName(g_sel.model));
    ShowRows(title, ItemStatus(g_sel), rows, n);
}
static void ShowConfirm() {
    g_level = 2; g_nActs = 0;
    static char yes[96];
    snprintf(yes, sizeof yes, "Sim, vender por $%d", g_price);
    const char* rows[2] = { yes, "Nao" };
    g_acts[g_nActs++] = AC_YES; g_acts[g_nActs++] = AC_NO;
    ShowRows("Tem certeza?", CarName(g_sel.model), rows, 2);
}
static bool g_garHooked = false;
// telefone (nivel 3): garagem e traficante
static void ShowRoot() {
    g_level = 3; g_nActs = 0;
    static char arm[64], rc[48];
    const char* rows[4]; int n = 0;
    if (g_garOn) { g_acts[g_nActs++] = AC_ROOT_GAR; rows[n++] = "Carro da garagem"; }
    if (g_armOn) { g_acts[g_nActs++] = AC_ROOT_ARM; DealerRootLabel(arm, sizeof arm); rows[n++] = arm; }
    if (VigLevel() >= 64) { g_acts[g_nActs++] = AC_ROOT_VIG; rows[n++] = VigActiveNow() ? "Encerrar o Vigilante" : "Comecar o Vigilante"; }
    if (RcCount() > 0) { g_acts[g_nActs++] = AC_ROOT_RC; snprintf(rc, sizeof rc, "Dispensar os policiais (%d)", RcCount()); rows[n++] = rc; }
    ShowRows("Telefone", "Ligar para", rows, n);
}
static void OpenMenu() {
    if (!g_garOn && !g_armOn && VigLevel() < 64 && !RcCount()) return;
    g_page = 0; g_menuOpen = true; g_menuOpenAt = GetTickCount(); g_garSel = g_garCancel = false;
    if (!ModelLoaded(330)) { RequestModel(330, 2); LoadAllRequestedModels(false); }   // celular
    PlayerControl(false);
    Phone(true);
    ShowRoot();
    Log("telefone: menu aberto");
}
static void OpenGarageList() {
    if (!g_garHooked) { CloseMenu(false); GarHelp("Garagem desligada: os ganchos nao entraram (veja o GTMTouchSA.log)."); return; }
    BuildItems();
    if (!g_nItems) { CloseMenu(false); GarHelp("Voce ainda nao tem carros guardados nas suas garagens."); return; }
    g_page = 0;
    ShowList();
    Log("garagem: lista aberta (%d carros)", g_nItems);
}

// ---------------------------------------------------------------- entrega (Grove dirigindo ate voce)
static bool FindSpawnNode(V3 p, const float* m, V3* out, float* heading) {
    float fx = m ? m[4] : 0.f, fy = m ? m[5] : 1.f;
    static const float ang[] = { 180.f, 140.f, 220.f, 100.f, 260.f, 0.f };
    static const float dist[] = { 150.f, 120.f, 190.f };
    for (float d : dist) for (float a : ang) {
        float r = a * 3.14159265f / 180.f, c = cosf(r), s = sinf(r);
        float x = p.x + (fx * c - fy * s) * d, y = p.y + (fx * s + fy * c) * d;
        int o[4] = {};
        Cmd cmd(0x03D3); cmd.F(x).F(y).F(p.z).Out(0).Out(1).Out(2).Out(3);
        RunCmd(cmd, o, 4);
        V3 n = { AsF(o[0]), AsF(o[1]), AsF(o[2]) };
        float dd = Dist2(n, p);
        if (dd > 70.f && dd < 260.f) { *out = n; *heading = AsF(o[3]); return true; }
    }
    return false;
}
static void DriveTo(GarCar& g, V3 p) {
    Cmd c(0x00A7); c.I(g.veh).F(p.x).F(p.y).F(p.z); RunCmd(c);
    g.lastTarget = p; g.tTarget = GetTickCount();
}
static bool LoadModelsNow(const BYTE* blob, int ped) {
    if (blob) RequestBlobModels(blob);
    if (ped > 0) RequestModel(ped, 2);
    LoadAllRequestedModels(false);
    return (!blob || BlobModelsLoaded(blob)) && (ped <= 0 || ModelLoaded(ped));
}
static bool StartDelivery(int i) {
    GarCar& g = g_gar[i];
    char* ped = PlayerPed();
    if (!ped) return false;
    V3 p = EntityPos(ped), node; float hd = 0.f;
    if (!FindSpawnNode(p, Rd<const float*>(ped, 0x14), &node, &hd)) { GarHelp("Nao achei uma rua para o carro vir. Tente de outro lugar."); return false; }
    static const int kFam[3] = { 105, 106, 107 };
    int fam = kFam[GetTickCount() % 3], ptype = 8;
    { int pt = 8, cop = GarageCopDriver(p, &pt); if (cop > 0) { fam = cop; ptype = pt; } }   // nivel 64: policial da regiao
    if (!LoadModelsNow(g.blob, fam)) { GarHelp("O carro nao carregou. Tente de novo."); return false; }
    BYTE b[0x40]; memcpy(b, g.blob, sizeof b);
    float* pos = (float*)b; pos[0] = node.x; pos[1] = node.y; pos[2] = node.z + 1.0f;
    float r = hd * 3.14159265f / 180.f;
    b[0x3C] = (BYTE)(signed char)(-sinf(r) * 100.f); b[0x3D] = (BYTE)(signed char)(cosf(r) * 100.f); b[0x3E] = 0;
    char* v = SpawnCar(g, b);
    if (!v) { GarHelp("O carro nao pode ser criado agora. Tente de novo."); return false; }
    *(float*)(v + 0x4B0) = 0.f;                                               // CVehicle::m_fDirtLevel: vem lavado
    g.veh = VehRef(v);
    g.flags40 = Rd<DWORD>(v, 0x40); g.b42B = Rd<BYTE>(v, 0x42B);
    *(DWORD*)(v + 0x40) |= kProofMask;                                        // bala, fogo, colisao, golpe, explosao
    *(BYTE*)(v + 0x42B) |= 0x80;                                              // pneus nao furam
    int o = -1;
    { Cmd c(0x0129); c.I(g.veh).I(ptype).I(fam).Out(0); RunCmd(c, &o, 1); }
    g.ped = o;
    { Cmd c(0x0249); c.I(fam); RunCmd(c); }
    { Cmd c(0x00AD); c.I(g.veh).F(22.f); RunCmd(c); }
    { Cmd c(0x00AE); c.I(g.veh).I(2); RunCmd(c); }
    { Cmd c(0x0186); c.I(g.veh).Out(0); RunCmd(c, &o, 1); g.blip = o; }
    DriveTo(g, p);
    g.state = GS_DELIVER; g.phase = 0; g.t0 = g.tPhase = GetTickCount(); g.inGarage = 0;
    g_errand = i; g_errandKind = ER_DELIVER;
    Log("garagem: %s a caminho, saindo de (%.0f, %.0f), %.0f m, motorista %d%s", CarName(BlobModel(g.blob)), node.x, node.y, Dist2(node, p), fam, g.dentLen ? ", com os amassados" : "");
    GarHelp("%s a caminho.", CarName(BlobModel(g.blob)));
    return true;
}
static void EndDelivery(GarCar& g, const char* how) {
    char* v = VehByRef(g.veh);
    if (v) {                                                                  // volta as marcas que ele tinha
        *(DWORD*)(v + 0x40) = (Rd<DWORD>(v, 0x40) & ~kProofMask) | (g.flags40 & kProofMask);
        *(BYTE*)(v + 0x42B) = (Rd<BYTE>(v, 0x42B) & 0x7F) | (g.b42B & 0x80);
        Cmd c(0x020A); c.I(g.veh).I(1); RunCmd(c);
    }
    if (g.blip >= 0) { Cmd c(0x0164); c.I(g.blip); RunCmd(c); g.blip = -1; }
    if (PedByRef(g.ped)) { Cmd c(0x01C2); c.I(g.ped); RunCmd(c); }
    g.ped = -1;
    g.state = v ? GS_LIVE : GS_AWAY;
    g_errand = -1; g_errandKind = ER_NONE;
    Log("garagem: entrega terminada (%s)", how);
}
static void DeliveryTick(GarCar& g) {
    char* v = VehByRef(g.veh); char* d = PedByRef(g.ped); char* me = PlayerPed();
    DWORD now = GetTickCount();
    if (!v) { EndDelivery(g, "o carro sumiu"); return; }
    if (!me) return;
    V3 vp = EntityPos(v), pp = EntityPos(me);
    float dist = Dist2(vp, pp);
    bool driverIn = d && Rd<float>(d, 0x540) > 0.f && (Rd<BYTE>(d, 0x46D) & 1) && Rd<char*>(d, 0x58C) == v;
    if (g.phase < 2 && !driverIn) { int pr = g.ped; EndDelivery(g, "o motorista saiu ou morreu"); if (PedByRef(pr)) { Cmd c(0x01C2); c.I(pr); RunCmd(c); } return; }
    if (g.phase == 0) {
        if (now - g.tTarget > 3000 && Dist2(g.lastTarget, pp) > 15.f) DriveTo(g, pp);
        if (dist < 14.f) { Cmd c(0x00AD); c.I(g.veh).F(0.f); RunCmd(c); g.phase = 1; g.tPhase = now; }
        else if (now - g.t0 > 100000) {                                       // preso no transito: aparece numa rua perto
            int o[4] = {};
            Cmd cn(0x03D3); cn.F(pp.x).F(pp.y).F(pp.z).Out(0).Out(1).Out(2).Out(3); RunCmd(cn, o, 4);
            { Cmd c(0x00AB); c.I(g.veh).F(AsF(o[0])).F(AsF(o[1])).F(AsF(o[2]) + 1.f); RunCmd(c); }
            { Cmd c(0x0175); c.I(g.veh).F(AsF(o[3])); RunCmd(c); }
            Log("garagem: o carro demorou; apareceu na rua mais perto");
            g.t0 = now;
        }
    } else if (g.phase == 1) {
        const float* mv = (const float*)(v + 0x44);
        float sp = sqrtf(mv[0] * mv[0] + mv[1] * mv[1] + mv[2] * mv[2]);
        if (sp < 0.02f || now - g.tPhase > 4000) { Cmd c(0x0633); c.I(g.ped); RunCmd(c); g.phase = 2; g.tPhase = now; }
    } else if (g.phase == 2) {
        bool out = !d || !(Rd<BYTE>(d, 0x46D) & 1);
        if (out || now - g.tPhase > 6000) {
            if (d) { Cmd c(0x05DE); c.I(g.ped); RunCmd(c); }
            Log("garagem: %s entregue a %.0f m", CarName(BlobModel(g.blob)), dist);
            GarHelp("%s entregue.", CarName(BlobModel(g.blob)));
            EndDelivery(g, "entregue");
        }
    }
}

// ---------------------------------------------------------------- busca a pe (Grove leva para a garagem, comprador leva o vendido)
static void RepairGar(GarCar& g) {                                           // inteiro: vida cheia, sem danos nem amassados
    g.health = 1000.f; g.hasDmg = 0; g.dentLen = 0;
}
static bool StartDelivery(int i);
static void RepairWaitTick(GarCar& g) {
    if (GetTickCount() < g_repairAt) return;
    char* me = PlayerPed();
    if (!me || Rd<float>(me, 0x540) <= 0.f || *(volatile int*)0xB72914 != 0) { g_repairAt = GetTickCount() + 3000; return; }
    int i = (int)(&g - g_gar);
    g_errand = -1; g_errandKind = ER_NONE;
    if (StartDelivery(i)) { Log("garagem: %s consertado e lavado; voltando", CarName(BlobModel(g.blob))); GarHelp("%s consertado e lavado, a caminho.", CarName(BlobModel(g.blob))); return; }
    g_errand = i; g_errandKind = ER_REPAIRWAIT; g_repairAt = GetTickCount() + 5000;   // tenta de novo
}
static bool StartPickup(int i, bool sold) {
    GarCar& g = g_gar[i];
    char* v = VehByRef(g.veh); char* me = PlayerPed();
    if (!v || !me) return false;
    V3 cp = EntityPos(v), pp = EntityPos(me);
    float dx = cp.x - pp.x, dy = cp.y - pp.y, l = sqrtf(dx * dx + dy * dy); if (l < 1.f) { dx = 0.f; dy = 1.f; l = 1.f; }
    V3 spot = cp;
    static const float side[] = { 0.f, 90.f, -90.f, 180.f };
    for (float a : side) {                                                   // calcada a uns 35 m do carro, do lado oposto ao seu
        float r = a * 3.14159265f / 180.f, ux = dx / l, uy = dy / l;
        float tx = cp.x + (ux * cosf(r) - uy * sinf(r)) * 35.f, ty = cp.y + (ux * sinf(r) + uy * cosf(r)) * 35.f;
        int o[3] = {};
        Cmd c(0x02C0); c.F(tx).F(ty).F(cp.z).Out(0).Out(1).Out(2); RunCmd(c, o, 3);
        V3 n = { AsF(o[0]), AsF(o[1]), AsF(o[2]) };
        if (Dist2(n, pp) > 25.f && Dist2(n, cp) < 80.f) { spot = n; break; }
    }
    static const int kFam[3] = { 105, 106, 107 }, kBuyer[3] = { 28, 29, 30 };   // Grove / traficantes
    int model = sold ? kBuyer[GetTickCount() % 3] : kFam[GetTickCount() % 3], ptype = sold ? 4 : 8;
    if (!sold) { int pt = 8, cop = GarageCopDriver(spot, &pt); if (cop > 0) { model = cop; ptype = pt; } }   // nivel 64
    if (!LoadModelsNow(nullptr, model)) { GarHelp("Ninguem disponivel agora. Tente de novo."); return false; }
    int o = -1;
    { Cmd c(0x009A); c.I(ptype).I(model).F(spot.x).F(spot.y).F(spot.z + 1.f).Out(0); RunCmd(c, &o, 1); }
    { Cmd c(0x0249); c.I(model); RunCmd(c); }
    if (!PedByRef(o)) { GarHelp("Ninguem disponivel agora. Tente de novo."); return false; }
    g.ped = o;
    { Cmd c(0x020A); c.I(g.veh).I(1); RunCmd(c); }                           // destranca
    { Cmd c(0x05CB); c.I(g.ped).I(g.veh).I(30000); RunCmd(c); }               // entrar como motorista
    g.state = GS_PICKUP; g.phase = 0; g.t0 = g.tPhase = GetTickCount(); g.inGarage = 0;
    g_errand = i; g_errandKind = sold ? ER_SOLD : ER_TOGARAGE;
    Log("garagem: %s vem buscar o %s", sold ? "o comprador" : ptype == 8 ? "um membro da Grove" : "a policia", CarName(BlobModel(g.blob)));
    return true;
}
static void FinishPickup(GarCar& g, bool vanish, const char* how) {
    int i = (int)(&g - g_gar);
    bool sold = g_errandKind == ER_SOLD;
    char* v = VehByRef(g.veh);
    if (vanish && g_errandKind == ER_REPAIR) {                                // levado para o conserto: volta em ~10 s
        if (v) CaptureCar(g, v);
        if (PedByRef(g.ped)) { Cmd c(0x009B); c.I(g.ped); RunCmd(c); }
        if (v) { Cmd c(0x00A6); c.I(g.veh); RunCmd(c); }
        g.ped = -1; g.veh = -1; g.state = GS_AWAY;
        RepairGar(g);
        g_errandKind = ER_REPAIRWAIT; g_repairAt = GetTickCount() + 10000;
        Log("garagem: %s levado para o conserto (%s); volta em 10 s", CarName(BlobModel(g.blob)), how);
        return;
    }
    if (vanish) {
        if (v && !sold) CaptureCar(g, v);
        if (PedByRef(g.ped)) { Cmd c(0x009B); c.I(g.ped); RunCmd(c); }
        if (v) { Cmd c(0x00A6); c.I(g.veh); RunCmd(c); }
        g.ped = -1; g.veh = -1;
        g_errand = -1; g_errandKind = ER_NONE;
        Log("garagem: %s levado (%s)", CarName(BlobModel(g.blob)), how);
        if (sold) RemoveGar(i); else g.state = GS_TOGARAGE;
        return;
    }
    if (PedByRef(g.ped)) { Cmd c(0x01C2); c.I(g.ped); RunCmd(c); }           // desistiu: o carro fica onde esta
    g.ped = -1;
    g_errand = -1; g_errandKind = ER_NONE;
    Log("garagem: busca interrompida (%s)", how);
    if (sold) { if (v) *(BYTE*)(v + 0x4A4) = 1; RemoveGar(i); }                 // vendido e pago: vira carro comum
    else g.state = v ? GS_LIVE : GS_AWAY;
}
static void PickupTick(GarCar& g) {
    char* v = VehByRef(g.veh); char* d = PedByRef(g.ped); char* me = PlayerPed();
    DWORD now = GetTickCount();
    if (!me) return;
    if (!v) { FinishPickup(g, true, "o carro sumiu"); return; }
    if (v == PlayerVehicle()) { FinishPickup(g, false, "voce entrou no carro"); return; }
    if (!d || Rd<float>(d, 0x540) <= 0.f) { FinishPickup(g, false, "quem vinha buscar morreu"); return; }
    V3 vp = EntityPos(v), pp = EntityPos(me);
    float dist = Dist2(vp, pp);
    bool onScreen = false;
    { Cmd c(0x02CA); c.I(g.veh); onScreen = RunCmd(c); }
    bool in = (Rd<BYTE>(d, 0x46D) & 1) && Rd<char*>(d, 0x58C) == v;
    if (g.phase == 0) {
        if (in) {
            { Cmd c(0x00AD); c.I(g.veh).F(18.f); RunCmd(c); }
            { Cmd c(0x00AE); c.I(g.veh).I(2); RunCmd(c); }
            { Cmd c(0x00A8); c.I(g.veh); RunCmd(c); }                         // sai dirigindo pela cidade
            g.phase = 1; g.tPhase = now;
        } else if (now - g.t0 > 40000 && !onScreen) FinishPickup(g, true, "fora de vista");
        else if (now - g.t0 > 90000) FinishPickup(g, true, "demorou");
    } else {
        if (dist > 90.f || (!onScreen && dist > 40.f) || now - g.tPhase > 60000) FinishPickup(g, true, "saiu dirigindo");
    }
}

// ---------------------------------------------------------------- acoes da lista
static bool CanUseNow(const GarItem& it, char** liveVeh) {
    *liveVeh = nullptr;
    if (it.kind == 1 && g_gar[it.gi].state == GS_LIVE) {
        char* v = VehByRef(g_gar[it.gi].veh);
        if (v && v == PlayerVehicle()) { GarHelp("Saia do carro antes."); return false; }
        *liveVeh = v;
    }
    return true;
}
static void DoCall() {
    if (g_errand >= 0) { GarHelp("A Grove ja esta ocupada com outro carro seu."); return; }
    if (*(volatile int*)0xB72914 != 0) { GarHelp("Chame o carro ao ar livre."); return; }
    char* v; if (!CanUseNow(g_sel, &v)) return;
    V3 pp = EntityPos(PlayerPed());
    if (g_sel.kind == 0) {
        BYTE* slot = SlotPtr(g_sel.house, g_sel.slot);
        GarCar* g = NewGar(g_sel.house, g_sel.slot, slot);
        if (!g) return;
        g->state = GS_AWAY;
        memcpy(g->plate, g_slotPlate[g_sel.house][g_sel.slot], 9);
        int gi = (int)(g - g_gar);
        if (StartDelivery(gi)) { *(WORD*)(slot + 0x12) = 0; g_slotPlate[g_sel.house][g_sel.slot][0] = 0; }   // sai da vaga
        else RemoveGar(gi);
        return;
    }
    GarCar& g = g_gar[g_sel.gi];
    if (g.state == GS_LIVE && v) {
        if (Dist2(EntityPos(v), pp) < 70.f) { GarHelp("%s esta aqui perto.", CarName(g_sel.model)); return; }
        CaptureCar(g, v); Cmd c(0x00A6); c.I(g.veh); RunCmd(c);
    }
    g.veh = -1; g.state = GS_AWAY;
    StartDelivery(g_sel.gi);
}
static void DoToGarage() {
    char* v; if (!CanUseNow(g_sel, &v)) return;
    GarCar& g = g_gar[g_sel.gi];
    if (g.state == GS_LIVE && v && Dist2(EntityPos(v), EntityPos(PlayerPed())) < 70.f) {
        if (g_errand >= 0) { GarHelp("A Grove ja esta ocupada com outro carro seu."); return; }
        if (StartPickup(g_sel.gi, false)) GarHelp("Um membro da Grove vem buscar o %s.", CarName(g_sel.model));
        return;
    }
    if (g.state == GS_LIVE && v) { CaptureCar(g, v); Cmd c(0x00A6); c.I(g.veh); RunCmd(c); }
    g.veh = -1; g.state = GS_TOGARAGE; g.waitLogged = 0;
    Log("garagem: %s indo para a garagem (casa %d)%s", CarName(g_sel.model), g.house, g.noSpot ? "; sem vaga agora, espera abrir espaco" : "");
    if (g.noSpot) GarHelp("A garagem do %s esta cheia. Ele entra quando abrir espaco.", CarName(g_sel.model));
    else GarHelp("%s vai para a garagem.", CarName(g_sel.model));
}
static void DoRepair() {
    if (g_errand >= 0) { GarHelp("A Grove ja esta ocupada com outro carro seu."); return; }
    if (*(volatile int*)0xB72914 != 0) { GarHelp("Peca o conserto ao ar livre."); return; }
    char* v; if (!CanUseNow(g_sel, &v)) return;
    GarCar& g = g_gar[g_sel.gi];
    const char* name = CarName(g_sel.model);
    if (g.state == GS_LIVE && v && Dist2(EntityPos(v), EntityPos(PlayerPed())) < 70.f) {
        if (StartPickup(g_sel.gi, false)) {
            g_errandKind = ER_REPAIR;
            Log("garagem: %s vai para o conserto; um membro da Grove vem buscar", name);
            GarHelp("Um membro da Grove vem buscar o %s para consertar.", name);
        }
        return;
    }
    if (g.state == GS_LIVE && v) { CaptureCar(g, v); Cmd c(0x00A6); c.I(g.veh); RunCmd(c); }
    g.veh = -1; g.state = GS_AWAY;
    RepairGar(g);
    g_errand = g_sel.gi; g_errandKind = ER_REPAIRWAIT; g_repairAt = GetTickCount() + 10000;
    Log("garagem: %s no conserto (estava longe); volta ate voce em 10 s", name);
    GarHelp("O %s vai para o conserto e volta ate voce, lavado.", name);
}
static void DoSell() {
    char* v; if (!CanUseNow(g_sel, &v)) return;
    int price = g_price;
    if (g_sel.kind == 0) {
        *(WORD*)(SlotPtr(g_sel.house, g_sel.slot) + 0x12) = 0;
        g_slotPlate[g_sel.house][g_sel.slot][0] = 0;
    } else {
        GarCar& g = g_gar[g_sel.gi];
        if (g.state == GS_LIVE && v && Dist2(EntityPos(v), EntityPos(PlayerPed())) < 70.f) {
            if (g_errand >= 0) { GarHelp("O comprador so pode vir depois do servico da Grove em andamento."); return; }
            if (!StartPickup(g_sel.gi, true)) return;
            AddMoney(price);
            Log("garagem: %s vendido por $%d; o comprador vem buscar", CarName(g_sel.model), price);
            GarHelp("%s vendido por $%d. O comprador vem buscar.", CarName(g_sel.model), price);
            return;
        }
        if (g.state == GS_LIVE && v) { Cmd c(0x00A6); c.I(g.veh); RunCmd(c); }
        RemoveGar(g_sel.gi);
    }
    AddMoney(price);
    Log("garagem: %s vendido por $%d", CarName(g_sel.model), price);
    GarHelp("%s vendido por $%d.", CarName(g_sel.model), price);
}
static void DoRepor() {
    GarCar& g = g_gar[g_sel.gi];
    int* money = (int*)(PlayerInfo() + 0xB8);
    if (*money < g_garPrice) { GarHelp("Repor custa $%d. Dinheiro insuficiente.", g_garPrice); return; }
    AddMoney(-g_garPrice);
    // novo e inteiro; entra na garagem pelo mesmo caminho do "mandar para a garagem" (porta fechada, lugar livre)
    g.state = GS_TOGARAGE; g.veh = -1; g.hasDmg = 0; g.health = 0.f; g.dentLen = 0; g.waitLogged = 0;
    Log("garagem: %s reposto por $%d; vai para a garagem (casa %d)", CarName(g_sel.model), g_garPrice, g.house);
    GarHelp("%s reposto por $%d. Ele aparece na garagem.", CarName(g_sel.model), g_garPrice);
}
// ---------------------------------------------------------------- trocar placa (teclado do iPhone pelo overlay, ou do PC)
// Arquivo compartilhado: [128..191] pedido do jogo (GTMK), [192..255] resposta do overlay (GTMA).
#pragma pack(push, 1)
struct KbReqBlock { DWORD magic, seq; BYTE maxLen, kind, pad[2]; char title[32]; char text[16]; BYTE pad2[4]; };
struct KbAnsBlock { DWORD magic, seq, result; char text[16]; BYTE pad[36]; };   // result 1 salvar, 2 cancelar
#pragma pack(pop)
static_assert(sizeof(KbReqBlock) == 64 && sizeof(KbAnsBlock) == 64, "layout kb");
static const DWORD KBREQ_MAGIC = 0x4B4D5447, KBANS_MAGIC = 0x414D5447;          // "GTMK", "GTMA"
static volatile int g_kbMode = 0;                                                  // 0 nada, 1 iPhone, 2 PC
static DWORD g_kbSeq = 0, g_kbT0 = 0, g_kbShowT = 0, g_kbDeadSince = 0;
static GarItem g_kbItem;
static char  g_kbBuf[9];
static bool  g_kbPrev[256];
static volatile bool g_kbCancel = false;
static void KbWriteReq(const KbReqBlock& r) {
    if (g_file == INVALID_HANDLE_VALUE) return;
    EnterCriticalSection(&g_io);
    OVERLAPPED o = {}; o.Offset = 128; DWORD n;
    WriteFile(g_file, &r, sizeof r, &n, &o);
    LeaveCriticalSection(&g_io);
}
static bool KbReadAns(KbAnsBlock* a) {
    if (g_file == INVALID_HANDLE_VALUE) return false;
    EnterCriticalSection(&g_io);
    OVERLAPPED o = {}; o.Offset = 192; DWORD n = 0;
    BOOL ok = ReadFile(g_file, a, sizeof *a, &n, &o);
    LeaveCriticalSection(&g_io);
    return ok && n == sizeof *a && a->magic == KBANS_MAGIC;
}
static void KbSnapshotKeys() {
    for (int k = 0; k < 256; k++) g_kbPrev[k] = (GetAsyncKeyState(k) & 0x8000) != 0;
}
static void StartPlate() {
    if (g_menu >= 0) { Cmd c(0x08DA); c.I(g_menu); RunCmd(c); g_menu = -1; }
    g_menuOpen = false; g_level = 0;                                          // o telefone fica no ouvido
    g_kbItem = g_sel; g_kbT0 = GetTickCount(); g_kbCancel = false; g_kbDeadSince = 0;
    const char* cur = ItemPlate(g_kbItem);
    HostBlock h;
    bool overlay = ReadHost(&h);
    if (overlay && h.version < 4) {                                           // overlay antigo: sem teclado
        PlayerControl(true); g_phoneOffAt = GetTickCount() + 1200;
        Log("garagem: overlay de toque protocolo %u nao tem o teclado da placa (precisa do 3.3)", (unsigned)h.version);
        GarHelp("Para digitar a placa no iPhone, instale o IPA novo (overlay 3.3).");
        return;
    }
    if (overlay) {
        DWORD seq = GetTickCount() | 1; if (seq == g_kbSeq) seq += 2;
        g_kbSeq = seq; g_kbMode = 1;
        KbReqBlock r = {};
        r.magic = KBREQ_MAGIC; r.seq = seq; r.maxLen = 8; r.kind = 1;
        snprintf(r.title, sizeof r.title, "Placa do %s", CarName(g_kbItem.model));
        snprintf(r.text, sizeof r.text, "%s", cur);
        KbWriteReq(r);
        Log("garagem: placa do %s pelo teclado do iPhone", CarName(g_kbItem.model));
    } else {
        g_kbMode = 2; snprintf(g_kbBuf, sizeof g_kbBuf, "%s", cur); g_kbShowT = 0;
        KbSnapshotKeys();
        Log("garagem: placa do %s pelo teclado do PC", CarName(g_kbItem.model));
    }
}
static void ApplyPlate(const GarItem& it, const char* p) {
    const char* name = CarName(it.model);
    if (!ItemValid(it)) { GarHelp("A situacao do %s mudou. Tente de novo.", name); return; }
    if (it.kind == 0) {
        memcpy(g_slotPlate[it.house][it.slot], p, 9);
        Log("garagem: placa do %s (casa %d vaga %d): \"%s\"", name, it.house, it.slot + 1, p);
        if (p[0]) GarHelp("Placa do %s: %s. Aparece quando ele sair da garagem.", name, p);
        else GarHelp("O %s volta a usar a placa do jogo.", name);
        return;
    }
    GarCar& g = g_gar[it.gi];
    char* v = g.state == GS_LIVE ? VehByRef(g.veh) : nullptr;
    if (v && !ApplyPlateNow(v, p)) { GarHelp("O %s nao tem placa.", name); return; }
    memcpy(g.plate, p, 9);
    Log("garagem: placa do %s: \"%s\"%s", name, p, v ? " (trocada na hora)" : " (quando ele surgir)");
    if (!p[0]) GarHelp("O %s volta a usar a placa do jogo.", name);
    else if (v) GarHelp("Placa do %s: %s.", name, p);
    else GarHelp("Placa do %s: %s. Aparece quando ele surgir.", name, p);
}
static void EndPlate(bool ok, const char* text) {
    if (g_kbMode == 1) { KbReqBlock r = {}; r.magic = KBREQ_MAGIC; KbWriteReq(r); }   // seq 0: overlay fecha o teclado
    g_kbMode = 0;
    PlayerControl(true);
    g_phoneOffAt = GetTickCount() + 1200;
    if (!ok) { Log("garagem: troca de placa cancelada"); GarHelp("Placa mantida."); return; }
    char p[9]; CleanPlate(text, p);
    ApplyPlate(g_kbItem, p);
}
static void PlateTick() {
    if (!g_kbMode) return;
    char* me = PlayerPed();
    DWORD now = GetTickCount();
    if (!me || Rd<float>(me, 0x540) <= 0.f || PlayerVehicle() || g_kbCancel || now - g_kbT0 > 180000) { EndPlate(false, ""); return; }
    if (g_kbMode == 1) {
        KbAnsBlock a;
        if (KbReadAns(&a) && a.seq == g_kbSeq) { a.text[15] = 0; EndPlate(a.result == 1, a.text); return; }
        HostBlock h;
        if (ReadHost(&h)) g_kbDeadSince = 0;
        else if (!g_kbDeadSince) g_kbDeadSince = now;
        else if (now - g_kbDeadSince > 3000) { Log("garagem: o overlay parou de responder"); EndPlate(false, ""); }
        return;
    }
    // teclado do PC: letras, numeros, espaco; Backspace apaga; Enter salva; Esc cancela
    if (MenuActive()) { EndPlate(false, ""); return; }
    bool changed = false;
    for (int k = 0; k < 256; k++) {
        bool down = (GetAsyncKeyState(k) & 0x8000) != 0, edge = down && !g_kbPrev[k];
        g_kbPrev[k] = down;
        if (!edge) continue;
        int len = (int)strlen(g_kbBuf);
        char c = 0;
        if (k >= 'A' && k <= 'Z') c = (char)k;
        else if (k >= '0' && k <= '9') c = (char)k;
        else if (k >= VK_NUMPAD0 && k <= VK_NUMPAD9) c = (char)('0' + k - VK_NUMPAD0);
        else if (k == VK_SPACE) c = ' ';
        if (c) { if (len < 8 && !(c == ' ' && len == 0)) { g_kbBuf[len] = c; g_kbBuf[len + 1] = 0; changed = true; } }
        else if (k == VK_BACK) { if (len) { g_kbBuf[len - 1] = 0; changed = true; } }
        else if (k == VK_RETURN) { EndPlate(true, g_kbBuf); return; }
        else if (k == VK_ESCAPE) { EndPlate(false, ""); return; }
    }
    if (changed || now - g_kbShowT > 4000) {
        g_kbShowT = now;
        GarHelp("Placa do %s: %s_  (Enter salva, Esc cancela)", CarName(g_kbItem.model), g_kbBuf);
    }
}
static void PickRow(int row) {
    if (g_level == 3) {
        if (row < 0 || row >= g_nActs) return;
        if (g_acts[row] == AC_ROOT_GAR) OpenGarageList();
        else if (g_acts[row] == AC_ROOT_ARM) { CloseMenu(true); DealerCall(); }
        else if (g_acts[row] == AC_ROOT_VIG) {
            CloseMenu(true);
            if (VigActiveNow()) Vig64Stop();
            else { const char* why = nullptr; if (Vig64CanStart(&why)) Vig64Start("pelo telefone"); else if (why) GarHelp("%s", why); }
        } else if (g_acts[row] == AC_ROOT_RC) {
            CloseMenu(true);
            int n = RcCount(); RcReleaseAll("dispensados pelo telefone");
            GarHelp(n > 1 ? "Os %d policiais foram dispensados." : "O policial foi dispensado.", n);
        }
        return;
    }
    if (g_level == 0) {
        int first = g_page * 11, n = g_nItems - first; if (n > 11) n = 11;
        if (row >= n) {                                                       // "Mais..." / "Voltar ao inicio"
            if (g_nItems > 11) { g_page = g_nItems > first + 11 ? g_page + 1 : 0; ShowList(); }
            return;
        }
        g_sel = g_items[first + row];
        if (!ItemValid(g_sel)) { BuildItems(); ShowList(); return; }
        ShowActions();
        return;
    }
    if (row < 0 || row >= g_nActs) return;
    int act = g_acts[row];
    if (act == AC_NO) { ShowActions(); return; }
    if (act == AC_SELL) { ShowConfirm(); return; }
    if (!ItemValid(g_sel)) { CloseMenu(false); GarHelp("A situacao desse carro mudou. Abra a lista de novo."); return; }
    if (act == AC_PLATE) { StartPlate(); return; }
    CloseMenu(true);                                                          // todas as acoes sao uma ligacao
    if (act == AC_CALL) DoCall();
    else if (act == AC_TOGARAGE) DoToGarage();
    else if (act == AC_YES) DoSell();
    else if (act == AC_REPOR) DoRepor();
    else if (act == AC_REPAIR) DoRepair();
}
static void MenuTick() {
    if (!g_menuOpen) return;
    char* me = PlayerPed();
    if (!me || MenuActive() || PlayerVehicle() || Rd<float>(me, 0x540) <= 0.f) { CloseMenu(false); return; }
    if (g_garCancel) {                                                        // Pular volta um nivel
        g_garCancel = false;
        if (g_level == 2) ShowActions();
        else if (g_level == 1) { BuildItems(); if (!g_nItems) CloseMenu(false); else { if (g_page * 11 >= g_nItems) g_page = 0; ShowList(); } }
        else if (g_level == 0) ShowRoot();
        else { CloseMenu(false); Log("telefone: menu fechado"); }
        return;
    }
    if (g_garSel) {
        g_garSel = false;
        int row = 0;
        { Cmd c(0x08D7); c.I(g_menu).Out(0); RunCmd(c, &row, 1); }
        PickRow(row);
    }
}

// carros meus na rua: nao somem, saem da memoria quando longe, voltam quando perto, destruidos ficam para repor
static DWORD g_garTick = 0;
static void CarsTick() {
    DWORD now = GetTickCount();
    if (now - g_garTick < 500) return;
    g_garTick = now;
    char* me = PlayerPed();
    if (!me) return;
    V3 pp = EntityPos(me);
    bool outside = *(volatile int*)0xB72914 == 0;
    char* pv = PlayerVehicle();
    static DWORD spotTick = 0;
    if (now - spotTick > 2000) { spotTick = now; ComputeSpots(); }
    for (int i = g_nGar - 1; i >= 0; i--) {
        GarCar& g = g_gar[i];
        if (g.state == GS_LIVE) {
            char* v = VehByRef(g.veh);
            if (!v && g.parked) {                                           // a garagem apagou sem guardar: nao coube
                g.state = GS_TOGARAGE; g.veh = -1; g.parked = 0; g.waitLogged = 0;
                Log("garagem: %s nao coube na garagem onde voce deixou; vai para a garagem dele quando houver espaco", CarName(BlobModel(g.blob)));
                continue;
            }
            if (!v) { g.state = GS_AWAY; g.veh = -1; Log("garagem: %s saiu da memoria; volta quando voce chegar perto", CarName(BlobModel(g.blob))); continue; }
            if ((Rd<BYTE>(v, 0x36) >> 3) == 5) {                              // STATUS_WRECKED
                g.state = GS_DEAD; *(BYTE*)(v + 0x4A4) = 1; g.veh = -1; g.dentLen = 0;
                Log("garagem: %s destruido", CarName(BlobModel(g.blob)));
                GarHelp("Seu %s foi destruido. Reponha pelo telefone por $%d.", CarName(BlobModel(g.blob)), g_garPrice);
                continue;
            }
            V3 vp = EntityPos(v);
            if (g.inGarage && Dist2(vp, *(V3*)g.home) > 25.f) { g.inGarage = 0; *(BYTE*)(v + 0x4A4) = 2; }
            if (g.inGarage) continue;
            // parado dentro de uma garagem sua: o jogo so guarda carros que nao sao de script
            bool parked = v != pv && HideoutGarageAt(vp) != nullptr;
            if (parked != (g.parked != 0)) { g.parked = parked; *(BYTE*)(v + 0x4A4) = parked ? 1 : 2; if (parked) Log("garagem: %s estacionado numa garagem sua; ela guarda quando voce sair", CarName(BlobModel(g.blob))); }
            if (!parked && Rd<BYTE>(v, 0x4A4) != 2) *(BYTE*)(v + 0x4A4) = 2;
            CaptureCar(g, v);
            if (!parked && v != pv && Dist2(vp, pp) > 350.f) {
                Cmd c(0x00A6); c.I(g.veh); RunCmd(c);
                g.state = GS_AWAY; g.veh = -1;
                Log("garagem: %s longe; guardado na memoria em (%.0f, %.0f)", CarName(BlobModel(g.blob)), vp.x, vp.y);
            }
        } else if (g.state == GS_AWAY && outside && g_errand != i) {
            if (Dist2(BlobPos(g.blob), pp) > 180.f) continue;
            RequestBlobModels(g.blob);
            if (!BlobModelsLoaded(g.blob)) continue;
            if (char* v = SpawnCar(g, g.blob)) {
                g.veh = VehRef(v); g.state = GS_LIVE; g.parked = 0;
                Log("garagem: %s de volta em (%.0f, %.0f)%s", CarName(BlobModel(g.blob)), BlobPos(g.blob).x, BlobPos(g.blob).y, g.dentLen ? " com os amassados" : "");
            }
        } else if (g.state == GS_TOGARAGE) {
            // so entra na vaga com a garagem fechada e voce longe dela (a garagem apaga as vagas antes de guardar)
            V3 home = *(V3*)g.home;
            if (Dist2(home, pp) < 60.f) continue;
            char* gr = GarageOfHouse(g.house, home);
            if (gr && Rd<BYTE>(gr, 0x4D) != 0) continue;                       // porta aberta: os carros dela estao no mundo
            int s = -1;
            for (int k = 0; k < 4 && s < 0; k++) if (!BlobModel(SlotPtr(g.house, k))) s = k;
            BYTE b[0x40]; memcpy(b, g.blob, sizeof b);
            memcpy(b, g.home, 12); memcpy(b + 0x3C, g.homeDir, 3);
            bool moved = false;
            if (s >= 0 && gr) {                                                // lugar livre, sem encostar nos que estao la
                Box2 obs[8]; int n = GarageObstacles(g.house, gr, obs, 8);
                float w, l; Dims(BlobModel(b), &w, &l);
                Box2 c;
                if (!FindFreeSpot(gr, w, l, home, g.homeDir[0], g.homeDir[1], obs, n, &c)) s = -2;
                else if (fabsf(c.x - home.x) > 0.05f || fabsf(c.y - home.y) > 0.05f || c.fx * g.homeDir[0] + c.fy * g.homeDir[1] < 0.f) {
                    float* bp = (float*)b; bp[0] = c.x; bp[1] = c.y;
                    b[0x3C] = (BYTE)(signed char)(int)floorf(c.fx * 100.f + .5f); b[0x3D] = (BYTE)(signed char)(int)floorf(c.fy * 100.f + .5f); b[0x3E] = 0;
                    moved = true;
                }
            }
            if (s < 0) {
                if (!g.waitLogged) { g.waitLogged = 1; Log("garagem: %s espera vaga na casa %d (%s)", CarName(BlobModel(b)), g.house, s == -1 ? "as 4 vagas estao ocupadas" : "nao ha espaco livre dentro dela"); }
                continue;
            }
            memcpy(SlotPtr(g.house, s), b, sizeof b);
            memcpy(g_slotPlate[g.house][s], g.plate, 9);
            Log("garagem: %s chegou na garagem (casa %d vaga %d%s)", CarName(BlobModel(b)), g.house, s + 1, moved ? ", num lugar livre ao lado" : "");
            RemoveGar(i);
        }
    }
}

// ---------------------------------------------------------------- junto do save: GTASAsfN.b.gtmgar
// versao 3: cabecalho, placas das 80 vagas (8 letras cada), e para cada carro seu fora da garagem: registro,
// amassados (tamanho + dados) e placa (8). A versao 2 nao tem as placas; a 1 nem os amassados.
#pragma pack(push, 1)
struct GarFileRec { BYTE state, house, slot, playerCar, hasDmg, pad[3]; BYTE blob[0x40]; float home[3]; signed char homeDir[3], pad2; float health; BYTE dmg[0x18]; };
#pragma pack(pop)
static const DWORD GAR_MAGIC = 0x52414747;                                    // 'GGAR'
static GenericSave_t g_origGenericSave = (GenericSave_t)0x5D13E0;
static bool __cdecl GenericSaveDetour(int a) {
    bool ok = g_origGenericSave(a);
    if (ok) ArmSave();
    if (!ok || !g_garOn) return ok;
    char path[MAX_PATH]; snprintf(path, MAX_PATH, "%s.gtmgar", SaveFileName);
    char* pv = PlayerVehicle() ? PlayerVehicle() : g_saveVeh;                  // o DoSave tira o CJ do carro so para gravar
    FILE* f = fopen(path, "wb");
    if (!f) { Log("garagem: nao gravou %s", path); return ok; }
    DWORD hdr[4] = { GAR_MAGIC, 3, *TimeMs, 0 };
    fwrite(hdr, sizeof hdr, 1, f);
    int plates = 0;
    for (int h = 0; h < 20; h++) for (int s = 0; s < 4; s++) {
        char t[8] = {};
        if (BlobModel(SlotPtr(h, s)) && g_slotPlate[h][s][0]) { memcpy(t, g_slotPlate[h][s], 8); plates++; }
        fwrite(t, 8, 1, f);
    }
    int n = 0, dents = 0;
    for (int i = 0; i < g_nGar; i++) {
        GarCar& g = g_gar[i];
        if (g.state == GS_NONE || (g.state == GS_PICKUP && g_errandKind == ER_SOLD && g_errand == i)) continue;
        char* v = VehByRef(g.veh);
        if (v && g.state == GS_DELIVER) { V3 vp = EntityPos(v); memcpy(g.blob, &vp, 12); }   // as marcas de entrega nao vao
        else if (v && g.state != GS_DEAD) CaptureCar(g, v);
        if (g.state == GS_LIVE && g.inGarage) continue;                       // a garagem guarda esse
        GarFileRec r;
        memset(&r, 0, sizeof r);
        r.state = g.state == GS_DEAD ? GS_DEAD : g.state == GS_TOGARAGE ? GS_TOGARAGE : GS_AWAY;
        r.house = g.house; r.slot = g.slot;
        r.playerCar = v && v == pv; r.hasDmg = g.hasDmg;
        memcpy(r.blob, g.blob, sizeof r.blob); memcpy(r.home, g.home, 12); memcpy(r.homeDir, g.homeDir, 3);
        r.health = g.health; memcpy(r.dmg, g.dmg, sizeof r.dmg);
        int dl = g.state == GS_DEAD ? 0 : g.dentLen;
        fwrite(&r, sizeof r, 1, f);
        fwrite(&dl, 4, 1, f);
        if (dl) { fwrite(g.dent, dl, 1, f); dents++; }
        char t[8] = {}; memcpy(t, g.plate, 8);
        fwrite(t, 8, 1, f);
        if (t[0]) plates++;
        n++;
    }
    fseek(f, 12, SEEK_SET); fwrite(&n, 4, 1, f);
    fclose(f);
    Log("garagem: %d carros seus fora da garagem gravados junto do save (%d com amassados, %d placas)", n, dents, plates);
    return ok;
}
// o arquivo do save carregado e lido logo depois do load (as placas das vagas valem antes de a garagem abrir);
// os carros de fora entram uns quadros depois, quando o CJ ja voltou (e o carro dele, pelo .gtmveh)
static int   g_garLoadPending = 0;
static BYTE* g_garFile = nullptr;
static long  g_garFileLen = 0;
static int   g_garFileSlot = 0;
static void GarOnLoad() {                                                    // logo depois de carregar: o mundo e outro
    ArmOnLoad();
    for (int i = 0; i < g_nGar; i++) free(g_gar[i].dent);
    g_nGar = 0; g_errand = -1; g_errandKind = ER_NONE; g_menuOpen = false; g_menu = -1; g_phoneOffAt = 0; g_level = 0;
    g_kbMode = 0;
    memset(g_slotPlate, 0, sizeof g_slotPlate);
    free(g_garFile); g_garFile = nullptr; g_garFileLen = 0;
    g_garLoadPending = 0;
    if (!g_garOn) return;
    char saved[MAX_PATH]; snprintf(saved, sizeof saved, "%s", SaveFileName);
    for (int s = 0; s < 8 && !g_garFile; s++) {
        MakeValidSaveName(s);
        char path[MAX_PATH]; snprintf(path, MAX_PATH, "%s.gtmgar", SaveFileName);
        FILE* f = fopen(path, "rb");
        if (!f) continue;
        DWORD hdr[4] = {};
        if (fread(hdr, sizeof hdr, 1, f) == 1 && hdr[0] == GAR_MAGIC && hdr[1] >= 1 && hdr[1] <= 3 && hdr[2] == g_loadedTimeMs && hdr[3] <= 80) {
            fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
            if (len > 0 && len < 40000000) {
                g_garFile = (BYTE*)malloc(len);
                if (g_garFile && fread(g_garFile, len, 1, f) == 1) { g_garFileLen = len; g_garFileSlot = s; }
                else { free(g_garFile); g_garFile = nullptr; }
            }
        }
        fclose(f);
    }
    strcpy(SaveFileName, saved);
    if (!g_garFile) { g_garLoadPending = 3; return; }
    const DWORD* hdr = (const DWORD*)g_garFile;
    int plates = 0;
    if (hdr[1] >= 3 && g_garFileLen >= 16 + 640) {
        for (int h = 0; h < 20; h++) for (int s = 0; s < 4; s++) {
            char t[9] = {}; memcpy(t, g_garFile + 16 + (h * 4 + s) * 8, 8);
            CleanPlate(t, g_slotPlate[h][s]);
            if (g_slotPlate[h][s][0]) plates++;
        }
    }
    if (plates) Log("garagem: %d placas das vagas carregadas", plates);
    g_garLoadPending = 3;
}
static void GarLoadTick() {
    if (!g_garLoadPending) return;
    if (g_pendingRestore || !PlayerPed()) return;                             // depois do veiculo do autosave
    if (--g_garLoadPending > 1) return;
    g_garLoadPending = 0;
    if (!g_garFile) return;
    const DWORD* hdr = (const DWORD*)g_garFile;
    DWORD ver = hdr[1];
    long pos = 16 + (ver >= 3 ? 640 : 0);
    int dents = 0, n0 = g_nGar;
    auto take = [&](void* dst, long len) { if (pos + len > g_garFileLen) return false; if (dst) memcpy(dst, g_garFile + pos, len); pos += len; return true; };
    for (DWORD k = 0; k < hdr[3]; k++) {
        GarFileRec r; int dl = 0; char t[9] = {};
        if (!take(&r, sizeof r)) break;
        if (ver >= 2 && (!take(&dl, 4) || dl < 0 || dl > 400000 || pos + dl > g_garFileLen)) break;
        const BYTE* dentData = g_garFile + pos; pos += dl;
        if (ver >= 3 && !take(t, 8)) break;
        if (IsImpoundHouse(r.house)) continue;                                // de versoes antigas: patio nao e seu
        GarCar* g = NewGar(r.house, r.slot, r.blob);
        if (!g) break;
        g->state = r.state; g->hasDmg = r.hasDmg; g->health = r.health;
        memcpy(g->dmg, r.dmg, sizeof g->dmg); memcpy(g->home, r.home, 12); memcpy(g->homeDir, r.homeDir, 3);
        CleanPlate(t, g->plate);
        if (dl && (g->dent = (BYTE*)malloc(dl)) != nullptr) { memcpy(g->dent, dentData, dl); g->dentLen = g->dentCap = dl; dents++; }
        if (r.playerCar) {                                                   // o CJ voltou dentro dele pelo .gtmveh
            char* pv = PlayerVehicle();
            if (pv && Rd<short>(pv, 0x22) == BlobModel(r.blob)) {
                g->state = GS_LIVE; g->veh = VehRef(pv); *(BYTE*)(pv + 0x4A4) = 2;
                NoteDims(pv);
                if (g->dentLen && g_ldImport) g_ldImport(pv, g->dent, g->dentLen);
                if (g->plate[0]) ApplyPlateNow(pv, g->plate);
            }
        }
    }
    Log("garagem: %d carros seus fora da garagem carregados (slot %d, versao %u, %d com amassados)", g_nGar - n0, g_garFileSlot + 1, (unsigned)ver, dents);
    free(g_garFile); g_garFile = nullptr; g_garFileLen = 0;
}

// Status + Pular abre a lista; com a lista aberta, Atirar escolhe e Pular volta
static bool g_garCombo = false, g_garFireWas = false, g_garJumpWas = true;
static WORD g_garEat = 0;                                                    // 3.46: Atirar/Pular do menu, segurados ate soltar
static void GarageCombo(WORD buttons, PadState& p) {
    if (!g_garOn && !g_armOn) return;
    const WORD LB = 0x0100, FIRE = 0x2000, JUMP = 0x4000;
    if (g_kbMode) {                                                           // digitando a placa: Pular cancela
        bool jump = (buttons & JUMP) != 0;
        if (jump && !g_garJumpWas) g_garCancel = false, g_kbCancel = true;
        g_garJumpWas = jump; g_garFireWas = true;
        p.wButtons &= (WORD)~(FIRE | JUMP);
        return;
    }
    if (g_menuOpen || g_shopOpen) {
        bool fire = (buttons & FIRE) != 0, jump = (buttons & JUMP) != 0;
        if (fire && !g_garFireWas) g_garSel = true;
        if (jump && !g_garJumpWas && GetTickCount() - g_menuOpenAt > 400) g_garCancel = true;
        g_garFireWas = fire; g_garJumpWas = jump;
        p.wButtons &= (WORD)~(FIRE | JUMP);
        g_garEat = FIRE | JUMP;
        return;
    }
    // 3.46: o menu (traficante, armeiro, garagem) fechou com o dedo ainda no Atirar ou no Pular (ex.: "Dispensar"):
    // o botao so volta para o jogo depois de solto. Antes o CJ dava um tiro ao dispensar o traficante.
    if (g_garEat) { g_garEat &= buttons; p.wButtons &= (WORD)~g_garEat; }
    g_garFireWas = true; g_garJumpWas = true;
    if (!(buttons & LB)) { g_garCombo = false; return; }
    if (!g_garCombo) {
        if (!(buttons & JUMP)) return;
        if (g_ctxCache != CTX_FOOT || (g_info.state & (S_ACTPHONE | S_ACTWEAPON | S_ACTOTHER))) return;
        g_garCombo = true;
        g_garReq = true;
        Log("garagem: Status + Pular");
    }
    p.wButtons &= (WORD)~JUMP;
}

static void RewardsTick();
static void GarageTick() {
    RewardsTick();
    if (!g_garOn && !g_armOn) return;
    if (g_garOn) { LdResolve(); GarLoadTick(); }
    if (g_phoneOffAt && !g_kbMode && GetTickCount() > g_phoneOffAt) { g_phoneOffAt = 0; Phone(false); }
    if (g_garReq) {
        g_garReq = false;
        char* me = PlayerPed();
        if (me && !g_menuOpen && !g_shopOpen && !g_kbMode && !PlayerVehicle() && !MenuActive()) OpenMenu();
    }
    if (g_garOn) PlateTick();
    MenuTick();
    if (g_garOn) {
        if (g_errand >= 0 && g_errand < g_nGar) {
            GarCar& g = g_gar[g_errand];
            if (g_errandKind == ER_DELIVER) DeliveryTick(g);
            else if (g_errandKind == ER_REPAIRWAIT) RepairWaitTick(g);
            else PickupTick(g);
        }
        CarsTick();
    }
    DealerTick();
    VigTick();
    WantedPerkTick();
}
static void InstallGarage() {
    bool a = g_garOn && MH_CreateHook((void*)0x447E40, (void*)&RestoreCarDetour, (void**)&g_origRestoreCar) == MH_OK && MH_EnableHook((void*)0x447E40) == MH_OK;
    bool b = g_garOn && MH_CreateHook((void*)0x449760, (void*)&StoreCarDetour, (void**)&g_origStoreCar) == MH_OK && MH_EnableHook((void*)0x449760) == MH_OK;
    bool c = MH_CreateHook((void*)0x5D13E0, (void*)&GenericSaveDetour, (void**)&g_origGenericSave) == MH_OK && MH_EnableHook((void*)0x5D13E0) == MH_OK;
    g_garHooked = a && b;
    Log("garagem: ganchos RestoreCar=%s StoreCar=%s save=%s", a ? "ok" : "FALHOU", b ? "ok" : "FALHOU", c ? "ok" : "FALHOU");
}

// ---------------------------------------------------------------- traficante de armas ([Traficante] Enabled=1)
// Status + Pular abre o telefone com "Carro da garagem" e "Traficante de armas". A ligacao custa CallPrice; ele
// vem numa van preta (Burrito), para perto do CJ e abre as portas de tras. Na traseira da van abre o menu de
// compra (analogico escolhe, Atirar compra, Pular volta). Se o CJ se afastar mais que LeaveDistance, ou se ninguem
// comprar nada por 3 minutos, ele fecha as portas e vai embora acelerando.
// Precos: o que a AmmuNation ja vende na fase atual sai Discount% mais barato; o que ela ainda nao vende ("fora
// de epoca") custa LockedMultiplier vezes mais (arma, caixa e pacote), com no maximo LockedBoxesPerDay caixas por
// arma por dia do jogo. A fase e a mesma conta do script da loja (ammu.scm, em script.img): a AmmuNation abre com
// Doberman ($67 = 1) e mostra os N primeiros itens da secao ammun do shopping.dat conforme os contadores de
// missao ($453 ... $597). Lanca-foguetes, teleguiado, lanca-chamas e minigun: preco da tabela do shopping.dat e sem
// limite so com Las Venturas liberada (estatistica 181 >= 2, depois de Yay Ka-Boom-Boom); antes, valem as regras
// de fora de epoca. Molotov, gas, corpo a corpo e equipamento: sempre liberados, com preco estimado.
// Policia (portas abertas; fechadas, ela ignora): um policial a menos de 40 m (80 m de helicoptero) com linha de
// visao ate a traseira da van ve. Com o menu aberto, o menu fecha, o CJ ganha 2 estrelas e o traficante foge
// (continua atendendo). Sem o menu, as viaturas perseguem a van, ele foge e fica 1 dia do jogo sem atender. Se a
// policia ja esta atras do CJ e chega perto, ele so foge e continua atendendo. Atacar o traficante ou a van:
// ele foge e fica 1 dia sem atender.
// O dia do jogo, o "volta a atender" e as caixas compradas no dia ficam num arquivo junto do save (.gtmarm).
static DWORD GameMs() { return *TimeMs; }                                     // para no menu de pausa
enum { AG_AMMU = 0, AG_HEAVY, AG_FREE };
enum { CT_PIST = 0, CT_SHOT, CT_SMG, CT_ASSAULT, CT_RIFLE, CT_HEAVY, CT_THROWN, CT_MELEE, CT_EQUIP, CT_COUNT };
static const char* const kArmCat[CT_COUNT] = { "Pistolas", "Escopetas", "Submetralhadoras", "Fuzis", "Rifles",
                                               "Pesadas", "Arremesso", "Corpo a corpo", "Equipamento" };
static const int ARM_ARMOUR = 200;                                            // "tipo" do colete (nao e arma)
struct ArmItem {
    int  type, cat, group, tier, slot;   // tier: posicao na lista da AmmuNation (1..16), so AG_AMMU
    bool pack;                           // a compra e o proprio pacote (arremesso, spray, extintor)
    const char* key; const char* name;
    int  price, ammo;                    // preco base (AmmuNation / tabela / estimado) e o que vem junto
    int  box, boxPrice;                  // caixa avulsa: tiros e preco (0 = sem caixa)
    const char* boxName;
    int  model, model2;
};
static ArmItem g_armItems[] = {
    { 22, CT_PIST,    AG_AMMU,  1, 2, false, "Pistola",            "Pistola 9mm",            200,  30,  50,   30, "Caixa 9mm",        346, 0 },
    { 23, CT_PIST,    AG_AMMU,  9, 2, false, "PistolaSilenciador", "Pistola silenciada",     600,  30,  50,   30, "Caixa .45 ACP subsonica", 347, 0 },
    { 24, CT_PIST,    AG_AMMU, 15, 2, false, "DesertEagle",        "Desert Eagle",          1200,  15,  20,   80, "Caixa .50 AE",     348, 0 },
    { 25, CT_SHOT,    AG_AMMU,  4, 3, false, "Escopeta",           "Escopeta",               600,  15,  25,   45, "Caixa calibre 12", 349, 0 },
    { 26, CT_SHOT,    AG_AMMU,  8, 3, false, "CanoSerrado",        "Cano serrado",           800,  12,  25,   45, "Caixa calibre 12", 350, 0 },
    { 27, CT_SHOT,    AG_AMMU, 16, 3, false, "EscopetaCombate",    "Escopeta de combate",   1000,  10,  25,   45, "Caixa calibre 12", 351, 0 },
    { 32, CT_SMG,     AG_AMMU,  2, 4, false, "Tec9",               "Tec-9",                  300,  60,  50,   30, "Caixa 9mm",        372, 0 },
    { 28, CT_SMG,     AG_AMMU,  3, 4, false, "MicroSMG",           "Micro SMG",              500,  60,  50,   30, "Caixa 9mm",        352, 0 },
    { 29, CT_SMG,     AG_AMMU,  7, 4, false, "SMG",                "SMG",                   2000,  90,  50,   30, "Caixa 9mm",        353, 0 },
    { 30, CT_ASSAULT, AG_AMMU, 13, 5, false, "AK47",               "AK-47",                 3500, 120,  50,   50, "Caixa 7.62x39",    355, 0 },
    { 31, CT_ASSAULT, AG_AMMU, 14, 5, false, "M4",                 "M4",                    4500, 150,  50,   50, "Caixa 5.56",       356, 0 },
    { 33, CT_RIFLE,   AG_AMMU, 11, 6, false, "RifleCaca",          "Rifle de caca",         1000,  20,  20,   55, "Caixa .308",       357, 0 },
    { 34, CT_RIFLE,   AG_AMMU, 12, 6, false, "RiflePrecisao",      "Rifle de precisao",     5000,  10,  20,   55, "Caixa .308",       358, 0 },
    { 35, CT_HEAVY,   AG_HEAVY, 0, 7, false, "LancaFoguetes",      "Lanca-foguetes",       10000,   5,   1, 1000, "Foguete",    359, 0 },
    { 36, CT_HEAVY,   AG_HEAVY, 0, 7, false, "Teleguiado",         "Teleguiado",           12000,   5,   1, 1500, "Foguete",    360, 0 },
    { 37, CT_HEAVY,   AG_HEAVY, 0, 7, false, "LancaChamas",        "Lanca-chamas",          6000, 500, 500,  300, "Tanque",     361, 0 },
    { 38, CT_HEAVY,   AG_HEAVY, 0, 7, false, "Minigun",            "Minigun",              10000, 500, 500, 1200, "Lata 7.62",       362, 0 },
    { 16, CT_THROWN,  AG_AMMU,  5, 8, true,  "Granadas",           "Granadas",               300,   5,   0,    0, "",           342, 0 },
    { 17, CT_THROWN,  AG_FREE,  0, 8, true,  "GasLacrimogeneo",    "Gas lacrimogeneo",       250,   5,   0,    0, "",           343, 0 },
    { 18, CT_THROWN,  AG_FREE,  0, 8, true,  "Molotov",            "Coquetel molotov",       150,   5,   0,    0, "",           344, 0 },
    { 39, CT_THROWN,  AG_AMMU, 10, 8, true,  "ExplosivoRemoto",    "Explosivo remoto",      2000,   1,   0,    0, "",           363, 364 },
    {  1, CT_MELEE,   AG_FREE,  0, 0, false, "SocoIngles",         "Soco ingles",             40,   1,   0,    0, "",           331, 0 },
    {  2, CT_MELEE,   AG_FREE,  0, 1, false, "TacoGolfe",          "Taco de golfe",           60,   1,   0,    0, "",           333, 0 },
    {  3, CT_MELEE,   AG_FREE,  0, 1, false, "Cassetete",          "Cassetete",               80,   1,   0,    0, "",           334, 0 },
    {  4, CT_MELEE,   AG_FREE,  0, 1, false, "Faca",               "Faca",                    60,   1,   0,    0, "",           335, 0 },
    {  5, CT_MELEE,   AG_FREE,  0, 1, false, "TacoBeisebol",       "Taco de beisebol",        40,   1,   0,    0, "",           336, 0 },
    {  6, CT_MELEE,   AG_FREE,  0, 1, false, "Pa",                 "Pa",                      30,   1,   0,    0, "",           337, 0 },
    {  7, CT_MELEE,   AG_FREE,  0, 1, false, "TacoSinuca",         "Taco de sinuca",          30,   1,   0,    0, "",           338, 0 },
    {  8, CT_MELEE,   AG_FREE,  0, 1, false, "Katana",             "Katana",                 400,   1,   0,    0, "",           339, 0 },
    {  9, CT_MELEE,   AG_FREE,  0, 1, false, "Motosserra",         "Motosserra",             800,   1,   0,    0, "",           341, 0 },
    { ARM_ARMOUR, CT_EQUIP, AG_AMMU, 6, 0, false, "Colete",        "Colete",                 200,   0,   0,    0, "",             0, 0 },
    { 46, CT_EQUIP,   AG_FREE,  0, 11, false, "Paraquedas",        "Paraquedas",             500,   1,   0,    0, "",           371, 0 },
    { 44, CT_EQUIP,   AG_FREE,  0, 11, false, "VisaoNoturna",      "Visao noturna",         2500,   1,   0,    0, "",           368, 0 },
    { 45, CT_EQUIP,   AG_FREE,  0, 11, false, "VisaoTermica",      "Visao termica",         4000,   1,   0,    0, "",           369, 0 },
    { 43, CT_EQUIP,   AG_FREE,  0, 9, false, "Camera",             "Camera",                 150,  36,  36,   15, "Filme",      367, 0 },
    { 41, CT_EQUIP,   AG_FREE,  0, 9, true,  "Spray",              "Spray",                   15, 500,   0,    0, "",           365, 0 },
    { 42, CT_EQUIP,   AG_FREE,  0, 9, true,  "Extintor",           "Extintor",                80, 500,   0,    0, "",           366, 0 },
};
static const int kArmItems = (int)(sizeof g_armItems / sizeof g_armItems[0]);
static int kArmItemsN() { return kArmItems; }

// a fase da AmmuNation: a mesma conta do ammu.scm (0 = loja ainda fechada)
static int Gvar(int n) { return *(volatile int*)(0xA49960 + n * 4); }          // CTheScripts::ScriptSpace
static int AmmuTier() {
    if (Gvar(67) != 1) return 0;                                              // Doberman
    if (Gvar(597) > 2) return 16;
    if (Gvar(593) > 7) return 15;
    if (Gvar(545) > 9) return 14;
    if (Gvar(543) > 0) return 13;
    if (Gvar(545) > 7) return 12;
    if (Gvar(491) > 0) return 11;
    if (Gvar(717) == 1) return 10;
    if (Gvar(455) > 4) return 9;
    if (Gvar(454) > 3) return 8;
    if (Gvar(453) > 2) return 7;
    return 6;
}
static bool LvOpen() { return *(volatile int*)(0xB78E20 + 181 * 4) >= 2; }    // CStats: cidades liberadas
static bool ArmLocked(const ArmItem& it, int tier, bool lv) {
    if (it.group == AG_AMMU) return tier == 0 || it.tier > tier;
    if (it.group == AG_HEAVY) return !lv;
    return false;
}
static int ArmPrice(const ArmItem& it, bool locked) {                         // arma (ou pacote)
    if (locked) return it.price * g_armLockMult;
    if (it.group == AG_AMMU) return (it.price * (100 - g_armDiscount) + 50) / 100;
    return it.price;
}
// a caixa sai a preco normal se alguma arma do mesmo calibre (mesma caixa) ja esta liberada: a escopeta de combate
// fora de epoca usa a caixa calibre 12 da escopeta comum; AK (7.62x39) e M4 (5.56) tem caixas diferentes
static bool ArmBoxLocked(const ArmItem& it, int tier, bool lv) {
    if (!ArmLocked(it, tier, lv)) return false;
    if (it.box <= 0 || !it.boxName[0]) return true;
    for (int i = 0; i < kArmItemsN(); i++) {
        const ArmItem& o = g_armItems[i];
        if (&o != &it && o.box > 0 && !strcmp(o.boxName, it.boxName) && !ArmLocked(o, tier, lv)) return false;
    }
    return true;
}
static int ArmBoxPrice(const ArmItem& it, bool locked) { return locked ? it.boxPrice * g_armLockMult : it.boxPrice; }
static int Money() { return Rd<int>(PlayerInfo(), 0xB8); }                    // CPlayerInfo::m_nMoney
static int WantedLevel() { int o = 0; Cmd c(0x01C0); c.I(0).Out(0); RunCmd(c, &o, 1); return o; }
static void ShopSound(bool ok) {
    char* me = PlayerPed(); if (!me) return;
    V3 p = EntityPos(me);
    Cmd c(0x097A); c.F(p.x).F(p.y).F(p.z).I(ok ? 1052 : 1053); RunCmd(c);    // SOUND_AMMUNATION_BUY_WEAPON(_DENIED)
}
static const ArmItem* ArmByType(int type) {
    for (int i = 0; i < kArmItems; i++) if (g_armItems[i].type == type) return &g_armItems[i];
    return nullptr;
}

// ---- dia do jogo: conta as viradas do relogio (meia-noite, dormir, salvar). Um pulo para tras de ate 6 horas
// (missao que acerta o relogio) nao conta como dia novo.
static DWORD g_armDay = 0, g_armCooldownUntil = 0, g_armBoxDay = 0;
static int   g_armLastMin = -1;
static BYTE  g_armBoxes[48];
static DWORD g_armClockTick = 0;
static void ArmClock() {
    int o[2] = { -1, -1 };
    Cmd c(0x00BF); c.Out(0).Out(1); RunCmd(c, o, 2);
    int cur = o[0] * 60 + o[1];
    if (o[0] < 0 || o[0] > 23 || o[1] < 0 || o[1] > 59) return;
    if (g_armLastMin >= 0 && cur < g_armLastMin && g_armLastMin - cur > 360) g_armDay++;
    g_armLastMin = cur;
}
static DWORD ArmNow() { return g_armDay * 1440 + (DWORD)(g_armLastMin < 0 ? 0 : g_armLastMin); }
static void ArmWhen(DWORD when, char* out, int n) {
    DWORD now = ArmNow();
    int m = (int)(when % 1440);
    if (when / 1440 == now / 1440) snprintf(out, n, "hoje as %02dh%02d", m / 60, m % 60);
    else if (when / 1440 == now / 1440 + 1) snprintf(out, n, "amanha as %02dh%02d", m / 60, m % 60);
    else snprintf(out, n, "daqui a %u dias", (unsigned)(when / 1440 - now / 1440));
}
static int ArmBoxesLeft(const ArmItem& it) {
    if (g_armBoxDay != g_armDay) { g_armBoxDay = g_armDay; memset(g_armBoxes, 0, sizeof g_armBoxes); }
    int used = (it.type >= 0 && it.type < 48) ? g_armBoxes[it.type] : 0;
    return g_armLockBoxes - used;
}
static void ArmCountBox(const ArmItem& it) {
    ArmBoxesLeft(it);
    if (it.type >= 0 && it.type < 48 && g_armBoxes[it.type] < 250) g_armBoxes[it.type]++;
}

// ---- junto do save: GTASAsfN.b.gtmarm
#pragma pack(push, 1)
struct ArmFile { DWORD magic, ver, timeMs, day; int lastMin; DWORD cooldownUntil, boxDay; BYTE boxes[48]; };
#pragma pack(pop)
static const DWORD ARM_MAGIC = 0x4D524147;                                    // 'GARM'
static void ArmSave() {
    if (!g_armOn) return;
    char path[MAX_PATH]; snprintf(path, MAX_PATH, "%s.gtmarm", SaveFileName);
    ArmFile a = {}; a.magic = ARM_MAGIC; a.ver = 1; a.timeMs = *TimeMs; a.day = g_armDay; a.lastMin = g_armLastMin;
    a.cooldownUntil = g_armCooldownUntil; a.boxDay = g_armBoxDay; memcpy(a.boxes, g_armBoxes, sizeof a.boxes);
    FILE* f = fopen(path, "wb");
    if (!f) { Log("traficante: nao gravou %s", path); return; }
    fwrite(&a, sizeof a, 1, f); fclose(f);
}
enum { DL_NONE = 0, DL_COMING, DL_STOPPING, DL_OPEN, DL_LEAVE, DL_FLEE, DL_CHASE };
struct DealerCop { int car, ped; bool spawned; };
struct Dealer {
    int state, van, ped, blip, doorA, doorB;
    DWORD t0, tPhase, tTarget, tLast, tFleeTarget, tStill;
    V3 lastTarget, fleeTarget, threat;
    bool armed, paid, calm, asking;
    DWORD tStillMe; int ynWas;           // CJ parado atras da van desde; setas da ultima leitura (Sim/Nao)
    DealerCop cops[4]; int nCops;
    bool doorsOpen, meetLow;             // portas abertas agora; o ponto de encontro nao esta no nivel do CJ
    DWORD tDoorFix, tFar, tProg, tMeet;  // reforco das portas; CJ longe desde; ultimo avanco; ultimo ponto de encontro
    float bestDist, arriveDist; V3 meet, lastPull;
    bool pulled, straight, los;
    DWORD tLos;
};
static Dealer g_dl = { DL_NONE, -1, -1, -1, -1, -1 };
static void ArmOnLoad() {                                                     // o mundo e outro: o traficante sumiu
    memset(&g_dl, 0, sizeof g_dl); g_dl.van = g_dl.ped = g_dl.blip = -1;
    g_shopOpen = false;
    g_armDay = 0; g_armLastMin = -1; g_armCooldownUntil = 0; g_armBoxDay = 0; memset(g_armBoxes, 0, sizeof g_armBoxes);
    if (!g_armOn) return;
    char saved[MAX_PATH]; snprintf(saved, sizeof saved, "%s", SaveFileName);
    bool found = false;
    for (int s = 0; s < 8 && !found; s++) {
        MakeValidSaveName(s);
        char path[MAX_PATH]; snprintf(path, MAX_PATH, "%s.gtmarm", SaveFileName);
        FILE* f = fopen(path, "rb");
        if (!f) continue;
        ArmFile a = {};
        if (fread(&a, sizeof a, 1, f) == 1 && a.magic == ARM_MAGIC && a.ver == 1 && a.timeMs == g_loadedTimeMs) {
            g_armDay = a.day; g_armCooldownUntil = a.cooldownUntil; g_armBoxDay = a.boxDay;
            memcpy(g_armBoxes, a.boxes, sizeof g_armBoxes);
            found = true;
            Log("traficante: carregado do slot %d (dia %u do jogo%s)", s + 1, (unsigned)g_armDay, g_armCooldownUntil > g_armDay * 1440 ? ", ainda sem atender por um tempo" : "");
        }
        fclose(f);
    }
    strcpy(SaveFileName, saved);
    g_armClockTick = 0;                                                       // o relogio do save vale de novo
}

// ---- menu de compra (mesmo painel da garagem, com duas colunas)
static int  g_shopLevel = 0, g_shopCat = 0, g_shopPick = -1, g_shopPickRow = 0, g_shopItem[12], g_shopN = 0;
static void ShowPanel(const char* title, const char* h0, const char* const* r0, const char* h1, const char* const* r1,
                      int n, float w0, float w1, DWORD disabled, int active) {
    if (g_menu >= 0) { Cmd c(0x08DA); c.I(g_menu); RunCmd(c); g_menu = -1; }
    static char k0[12][8], k1[12][8];
    const char* keys0[12]; const char* keys1[12];
    SetText("GTMGT", title);
    SetText("GTMGH", h0);
    if (r1) SetText("GTMGI", h1 ? h1 : "");
    for (int r = 0; r < 12; r++) {
        snprintf(k0[r], 8, "GTMG%d", r); snprintf(k1[r], 8, "GTMP%d", r);
        keys0[r] = keys1[r] = "DUMMY";
        if (r < n) { SetText(k0[r], r0[r]); keys0[r] = k0[r]; if (r1) { SetText(k1[r], r1[r]); keys1[r] = k1[r]; } }
    }
    int id = -1;
    { Cmd c(0x08D4); c.Key("GTMGT").F(29.f).F(140.f).F(w0).I(r1 ? 2 : 1).I(1).I(1).I(0).Out(0); RunCmd(c, &id, 1); }
    g_menu = id;
    if (id < 0) return;
    { Cmd c(0x08DB); c.I(id).I(0).Key("GTMGH"); for (int r = 0; r < 12; r++) c.Key(keys0[r]); RunCmd(c); }
    if (r1) {
        { Cmd c(0x08DB); c.I(id).I(1).Key("GTMGI"); for (int r = 0; r < 12; r++) c.Key(keys1[r]); RunCmd(c); }
        { Cmd c(0x09DB); c.I(id).I(0).I((int)w0); RunCmd(c); }
        { Cmd c(0x09DB); c.I(id).I(1).I((int)w1); RunCmd(c); }
        { Cmd c(0x08D6); c.I(id).I(0).I(1); RunCmd(c); }                      // nomes a esquerda
        { Cmd c(0x08D6); c.I(id).I(1).I(2); RunCmd(c); }                      // precos a direita
    }
    for (int r = 0; r < n; r++) if (disabled & (1u << r)) { Cmd c(0x08D9); c.I(id).I(r).I(0); RunCmd(c); }
    if (active > 0 && active < n) { Cmd c(0x090E); c.I(id).I(active); RunCmd(c); }
}
static void ShopCategories(int active) {
    g_shopLevel = 0; g_shopN = CT_COUNT + 1;
    const char* rows[CT_COUNT + 1];
    for (int i = 0; i < CT_COUNT; i++) rows[i] = kArmCat[i];
    rows[CT_COUNT] = g_dlLegal ? "Dispensar o armeiro" : "Dispensar o traficante";
    char head[40]; snprintf(head, sizeof head, "Dinheiro: $%d", Money());
    ShowPanel(g_dlLegal ? "Armeiro" : "Traficante", head, rows, nullptr, nullptr, CT_COUNT + 1, 200.f, 0.f, 0, active);
}
static void DealerDismiss();
static void ShopItems(int active) {
    g_shopLevel = 1; g_shopN = 0;
    char* me = PlayerPed(); if (!me) return;
    int tier = AmmuTier(); bool lv = LvOpen();
    static char t0[12][48], t1[12][24];
    const char* r0[12]; const char* r1[12];
    DWORD disabled = 0;
    for (int i = 0; i < kArmItems && g_shopN < 12; i++) {
        const ArmItem& it = g_armItems[i];
        if (it.cat != g_shopCat) continue;
        int r = g_shopN++;
        g_shopItem[r] = i;
        bool locked = ArmLocked(it, tier, lv);
        const char* star = locked ? " *" : "";
        bool own = it.type != ARM_ARMOUR && WeaponType(me, it.slot) == it.type;
        if (it.type == ARM_ARMOUR) {
            float a = Rd<float>(me, 0x548); int maxA = Rd<BYTE>(PlayerInfo(), 0x150);
            snprintf(t0[r], 48, "%s", it.name);
            if (a >= (float)maxA - 0.5f) { snprintf(t1[r], 24, "cheio"); disabled |= 1u << r; }
            else snprintf(t1[r], 24, "$%d%s", ArmPrice(it, locked), star);
        } else if (it.pack) {
            if (locked) snprintf(t0[r], 48, "%s (%d) %d/%d", it.name, it.ammo, ArmBoxesLeft(it) > 0 ? ArmBoxesLeft(it) : 0, g_armLockBoxes);
            else snprintf(t0[r], 48, "%s (%d)", it.name, it.ammo);
            snprintf(t1[r], 24, "$%d%s", ArmPrice(it, locked), star);
        } else if (own && it.box > 0) {
            bool bl = ArmBoxLocked(it, tier, lv);
            if (bl) snprintf(t0[r], 48, "%s (%d) %d/%d", it.boxName, it.box, ArmBoxesLeft(it) > 0 ? ArmBoxesLeft(it) : 0, g_armLockBoxes);
            else snprintf(t0[r], 48, "%s (%d)", it.boxName, it.box);
            snprintf(t1[r], 24, "$%d%s", ArmBoxPrice(it, bl), bl ? " *" : "");
        } else if (own) {
            snprintf(t0[r], 48, "%s", it.name); snprintf(t1[r], 24, "tem"); disabled |= 1u << r;
        } else {
            snprintf(t0[r], 48, "%s", it.name); snprintf(t1[r], 24, "$%d%s", ArmPrice(it, locked), star);
        }
        r0[r] = t0[r]; r1[r] = t1[r];
    }
    char head[40]; snprintf(head, sizeof head, "%s ($%d)", kArmCat[g_shopCat], Money());
    ShowPanel(g_dlLegal ? "Armeiro" : "Traficante", head, r0, "Preco", r1, g_shopN, 215.f, 80.f, disabled, active);
}
static void ShopConfirm(int idx, int cur, int price) {
    g_shopLevel = 2; g_shopPick = idx; g_shopN = 2;
    const ArmItem& it = g_armItems[idx];
    const ArmItem* old = ArmByType(cur);
    static char title[64], yes[48], note[64];
    snprintf(title, sizeof title, "Trocar %s?", old ? old->name : "a arma atual");
    snprintf(yes, sizeof yes, "Sim, %s por $%d", it.name, price);
    if (it.slot >= 3 && it.slot <= 5) snprintf(note, sizeof note, "A municao passa para a nova");
    else if (it.slot <= 1 || it.slot == 11) snprintf(note, sizeof note, "Voce fica so com a nova");
    else snprintf(note, sizeof note, "A municao da atual se perde");
    const char* rows[2] = { yes, "Nao" };
    ShowPanel(title, note, rows, nullptr, nullptr, 2, 220.f, 0.f, 0, 0);
}
static void CloseShop(bool rearm) {
    if (!g_shopOpen) return;
    if (g_menu >= 0) { Cmd c(0x08DA); c.I(g_menu); RunCmd(c); }
    g_menu = -1; g_shopOpen = false; g_shopLevel = 0;
    PlayerControl(true);
    g_dl.armed = rearm;
    g_dl.tLast = GameMs();
}
static void OpenShop() {
    g_shopOpen = true; g_menuOpenAt = GetTickCount(); g_garSel = g_garCancel = false;
    g_dl.armed = false; g_dl.tLast = GameMs();
    PlayerControl(false);
    int tier = AmmuTier();
    Log("traficante: menu aberto (dinheiro $%d, AmmuNation %s, nivel %d de 16, Las Venturas %s)", Money(), tier ? "aberta" : "fechada", tier, LvOpen() ? "liberada" : "fechada");
    GarHelp("* fora de epoca: %dx o preco e %d caixas por arma por dia (o numero mostra quantas restam hoje).", g_armLockMult, g_armLockBoxes);
    ShopCategories(0);
}
static bool ArmGive(const ArmItem& it, int ammo) {
    int pr = PlayerRef();
    if (pr < 0) return false;
    if (it.model > 0) {
        RequestModel(it.model, 2);
        if (it.model2 > 0) RequestModel(it.model2, 2);
        LoadAllRequestedModels(false);
        if (!ModelLoaded(it.model) || (it.model2 > 0 && !ModelLoaded(it.model2))) return false;
    }
    { Cmd c(0x01B2); c.I(pr).I(it.type).I(ammo); RunCmd(c); }
    if (it.model > 0) { Cmd c(0x0249); c.I(it.model); RunCmd(c); }
    if (it.model2 > 0) { Cmd c(0x0249); c.I(it.model2); RunCmd(c); }
    return true;
}
static void ShopBuy(int idx, bool confirmed, int row) {
    const ArmItem& it = g_armItems[idx];
    char* me = PlayerPed(); int pr = PlayerRef();
    if (!me || pr < 0) return;
    bool locked = ArmLocked(it, AmmuTier(), LvOpen());
    int money = Money();
    if (it.type == ARM_ARMOUR) {
        float a = Rd<float>(me, 0x548); int maxA = Rd<BYTE>(PlayerInfo(), 0x150);
        int price = ArmPrice(it, locked);
        if (a >= (float)maxA - 0.5f) { ShopSound(false); GarHelp("O colete ja esta cheio."); return; }
        if (money < price) { ShopSound(false); GarHelp("Dinheiro insuficiente: o colete custa $%d.", price); return; }
        { Cmd c(0x035F); c.I(pr).I(maxA > 0 ? maxA : 100); RunCmd(c); }
        AddMoney(-price); ShopSound(true);
        Log("traficante: colete por $%d%s", price, locked ? " (fora de epoca)" : "");
        GarHelp("Colete comprado.");
        ShopItems(row);
        return;
    }
    int cur = WeaponType(me, it.slot);
    bool own = cur == it.type;
    bool isBox = it.pack || (own && it.box > 0);
    if (own && !it.pack && it.box <= 0) { ShopSound(false); GarHelp("Voce ja tem %s.", it.name); return; }
    if (isBox && !it.pack) locked = ArmBoxLocked(it, AmmuTier(), LvOpen());   // caixa: vale o calibre
    int price = isBox && !it.pack ? ArmBoxPrice(it, locked) : ArmPrice(it, locked);
    if (isBox && locked && ArmBoxesLeft(it) <= 0) {
        ShopSound(false);
        GarHelp("Hoje nao tem mais %s. Ele consegue mais amanha.", it.pack ? it.name : "municao dessa arma");
        return;
    }
    if (!own && cur > 0 && !confirmed) { g_shopPickRow = row; ShopConfirm(idx, cur, price); return; }
    if (money < price) { ShopSound(false); GarHelp("Dinheiro insuficiente: custa $%d.", price); return; }
    bool ok;
    int qty = it.pack ? it.ammo : (own ? it.box : it.ammo);
    if (own) { Cmd c(0x0114); c.I(pr).I(it.type).I(qty); RunCmd(c); ok = true; }
    else ok = ArmGive(it, qty);
    if (!ok) { ShopSound(false); GarHelp("Nao deu para pegar %s agora. Tente de novo.", it.name); return; }
    AddMoney(-price);
    if (isBox && locked) ArmCountBox(it);
    ShopSound(true);
    if (own && !it.pack) GarHelp("%s (%d) para %s.", it.boxName, it.box, it.name);
    else if (it.pack) GarHelp("%s: mais %d.", it.name, it.ammo);
    else GarHelp("%s comprada.", it.name);
    Log("traficante: %s %s por $%d%s", it.name, own && !it.pack ? "caixa" : it.pack ? "pacote" : "arma", price, locked ? " (fora de epoca)" : "");
    ShopItems(row);
}
static void ShopTick() {
    if (!g_shopOpen) return;
    char* me = PlayerPed();
    if (!me || MenuActive() || PlayerVehicle() || Rd<float>(me, 0x540) <= 0.f) { CloseShop(false); return; }
    if (g_garCancel) {
        g_garCancel = false;
        if (g_shopLevel == 2) { g_shopPick = -1; ShopItems(g_shopPickRow); }
        else if (g_shopLevel == 1) ShopCategories(g_shopCat);
        else { CloseShop(false); Log("traficante: menu fechado"); }
        return;
    }
    if (g_garSel) {
        g_garSel = false;
        int row = 0;
        { Cmd c(0x08D7); c.I(g_menu).Out(0); RunCmd(c, &row, 1); }
        g_dl.tLast = GameMs();
        if (g_shopLevel == 0) {
            if (row >= 0 && row < CT_COUNT) { g_shopCat = row; ShopItems(0); }
            else if (row == CT_COUNT) DealerDismiss();
            return;
        }
        if (g_shopLevel == 2) {
            int pick = g_shopPick;
            g_shopPick = -1;
            if (row == 0 && pick >= 0) {
                ShopBuy(pick, true, g_shopPickRow);
                if (g_shopLevel == 2) ShopItems(g_shopPickRow);                // nao comprou (dinheiro, limite...)
            } else ShopItems(g_shopPickRow);
            return;
        }
        if (row >= 0 && row < g_shopN) ShopBuy(g_shopItem[row], false, row);
    }
}

// ---- van e motorista
// CAutomobile::OpenDoor (vtable +0x6C): (ped, no, porta, abertura 0..1, som). Mesma chamada do OPEN_CAR_DOOR (0x494
// 6DA) e do CloseAllDoors (0x6A4520), que so fecha quantas portas o modelo declara (+0x4C; a Burrito diz 2).
typedef void (__thiscall* OpenDoorV_t)(void* veh, void* ped, int node, int door, float ratio, bool sound);
typedef bool (__thiscall* DoorMissingV_t)(void* veh, int door);
static void SetVanDoor(char* v, int door, float ratio, bool sound = true) {
    static const int kNode[6] = { 0x10, 0x11, 0x0A, 0x08, 0x0B, 0x09 };       // capo, porta-malas, frente E/D, tras E/D
    if (!v || door < 0 || door > 5) return;
    void** vt = *(void***)v;
    if (((DoorMissingV_t)vt[0x98 / 4])(v, door)) return;
    if (!((void* const*)(v + 0x648))[kNode[door]]) return;
    ((OpenDoorV_t)vt[0x6C / 4])(v, nullptr, kNode[door], door, ratio, sound);
}
static void DealerDoors(bool open) {
    char* v = VehByRef(g_dl.van);
    if (!v) return;
    g_dl.doorsOpen = open;
    if (open) {
        if (g_dl.doorA >= 0) { Cmd c(0x0657); c.I(g_dl.van).I(g_dl.doorA); RunCmd(c); }
        if (g_dl.doorB >= 0) { Cmd c(0x0657); c.I(g_dl.van).I(g_dl.doorB); RunCmd(c); }
        g_dl.tDoorFix = GameMs();
        return;
    }
    SetVanDoor(v, g_dl.doorA, 0.f);
    SetVanDoor(v, g_dl.doorB, 0.f);
    { Cmd c(0x0508); c.I(g_dl.van); RunCmd(c); }
}
static V3 VanRear(const char* v) {                                            // atras da traseira, no chao da van
    V3 p = EntityPos(v);
    const float* m = Rd<const float*>(v, 0x14);
    float fx = m ? m[4] : 0.f, fy = m ? m[5] : 1.f, fz = m ? m[6] : 0.f;
    float back = -2.8f;
    char* mi = ModelInfo(Rd<short>(v, 0x22));
    const float* c = mi ? Rd<const float*>(mi, 0x14) : nullptr;
    if (c && c[1] < -0.5f && c[1] > -8.f) back = c[1];
    back -= 0.9f;
    return { p.x + fx * back, p.y + fy * back, p.z + fz * back };
}
static bool IsCopCarModel(int m) { return m == 596 || m == 597 || m == 598 || m == 599 || m == 523 || m == 427 || m == 490 || m == 528 || m == 601 || m == 497; }
static bool IsCop(const char* p) { return p && Rd<int>(p, 0x598) == 6 && Rd<float>(p, 0x540) > 0.f && !RcIsRecruit(p); }
static bool IsOurCop(const char* p) {
    for (int k = 0; k < g_dl.nCops; k++) if (PedByRef(g_dl.cops[k].ped) == p) return true;
    return false;
}
// algum policial perto (range) da traseira; com sight, so conta quem tem linha de visao ate ela
static bool CopNear(V3 at, float range, bool sight, V3* where) {
    char* pool = *(char**)0xB74490;
    if (!pool) return false;
    int size = Rd<int>(pool, 8);
    for (int i = 0; i < size; i++) {
        if (Rd<BYTE*>(pool, 4)[i] & 0x80) continue;
        char* p = Rd<char*>(pool, 0) + i * 0x7C4;
        if (!IsCop(p) || IsOurCop(p)) continue;
        V3 cp = EntityPos(p);
        char* cv = (Rd<BYTE>(p, 0x46D) & 1) ? Rd<char*>(p, 0x58C) : nullptr;
        float r = range;
        if (sight && cv && Rd<int>(cv, 0x590) == 3) r = range * 2.f;          // helicoptero
        float dx = cp.x - at.x, dy = cp.y - at.y, dz = cp.z - at.z;
        if (dx * dx + dy * dy + dz * dz > r * r) continue;
        if (sight) {
            Cmd c(0x06BD); c.F(cp.x).F(cp.y).F(cp.z + 0.6f).F(at.x).F(at.y).F(at.z + 0.8f).I(1).I(0).I(0).I(1).I(0);
            if (!RunCmd(c)) continue;
        }
        if (where) *where = cp;
        return true;
    }
    return false;
}
// policial que ve a negociacao: perto da traseira (CopSeeDistance; helicoptero 1,6x), dentro do cone de tras da van
// (CopSeeAngle para cada lado do eixo de tras) e com linha de visao ate a porta
static bool CopSeesRear(const char* v, V3 rear, V3* where) {
    char* pool = *(char**)0xB74490;
    const float* m = Rd<const float*>(v, 0x14);
    if (!pool || !m) return false;
    float bx = -m[4], by = -m[5], bl = sqrtf(bx * bx + by * by);
    if (bl < 0.01f) return false;
    bx /= bl; by /= bl;
    float cosMax = cosf((float)g_armCopAngle * 3.14159265f / 180.f);
    int size = Rd<int>(pool, 8);
    for (int i = 0; i < size; i++) {
        if (Rd<BYTE*>(pool, 4)[i] & 0x80) continue;
        char* p = Rd<char*>(pool, 0) + i * 0x7C4;
        if (!IsCop(p) || IsOurCop(p)) continue;
        V3 cp = EntityPos(p);
        char* cv = (Rd<BYTE>(p, 0x46D) & 1) ? Rd<char*>(p, 0x58C) : nullptr;
        bool heli = cv && Rd<int>(cv, 0x590) == 3;
        float r = heli ? g_armCopSee * 1.6f : (float)g_armCopSee;
        float dx = cp.x - rear.x, dy = cp.y - rear.y, dz = cp.z - rear.z;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d > r) continue;
        float h = sqrtf(dx * dx + dy * dy);
        float cs = h < 2.f ? 1.f : (dx * bx + dy * by) / h;
        if (cs < cosMax) continue;                                             // de lado ou pela frente: nao ve
        Cmd c(0x06BD); c.F(cp.x).F(cp.y).F(cp.z + 0.6f).F(rear.x).F(rear.y).F(rear.z + 0.8f).I(1).I(0).I(0).I(1).I(0);
        if (!RunCmd(c)) continue;
        Log("traficante: policial%s a %.0f m da traseira, %.0f graus do eixo de tras", heli ? " de helicoptero" : cv ? " de viatura" : "",
            d, acosf(cs > 1.f ? 1.f : cs) * 180.f / 3.14159265f);
        if (where) *where = cp;
        return true;
    }
    return false;
}
// ponto de encontro: a rua mais perto do CJ no mesmo nivel (ate 3 m de altura); sem nenhuma, a menos ruim
static bool MeetNode(V3 pp, V3* out) {
    bool found = false; float best = 1e9f, anyBest = 1e9f; V3 any = pp;
    for (int n = 1; n <= 30; n++) {
        int o[4] = {};
        Cmd c(0x06F8); c.F(pp.x).F(pp.y).F(pp.z).I(n).Out(0).Out(1).Out(2).Out(3); RunCmd(c, o, 4);
        V3 q = { AsF(o[0]), AsF(o[1]), AsF(o[2]) };
        if (q.x == 0.f && q.y == 0.f) continue;
        float d = Dist2(q, pp), dz = fabsf(q.z - pp.z);
        if (d > 300.f) continue;
        if (dz <= 3.f && d < best) { best = d; *out = q; found = true; }
        float sc = d + dz * 10.f;
        if (sc < anyBest) { anyBest = sc; any = q; }
    }
    if (!found) *out = any;
    return found;
}
// delegacias (e os patios delas): a van nao e puxada para perto delas
static bool NearPoliceStation(V3 q) {
    static const float st[][2] = { { 1555.f, -1675.f }, { -1605.f, 715.f }, { 2290.f, 2430.f }, { 630.f, -570.f },
                                   { -1390.f, 2640.f }, { -215.f, 980.f }, { -2160.f, -2385.f } };
    for (auto& c : st) { float dx = q.x - c[0], dy = q.y - c[1]; if (dx * dx + dy * dy < 80.f * 80.f) return true; }
    return false;
}
// para onde puxar a van perdida: rua no nivel do CJ, de 25 a 150 m, de preferencia fora da tela e longe de delegacia
static bool PullNode(V3 pp, V3* out, float* hd) {
    bool got = false, gotOn = false; V3 on = {}; float onHd = 0.f;
    for (int n = 1; n <= 60 && !got; n++) {
        int o[4] = {};
        Cmd c(0x06F8); c.F(pp.x).F(pp.y).F(pp.z).I(n).Out(0).Out(1).Out(2).Out(3); RunCmd(c, o, 4);
        V3 q = { AsF(o[0]), AsF(o[1]), AsF(o[2]) };
        if (q.x == 0.f && q.y == 0.f) continue;
        float d = Dist2(q, pp), dz = fabsf(q.z - pp.z);
        if (dz > 3.f || d < 25.f || d > 150.f) continue;
        if (NearPoliceStation(q) && !NearPoliceStation(pp)) continue;
        bool seen = false;
        { Cmd s(0x00C2); s.F(q.x).F(q.y).F(q.z + 1.f).F(4.f); seen = RunCmd(s); }
        if (!seen) { *out = q; *hd = AsF(o[3]); got = true; }
        else if (!gotOn) { on = q; onHd = AsF(o[3]); gotOn = true; }
    }
    if (!got && gotOn) { *out = on; *hd = onHd; got = true; }
    return got;
}
static void DealerRemoveBlip() { if (g_dl.blip >= 0) { Cmd c(0x0164); c.I(g_dl.blip); RunCmd(c); g_dl.blip = -1; } }
static void ReleaseCops() {
    for (int k = 0; k < g_dl.nCops; k++) {
        DealerCop& c = g_dl.cops[k];
        char* car = VehByRef(c.car); char* ped = PedByRef(c.ped);
        if (car) { Cmd x(0x0397); x.I(c.car).I(0); RunCmd(x); }
        if (car && ped && (Rd<BYTE>(ped, 0x46D) & 1) && Rd<char*>(ped, 0x58C) == car) { Cmd x(0x05D2); x.I(c.ped).I(c.car).F(12.f).I(0); RunCmd(x); }
        if (c.spawned) {
            if (ped) { Cmd x(0x01C2); x.I(c.ped); RunCmd(x); }
            if (car) { Cmd x(0x01C3); x.I(c.car); RunCmd(x); }
        }
    }
    g_dl.nCops = 0;
}
static void DealerAskEnd() {
    if (!g_dl.asking) return;
    g_dl.asking = false; g_dealerYN = false;
    Cmd c(0x03E6); RunCmd(c);                                                  // CLEAR_HELP
}
static void DealerEnd(const char* how) {
    DealerAskEnd();
    CloseShop(false);
    DealerRemoveBlip();
    ReleaseCops();
    char* v = VehByRef(g_dl.van); char* d = PedByRef(g_dl.ped);
    bool onScreen = false;
    if (v) { Cmd c(0x02CA); c.I(g_dl.van); onScreen = RunCmd(c); }
    if (d) { Cmd c(0x09B6); c.I(g_dl.ped).I(0); RunCmd(c); }
    if (!onScreen) {                                                          // fora de vista: some de vez
        if (d) { Cmd c(0x009B); c.I(g_dl.ped); RunCmd(c); }
        if (v) { Cmd c(0x00A6); c.I(g_dl.van); RunCmd(c); }
    } else {                                                                  // na tela: o jogo tira quando sair
        if (d) { Cmd c(0x01C2); c.I(g_dl.ped); RunCmd(c); }
        if (v) { Cmd c(0x01C3); c.I(g_dl.van); RunCmd(c); }
    }
    Log("traficante: fim (%s)", how);
    memset(&g_dl, 0, sizeof g_dl); g_dl.van = g_dl.ped = g_dl.blip = -1;
}
static DWORD g_armLegalCool = 0;                                             // armeiro: sem atender (nao vai para o save)
static void DealerCooldown(const char* why) {
    DWORD& until = g_dlLegal ? g_armLegalCool : g_armCooldownUntil;
    until = ArmNow() + (DWORD)(g_armCooldown > 0 ? g_armCooldown : 0);
    char t[40]; ArmWhen(until, t, sizeof t);
    Log("traficante: sem atender ate %s (%s)", t, why);
}
static V3 FleePoint(V3 from, V3 threat) {
    float dx = from.x - threat.x, dy = from.y - threat.y, l = sqrtf(dx * dx + dy * dy);
    if (l < 1.f) { dx = 0.f; dy = 1.f; l = 1.f; }
    float tx = from.x + dx / l * 400.f, ty = from.y + dy / l * 400.f;
    int o[3] = {};
    Cmd c(0x02C1); c.F(tx).F(ty).F(from.z).Out(0).Out(1).Out(2); RunCmd(c, o, 3);
    V3 n = { AsF(o[0]), AsF(o[1]), AsF(o[2]) };
    if (Dist2(n, from) < 60.f) n = { tx, ty, from.z };
    return n;
}
static void DealerDrive(V3 to, float speed) {
    { Cmd c(0x00AD); c.I(g_dl.van).F(speed); RunCmd(c); }
    { Cmd c(0x00AE); c.I(g_dl.van).I(2); RunCmd(c); }                          // desvia dos carros, fura sinal
    { Cmd c(0x00A7); c.I(g_dl.van).F(to.x).F(to.y).F(to.z); RunCmd(c); }
}
static void DealerGo(int state, V3 threat) {                                  // vai embora (LEAVE) ou foge (FLEE/CHASE)
    if (g_dl.straight && VehByRef(g_dl.van)) { Cmd c(0x04E0); c.I(g_dl.van).I(20); RunCmd(c); g_dl.straight = false; }
    DealerAskEnd();
    CloseShop(false);
    DealerRemoveBlip();
    DealerDoors(false);
    char* v = VehByRef(g_dl.van);
    g_dl.state = state; g_dl.tPhase = g_dl.tFleeTarget = GameMs(); g_dl.threat = threat; g_dl.tStill = 0;
    if (!v) return;
    if (state == DL_LEAVE) {
        { Cmd c(0x00AD); c.I(g_dl.van).F(g_dl.calm ? 14.f : 32.f); RunCmd(c); }
        { Cmd c(0x00AE); c.I(g_dl.van).I(g_dl.calm ? 0 : 2); RunCmd(c); }   // calmo: para nos carros e no sinal
        { Cmd c(0x00A8); c.I(g_dl.van); RunCmd(c); }
    } else {
        g_dl.fleeTarget = FleePoint(EntityPos(v), threat);
        DealerDrive(g_dl.fleeTarget, 45.f);
    }
}
static void DealerDismiss() {
    char* me = PlayerPed();
    V3 pp = me ? EntityPos(me) : V3{ 0.f, 0.f, 0.f };
    g_dl.calm = true;
    GarHelp("Valeu. O traficante foi embora.");
    Log("traficante: dispensado; foi embora com calma");
    DealerGo(DL_LEAVE, pp);
}
static void SpawnChaseCop(const char* van) {
    V3 vp = EntityPos(van);
    int level = 1;
    { Cmd c(0x07EF); c.F(vp.x).F(vp.y).F(vp.z).Out(0); RunCmd(c, &level, 1); }
    static const int kCar[4] = { 599, 596, 597, 598 }, kPed[4] = { 283, 280, 281, 282 };
    if (level < 0 || level > 3) level = 1;
    int carM = kCar[level], pedM = kPed[level];
    const float* m = Rd<const float*>(van, 0x14);
    float fx = m ? m[4] : 0.f, fy = m ? m[5] : 1.f;
    char* me = PlayerPed(); V3 pp = me ? EntityPos(me) : vp;
    static const float dist[] = { 90.f, 120.f, 70.f, 150.f };
    static const float ang[] = { 180.f, 150.f, 210.f, 120.f, 240.f };
    for (float d : dist) for (float a : ang) {
        float r = a * 3.14159265f / 180.f, cs = cosf(r), sn = sinf(r);
        float x = vp.x + (fx * cs - fy * sn) * d, y = vp.y + (fx * sn + fy * cs) * d;
        int o[4] = {};
        { Cmd c(0x03D3); c.F(x).F(y).F(vp.z).Out(0).Out(1).Out(2).Out(3); RunCmd(c, o, 4); }
        V3 n = { AsF(o[0]), AsF(o[1]), AsF(o[2]) };
        if (Dist2(n, vp) < 50.f || Dist2(n, pp) < 50.f) continue;
        { Cmd c(0x00C2); c.F(n.x).F(n.y).F(n.z).F(4.f); if (RunCmd(c)) continue; }   // nao nasce na frente da camera
        RequestModel(carM, 2); RequestModel(pedM, 2); LoadAllRequestedModels(false);
        if (!ModelLoaded(carM) || !ModelLoaded(pedM)) return;
        int car = -1, ped = -1;
        { Cmd c(0x00A5); c.I(carM).F(n.x).F(n.y).F(n.z).Out(0); RunCmd(c, &car, 1); }
        if (!VehByRef(car)) return;
        { Cmd c(0x0175); c.I(car).F(AsF(o[3])); RunCmd(c); }
        { Cmd c(0x0129); c.I(car).I(6).I(pedM).Out(0); RunCmd(c, &ped, 1); }
        { Cmd c(0x0249); c.I(carM); RunCmd(c); }
        { Cmd c(0x0249); c.I(pedM); RunCmd(c); }
        if (!PedByRef(ped)) { Cmd c(0x00A6); c.I(car); RunCmd(c); return; }
        { Cmd c(0x0397); c.I(car).I(1); RunCmd(c); }
        { Cmd c(0x06E1); c.I(ped).I(car).I(g_dl.van).I(15).F(40.f).I(2); RunCmd(c); }   // MISSION_RAMCAR_FARAWAY
        g_dl.cops[g_dl.nCops++] = { car, ped, true };
        Log("traficante: viatura %d veio de (%.0f, %.0f) atras da van", carM, n.x, n.y);
        return;
    }
    Log("traficante: nao achei rua fora de vista para uma viatura");
}
static void StartChase(V3 copAt) {
    char* v = VehByRef(g_dl.van);
    DealerGo(DL_CHASE, copAt);
    if (!v) return;
    if (PedByRef(g_dl.ped)) { Cmd c(0x09B6); c.I(g_dl.ped).I(1); RunCmd(c); }   // a policia vai atras dele
    V3 vp = EntityPos(v);
    char* pool = *(char**)0xB74494;
    if (pool) {
        int size = Rd<int>(pool, 8);
        for (int i = 0; i < size && g_dl.nCops < 3; i++) {
            if (Rd<BYTE*>(pool, 4)[i] & 0x80) continue;
            char* car = Rd<char*>(pool, 0) + i * 0xA18;
            if (car == v || !IsCopCarModel(Rd<short>(car, 0x22)) || (Rd<BYTE>(car, 0x36) >> 3) == 5) continue;
            char* drv = Rd<char*>(car, 0x460);
            if (!IsCop(drv) || Dist2(EntityPos(car), vp) > 150.f) continue;
            int ch = VehRef(car), ph = PedRef(drv);
            if (ch < 0 || ph < 0) continue;
            { Cmd c(0x0397); c.I(ch).I(1); RunCmd(c); }
            { Cmd c(0x06E1); c.I(ph).I(ch).I(g_dl.van).I(15).F(40.f).I(2); RunCmd(c); }
            g_dl.cops[g_dl.nCops++] = { ch, ph, false };
        }
    }
    int nNear = g_dl.nCops;
    if (!g_dl.nCops) SpawnChaseCop(v);
    Log("traficante: a policia viu a van aberta; perseguicao (%d viaturas perto, %d chamadas)", nNear, g_dl.nCops - nNear);
}

// ---- ligacao
static void DealerRootLabel(char* out, int n) {
    bool legal = g_dl.state != DL_NONE ? g_dlLegal : ArmLegalNow();
    const char* who = legal ? "Armeiro da policia" : "Traficante de armas";
    if (g_dl.state == DL_COMING || g_dl.state == DL_STOPPING) snprintf(out, n, "%s (a caminho)", who);
    else if (g_dl.state == DL_OPEN) snprintf(out, n, "%s (venha ate aqui)", who);
    else if (ArmNow() < (legal ? g_armLegalCool : g_armCooldownUntil)) snprintf(out, n, "%s (nao atende)", who);
    else if (legal) snprintf(out, n, "%s (gratis)", who);
    else snprintf(out, n, "%s ($%d)", who, g_armCall);
}
static void DealerCall() {
    char* me = PlayerPed();
    if (!me) return;
    if (g_dl.state == DL_NONE) g_dlLegal = ArmLegalNow();                    // armeiro: nivel 12 no Vigilante, nivel 64 sempre
    if (g_dl.state == DL_COMING || g_dl.state == DL_STOPPING) { GarHelp("O traficante ja esta a caminho."); return; }
    if (g_dl.state == DL_OPEN) {                                              // "venha ate aqui": leva a van ate onde voce esta
        if (g_shopOpen || g_dl.asking) { GarHelp("O traficante ja esta te atendendo."); return; }
        char* v = VehByRef(g_dl.van);
        if (!v) return;
        if (g_dl.doorsOpen) DealerDoors(false);
        V3 p = EntityPos(me);
        g_dl.meetLow = !MeetNode(p, &g_dl.meet);
        g_dl.straight = false; g_dl.los = false; g_dl.tLos = 0;
        { Cmd c(0x04E0); c.I(g_dl.van).I(20); RunCmd(c); }
        DealerDrive(g_dl.meet, 14.f);
        g_dl.lastTarget = p; g_dl.bestDist = 1e9f; g_dl.arriveDist = 0.f; g_dl.pulled = false; g_dl.tFar = 0;
        g_dl.state = DL_COMING; g_dl.t0 = g_dl.tPhase = g_dl.tProg = g_dl.tMeet = g_dl.tLast = GameMs();
        Log("traficante: chamado ate (%.0f, %.0f, %.0f)", p.x, p.y, p.z);
        GarHelp("O traficante esta vindo ate voce.");
        return;
    }
    if (g_dl.state != DL_NONE) { GarHelp("O traficante esta ocupado. Ligue daqui a pouco."); return; }
    if (ArmNow() < (g_dlLegal ? g_armLegalCool : g_armCooldownUntil)) {
        char t[40]; ArmWhen(g_dlLegal ? g_armLegalCool : g_armCooldownUntil, t, sizeof t);
        GarHelp("O traficante nao atende. Ele volta a atender %s.", t);
        Log("traficante: nao atendeu (volta %s)", t);
        return;
    }
    if (*(volatile int*)0xB72914 != 0) { GarHelp("Ligue ao ar livre."); return; }
    int price = g_dlLegal ? 0 : g_armCall;
    if (Money() < price) { GarHelp("Ele so vem por $%d.", price); return; }
    V3 p = EntityPos(me), node; float hd = 0.f;
    if (!FindSpawnNode(p, Rd<const float*>(me, 0x14), &node, &hd)) { GarHelp("Nao achei uma rua para a van vir. Tente de outro lugar."); return; }
    static const int kPed[3] = { 28, 29, 30 };
    int pm = g_dlLegal ? 285 : kPed[GetTickCount() % 3], vm = g_dlLegal ? 427 : 482;   // armeiro: Enforcer com um SWAT
    RequestModel(vm, 2); RequestModel(pm, 2); LoadAllRequestedModels(false);
    if (!ModelLoaded(vm) || !ModelLoaded(pm)) { GarHelp("A van nao carregou. Tente de novo."); return; }
    int van = -1, ped = -1;
    { Cmd c(0x00A5); c.I(vm).F(node.x).F(node.y).F(node.z).Out(0); RunCmd(c, &van, 1); }
    char* v = VehByRef(van);
    if (!v) { GarHelp("A van nao pode ser criada agora. Tente de novo."); return; }
    { Cmd c(0x0175); c.I(van).F(hd); RunCmd(c); }
    if (!g_dlLegal) { Cmd c(0x0229); c.I(van).I(0).I(0); RunCmd(c); }        // preta
    { Cmd c(0x020A); c.I(van).I(2); RunCmd(c); }                             // trancada
    { Cmd c(0x0129); c.I(van).I(4).I(pm).Out(0); RunCmd(c, &ped, 1); }
    { Cmd c(0x0249); c.I(vm); RunCmd(c); }
    { Cmd c(0x0249); c.I(pm); RunCmd(c); }
    if (!PedByRef(ped)) { Cmd c(0x00A6); c.I(van); RunCmd(c); GarHelp("O traficante nao pode vir agora. Tente de novo."); return; }
    { Cmd c(0x039E); c.I(ped).I(1); RunCmd(c); }                             // ninguem tira ele da van
    { Cmd c(0x0526); c.I(ped).I(1); RunCmd(c); }
    memset(&g_dl, 0, sizeof g_dl);
    g_dl.van = van; g_dl.ped = ped; g_dl.blip = -1;
    { int b = -1; Cmd c(0x0186); c.I(van).Out(0); RunCmd(c, &b, 1); g_dl.blip = b; }
    // portas de tras: door_lr / door_rr (CAutomobile::m_aCarNodes +0x648, nos 11 e 9); sem elas, o porta-malas
    void* const* nodes = (void* const*)(v + 0x648);
    if (nodes[11] && nodes[9]) { g_dl.doorA = 4; g_dl.doorB = 5; }
    else if (nodes[17]) { g_dl.doorA = 1; g_dl.doorB = -1; }
    else { g_dl.doorA = g_dl.doorB = -1; }
    g_dl.meetLow = !MeetNode(p, &g_dl.meet);
    DealerDrive(g_dl.meet, 22.f);
    g_dl.lastTarget = p; g_dl.bestDist = 1e9f;
    if (price > 0) { AddMoney(-price); g_dl.paid = true; }
    g_dl.state = DL_COMING; g_dl.t0 = g_dl.tPhase = g_dl.tTarget = g_dl.tLast = g_dl.tProg = g_dl.tMeet = GameMs();
    Log("traficante: %sa caminho de (%.0f, %.0f), %.0f m, motorista %d, portas %s ($%d)", g_dlLegal ? "armeiro da policia " : "", node.x, node.y, Dist2(node, p), pm,
        g_dl.doorA == 4 ? "traseiras" : g_dl.doorA == 1 ? "do porta-malas" : "nenhuma", price);
    if (g_dlLegal) GarHelp("O armeiro da policia esta a caminho. Fique por perto.");
    else GarHelp("O traficante esta a caminho ($%d). Fique por perto.", g_armCall);
}

// ---- a cada quadro
static void DealerTick() {
    if (!g_armOn) return;
    DWORD now = GameMs();
    char* me = PlayerPed();
    if (me && !MenuActive() && now - g_armClockTick >= 1000) { g_armClockTick = now; ArmClock(); }
    ShopTick();
    if (g_dl.state == DL_NONE || !me) return;
    static DWORD logic = 0;
    if (now - logic < 200 && g_dl.state != DL_OPEN) return;
    logic = now;
    char* v = VehByRef(g_dl.van); char* d = PedByRef(g_dl.ped);
    bool vanOk = v && (Rd<BYTE>(v, 0x36) >> 3) != 5;
    bool drvOk = vanOk && d && Rd<float>(d, 0x540) > 0.f && (Rd<BYTE>(d, 0x46D) & 1) && Rd<char*>(d, 0x58C) == v;
    int pr = PlayerRef();
    bool attacked = false;
    if (pr >= 0 && g_dl.state <= DL_OPEN) {
        if (d) { Cmd c(0x051A); c.I(g_dl.ped).I(pr); attacked = RunCmd(c); }
        if (!attacked && v) { Cmd c(0x051C); c.I(g_dl.van).I(pr); attacked = RunCmd(c); }
    }
    V3 pp = EntityPos(me);
    if (g_dl.state <= DL_OPEN && !drvOk) {
        bool arrived = g_dl.state == DL_OPEN;
        if (attacked || arrived) DealerCooldown(attacked ? "atacado" : "a van ou o traficante nao resistiu");
        else if (g_dl.paid) { AddMoney(g_armCall); GarHelp("O traficante nao conseguiu chegar. Os $%d voltaram.", g_armCall); }
        if (attacked) GarHelp("Voce atacou o traficante. Ele nao vai te atender por um dia.");
        DealerEnd(attacked ? "atacado" : arrived ? "van ou traficante fora de combate" : "nao chegou");
        return;
    }
    if (attacked) {
        DealerCooldown("atacado");
        GarHelp("Voce atacou o traficante. Ele fugiu e nao vai te atender por um dia.");
        DealerGo(DL_FLEE, pp);
        Log("traficante: atacado pelo CJ; fugiu");
        return;
    }
    float dist = vanOk ? Dist2(EntityPos(v), pp) : 9999.f;
    bool dead = Rd<float>(me, 0x540) <= 0.f;
    if (g_dl.state <= DL_OPEN && *(volatile int*)0xB72914 != 0) {              // o CJ entrou num interior
        Log("traficante: o CJ entrou num interior; foi embora");
        DealerGo(DL_LEAVE, pp);
        return;
    }
    switch (g_dl.state) {
    case DL_COMING: {
        if (dead) { DealerGo(DL_LEAVE, pp); return; }
        V3 vp = EntityPos(v);
        bool level = fabsf(vp.z - pp.z) < 3.5f;
        // perto, no mesmo nivel e com caminho livre a vista (sem parede, muro ou cerca entre a van e o CJ): vem em
        // linha reta (canal, beco, estacionamento sem rua no mapa do jogo); senao, segue as ruas
        if (now - g_dl.tLos >= 1000) {
            g_dl.tLos = now; g_dl.los = false;
            if (level && dist < 70.f) {
                Cmd c(0x06BD); c.F(vp.x).F(vp.y).F(vp.z + 0.8f).F(pp.x).F(pp.y).F(pp.z + 0.3f).I(1).I(0).I(0).I(1).I(0);
                g_dl.los = RunCmd(c);
            }
        }
        bool straight = level && dist < 70.f && g_dl.los;
        if (straight != g_dl.straight) {
            g_dl.straight = straight;
            { Cmd c(0x04E0); c.I(g_dl.van).I(straight ? 100 : 20); RunCmd(c); }
            g_dl.tMeet = 0; g_dl.lastTarget = { 1e9f, 1e9f, 0.f };
            Log("traficante: %s (%.0f m, %.1f m de altura)", straight ? "perto, no seu nivel e com caminho livre; vem reto ate voce" : "volta a seguir as ruas", dist, vp.z - pp.z);
        }
        if (now - g_dl.tMeet > 3000 && Dist2(g_dl.lastTarget, pp) > (straight ? 4.f : 15.f)) {
            if (straight) {                                                    // mira 8 m antes do CJ, devagar
                float dx = vp.x - pp.x, dy = vp.y - pp.y, l = sqrtf(dx * dx + dy * dy);
                V3 t = l > 8.f ? V3{ pp.x + dx / l * 8.f, pp.y + dy / l * 8.f, pp.z } : vp;
                DealerDrive(t, 9.f);
            }
            else { g_dl.meetLow = !MeetNode(pp, &g_dl.meet); DealerDrive(g_dl.meet, 22.f); }
            g_dl.lastTarget = pp; g_dl.tMeet = now;
        }
        if (dist < g_dl.bestDist - 3.f) { g_dl.bestDist = dist; g_dl.tProg = now; }
        bool there = (dist < 14.f && level) || (g_dl.meetLow && Dist2(vp, g_dl.meet) < 12.f);
        bool stuck = now - g_dl.tProg > 60000;                                // longe: 60 s sem chegar mais perto
        bool closest = now - g_dl.tProg > 15000 && ((level && dist < 100.f) || (g_dl.pulled && Dist2(vp, g_dl.lastPull) < 15.f));
        if (there || closest) {
            { Cmd c(0x00AD); c.I(g_dl.van).F(0.f); RunCmd(c); }
            g_dl.state = DL_STOPPING; g_dl.tPhase = now; g_dl.arriveDist = dist;
            if (closest) Log("traficante: nao consegue chegar mais perto; para a %.0f m do CJ (%.1f m de altura); CJ em (%.0f, %.0f, %.0f)", dist, vp.z - pp.z, pp.x, pp.y, pp.z);
        } else if (stuck) {                                                   // perdido ou preso longe: puxa para perto
            V3 n; float hd = 0.f;
            if (PullNode(pp, &n, &hd)) {
                { Cmd c(0x00AB); c.I(g_dl.van).F(n.x).F(n.y).F(n.z + 1.f); RunCmd(c); }
                { Cmd c(0x0175); c.I(g_dl.van).F(hd); RunCmd(c); }
                g_dl.pulled = true; g_dl.lastPull = n;
                g_dl.straight = false; g_dl.tMeet = 0; g_dl.lastTarget = { 1e9f, 1e9f, 0.f };
                Log("traficante: %s; puxado para (%.0f, %.0f, %.0f), %.0f m do CJ em (%.0f, %.0f, %.0f)",
                    "60 s sem chegar mais perto", n.x, n.y, n.z, Dist2(n, pp), pp.x, pp.y, pp.z);
            } else Log("traficante: perdido, sem rua no seu nivel entre 25 e 150 m; CJ em (%.0f, %.0f, %.0f)", pp.x, pp.y, pp.z);
            g_dl.t0 = g_dl.tProg = now; g_dl.bestDist = 1e9f;
        }
        break;
    }
    case DL_STOPPING: {
        const float* mv = (const float*)(v + 0x44);
        float sp = sqrtf(mv[0] * mv[0] + mv[1] * mv[1] + mv[2] * mv[2]);
        if (sp < 0.02f || now - g_dl.tPhase > 4000) {
            if (!g_dlLegal && WantedLevel() > 0 && CopNear(EntityPos(v), 60.f, false, nullptr)) {
                GarHelp("A policia esta atras de voce. O traficante nao parou.");
                Log("traficante: chegou com a policia atras do CJ; foi embora");
                DealerGo(DL_FLEE, pp);
                return;
            }
            { Cmd c(0x00AF); c.I(g_dl.van).I(11); RunCmd(c); }               // MISSION_STOP_FOREVER
            g_dl.doorsOpen = false;
            g_dl.state = DL_OPEN; g_dl.tPhase = g_dl.tLast = now; g_dl.armed = true; g_dl.tFar = 0;
            V3 vp = EntityPos(v);
            Log("traficante: chegou a %.0f m (%.1f m de altura de diferenca%s); portas fechadas ate voce chegar na traseira",
                dist, vp.z - pp.z, g_dl.meetLow ? ", sem rua no seu nivel" : "");
            if (dist >= 20.f || g_dl.meetLow) GarHelp("O traficante parou a %.0f m, o mais perto que deu. Va ate a traseira da van.", dist);
            else GarHelp("O traficante chegou. Va ate a traseira da van.");
        }
        break;
    }
    case DL_OPEN: {
        if (dead) { DealerGo(DL_LEAVE, pp); Log("traficante: o CJ caiu; foi embora"); return; }
        V3 rear = VanRear(v);
        {                                                                      // portas: so com o CJ na traseira
            float drr = Dist2(rear, pp), dzr = fabsf(rear.z - pp.z);
            bool inVeh = PlayerVehicle() != nullptr;
            if (!g_dl.doorsOpen && drr < (float)g_armDoorDist && dzr < 3.f && !inVeh) {
                DealerDoors(true);
                Log("traficante: o CJ chegou na traseira; portas abertas");
            } else if (g_dl.doorsOpen && !g_shopOpen && !g_dl.asking && (drr > g_armDoorDist + 4.f || dzr >= 3.f || inVeh)) {
                DealerDoors(false);
                Log("traficante: o CJ saiu da traseira; portas fechadas");
            }
            if (g_dl.doorsOpen && now - g_dl.tDoorFix >= 250) {               // segura as portas abertas (o CJ esbarra)
                g_dl.tDoorFix = now;
                SetVanDoor(v, g_dl.doorA, 1.f, false);
                SetVanDoor(v, g_dl.doorB, 1.f, false);
            }
        }
        static DWORD copTick = 0;
        if (!g_dlLegal && now - copTick >= 250) {                              // o armeiro e legalizado: a policia nao liga
            copTick = now;
            V3 cop = rear;
            if (WantedLevel() > 0 && CopNear(rear, 60.f, false, &cop)) {
                bool shop = g_shopOpen;
                if (shop) { Cmd c(0x010E); c.I(0).I(2); RunCmd(c); }
                GarHelp("A policia esta atras de voce. O traficante fugiu.");
                Log("traficante: policia atras do CJ chegou perto%s; fugiu", shop ? " com o menu aberto" : "");
                DealerGo(DL_FLEE, cop);
                return;
            }
            if (g_dl.doorsOpen && CopSeesRear(v, rear, &cop)) {
                if (g_shopOpen) {
                    CloseShop(false);
                    { Cmd c(0x010E); c.I(0).I(2); RunCmd(c); }
                    GarHelp("A policia viu voce comprando! O traficante fugiu.");
                    Log("traficante: a policia viu a negociacao; 2 estrelas, ele fugiu");
                    DealerGo(DL_FLEE, cop);
                } else {
                    DealerCooldown("a policia viu a van aberta");
                    GarHelp("A policia viu a van do traficante. Ele fugiu e nao vai atender por um dia.");
                    StartChase(cop);
                }
                return;
            }
        }
        float leaveAt = (float)g_armLeave;                                     // parou longe: conta a partir de onde parou
        if (g_dl.arriveDist + 20.f > leaveAt) leaveAt = g_dl.arriveDist + 20.f;
        if (dist > leaveAt) {                                                  // 10 s de tolerancia (ou o dobro da distancia)
            if (!g_dl.tFar) { g_dl.tFar = now; GarHelp("Volte para perto da van, ou o traficante vai embora."); }
            if (now - g_dl.tFar > 10000 || dist > leaveAt * 2.f) {
                GarHelp("Voce se afastou. O traficante foi embora.");
                Log("traficante: o CJ se afastou %.0f m; foi embora", dist);
                DealerGo(DL_LEAVE, pp);
                return;
            }
        } else g_dl.tFar = 0;
        if (!g_shopOpen && now - g_dl.tLast > 180000) {
            GarHelp("O traficante cansou de esperar e foi embora.");
            Log("traficante: 3 minutos sem compra; foi embora");
            DealerGo(DL_LEAVE, pp);
            return;
        }
        if (!g_shopOpen) {
            float dr = Dist2(rear, pp), dz = fabsf(rear.z - pp.z);
            bool here = dr < 1.6f && dz < 2.f && !PlayerVehicle() && !MenuActive() && !g_menuOpen && !g_kbMode && g_ctxCache == CTX_FOOT;
            const float* mv = (const float*)(me + 0x44);                      // CPhysical::m_vecMoveSpeed
            bool still = mv[0] * mv[0] + mv[1] * mv[1] < 0.004f * 0.004f;         // menos de ~0,2 m/s
            if (g_dl.asking) {
                if (!here || dr > 2.2f) { DealerAskEnd(); g_dl.tStillMe = 0; break; }
                int yn = 0;                                                    // setas de conversa: 11 Sim, 10 Nao
                { Cmd c(0x00E1); c.I(0).I(11); if (RunCmd(c)) yn |= 1; }
                { Cmd c(0x00E1); c.I(0).I(10); if (RunCmd(c)) yn |= 2; }
                int pressed = yn & ~g_dl.ynWas;
                g_dl.ynWas = yn;
                if (pressed & 1) { DealerAskEnd(); g_dl.tStillMe = 0; OpenShop(); }
                else if (pressed & 2) { DealerAskEnd(); DealerDismiss(); return; }
                break;
            }
            if (!here || !still) { g_dl.tStillMe = 0; break; }
            if (!g_dl.tStillMe) g_dl.tStillMe = now;
            if (now - g_dl.tStillMe >= 1000) {
                g_dl.asking = true; g_dealerYN = true; g_dl.ynWas = 3;          // so vale apertar de novo
                if (g_origSetHelp) g_origSetHelp(g_dlLegal ? "Quer comprar? Sim abre o menu. Nao dispensa o armeiro." : "Quer comprar? Sim abre o menu. Nao dispensa o traficante.", true, true, false);
                Log("traficante: perguntou se quer comprar");
            }
        }
        break;
    }
    case DL_LEAVE: case DL_FLEE: case DL_CHASE: {
        if (!vanOk || !drvOk) {
            if (g_dl.state == DL_CHASE) Log("traficante: %s na perseguicao", vanOk ? "o traficante caiu" : "a van foi destruida");
            DealerEnd(g_dl.state == DL_CHASE ? "fim da perseguicao" : "van parada");
            return;
        }
        bool onScreen = false;
        { Cmd c(0x02CA); c.I(g_dl.van); onScreen = RunCmd(c); }
        V3 vp = EntityPos(v);
        if (g_dl.state != DL_LEAVE && (now - g_dl.tFleeTarget > 6000 || Dist2(vp, g_dl.fleeTarget) < 60.f)) {
            V3 threat = g_dl.threat;
            if (g_dl.state == DL_CHASE) {                                     // foge da viatura mais perto
                float best = 1e9f;
                for (int k = 0; k < g_dl.nCops; k++) if (char* c = VehByRef(g_dl.cops[k].car)) { float dd = Dist2(EntityPos(c), vp); if (dd < best) { best = dd; threat = EntityPos(c); } }
            } else threat = pp;
            g_dl.fleeTarget = FleePoint(vp, threat); g_dl.tFleeTarget = now;
            DealerDrive(g_dl.fleeTarget, 45.f);
        }
        if (g_dl.state == DL_CHASE) {
            float nearest = 1e9f;
            for (int k = 0; k < g_dl.nCops; k++) if (char* c = VehByRef(g_dl.cops[k].car)) { float dd = Dist2(EntityPos(c), vp); if (dd < nearest) nearest = dd; }
            const float* mv = (const float*)(v + 0x44);
            float sp = sqrtf(mv[0] * mv[0] + mv[1] * mv[1]);
            if (sp < 0.05f && nearest < 12.f) { if (!g_dl.tStill) g_dl.tStill = now; } else g_dl.tStill = 0;
            if (g_dl.tStill && now - g_dl.tStill > 10000) { Log("traficante: preso pela policia"); DealerEnd("preso"); return; }
            if ((g_dl.nCops && nearest > 200.f) || (!onScreen && dist > 150.f) || dist > 300.f || now - g_dl.tPhase > 150000) {
                Log("traficante: escapou da policia (%.0f m da viatura mais perto)", nearest < 1e8f ? nearest : -1.f);
                DealerEnd("escapou");
                return;
            }
        } else if ((!onScreen && dist > 80.f) || dist > 250.f || now - g_dl.tPhase > 90000) {
            DealerEnd(g_dl.state == DL_FLEE ? "fugiu" : "foi embora");
            return;
        }
        break;
    }
    }
}

// ---------------------------------------------------------------- dar a arma para um pedestre ([ArmaPedestre])
// Status + Correr, a pe, com uma arma na mao: o pedestre mais perto na sua frente (ate 2,5 m, a pe) ganha a mesma
// arma, com Ammo de municao, ja na mao; a sua fica com voce. Antes, os dois se cumprimentam (GANGS.ifp, Anim: o
// abraco de gangue por padrao, com ele a Distance de voce). NPC de missao que nao e do seu grupo faz so o aperto de
// mao parcial (AnimPartial, parte de cima do corpo): a animacao inteira apagaria a tarefa que o script deu a ele.
// Ficam de fora: soco, detonador, camera, oculos, paraquedas e presentes.
static int   g_giveOn = 1, g_giveAmmo = 9999;
static float g_giveDist = 1.0f;
static char  g_giveAnim[32] = "hndshkfa", g_giveAnimPart[32] = "prtial_hndshk_01";
static volatile bool g_giveReq = false;
static bool  g_giveCombo = false;
enum { GV_NONE = 0, GV_LOAD, GV_PLAY };
struct GiveState { int phase, ped, weapon; bool full, anim; DWORD t0; };
static GiveState g_give = {};
static void GiveCombo(WORD buttons, PadState& p) {                            // thread do XInput
    if (!g_giveOn) return;
    const WORD LB = 0x0100, RUN = 0x1000;                                     // Status (TAB) e Correr (A)
    if (!(buttons & LB) || !(buttons & RUN)) { g_giveCombo = false; return; }
    if (!g_giveCombo) {
        if (g_ctxCache != CTX_FOOT || g_menuOpen || g_shopOpen) return;
        g_giveCombo = true; g_giveReq = true;
        Log("dar arma: Status + Correr");
    }
    p.wButtons &= (WORD)~RUN;                                                 // esse Correr nao chega ao jogo
}
static bool GiveAllowed(int type) {
    if (type == 43 || type == 44 || type == 45 || type == 46) return false;   // camera, oculos, paraquedas
    const ArmItem* it = ArmByType(type);
    return it && it->type != ARM_ARMOUR;                                      // soco, detonador e presentes nao estao na lista
}
static char* FindGiveTarget(char* me) {
    V3 mp = EntityPos(me);
    const float* m = Rd<const float*>(me, 0x14);
    float fx = m ? m[4] : 0.f, fy = m ? m[5] : 1.f;
    char* pool = *(char**)0xB74490;
    if (!pool) return nullptr;
    char* best = nullptr; float bestD = 2.5f;
    int size = Rd<int>(pool, 8);
    for (int i = 0; i < size; i++) {
        if (Rd<BYTE*>(pool, 4)[i] & 0x80) continue;
        char* p = Rd<char*>(pool, 0) + i * 0x7C4;
        if (p == me || Rd<float>(p, 0x540) <= 0.f || (Rd<BYTE>(p, 0x46D) & 1)) continue;
        V3 pp = EntityPos(p);
        float dx = pp.x - mp.x, dy = pp.y - mp.y, d = sqrtf(dx * dx + dy * dy);
        if (d >= bestD || fabsf(pp.z - mp.z) > 1.5f) continue;
        if (d > 0.2f && (dx * fx + dy * fy) / d < 0.25f) continue;           // na frente (uns 75 graus para cada lado)
        best = p; bestD = d;
    }
    return best;
}
static void GivePose() {                                                     // um de frente para o outro
    char* me = PlayerPed(); char* t = PedByRef(g_give.ped); int pr = PlayerRef();
    if (!me || !t || pr < 0) return;
    V3 a = EntityPos(me), b = EntityPos(t);
    float dx = b.x - a.x, dy = b.y - a.y, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.05f) { const float* m = Rd<const float*>(me, 0x14); dx = m ? m[4] : 0.f; dy = m ? m[5] : 1.f; l = 1.f; }
    dx /= l; dy /= l;
    float h = atan2f(-dx, dy) * 57.2957795f;                                   // direcao do jogo: 0 = norte, anti-horario
    { Cmd c(0x0173); c.I(pr).F(h); RunCmd(c); }
    { Cmd c(0x0173); c.I(g_give.ped).F(h + 180.f); RunCmd(c); }
    if (g_give.full && g_give.anim) {                                         // a distancia certa para o abraco encaixar
        Cmd c(0x0972); c.I(g_give.ped).F(a.x + dx * g_giveDist).F(a.y + dy * g_giveDist).F(b.z); RunCmd(c);
    }
}
static void GiveAnim(int ped, const char* anim, bool secondary) {
    Cmd c(secondary ? 0x0A1A : 0x0605);
    c.I(ped).Str(anim).Str("GANGS").F(4.f).I(0).I(0).I(0).I(0).I(-1);
    RunCmd(c);
}
static void GiveEnd(bool gave, const char* why) {
    char* me = PlayerPed(); int pr = PlayerRef();
    char* p = PedByRef(g_give.ped);
    if (gave && p && Rd<float>(p, 0x540) > 0.f) {
        const ArmItem* it = ArmByType(g_give.weapon);
        if (it && it->model > 0) { RequestModel(it->model, 2); if (it->model2 > 0) RequestModel(it->model2, 2); LoadAllRequestedModels(false); }
        { Cmd c(0x01B2); c.I(g_give.ped).I(g_give.weapon).I(g_giveAmmo); RunCmd(c); }
        { Cmd c(0x01B9); c.I(g_give.ped).I(g_give.weapon); RunCmd(c); }
        if (it && it->model > 0) { Cmd c(0x0249); c.I(it->model); RunCmd(c); }
        if (it && it->model2 > 0) { Cmd c(0x0249); c.I(it->model2); RunCmd(c); }
        Log("dar arma: %s para o pedestre %d (%s, %d de municao)", it ? it->name : "?", Rd<short>(p, 0x22),
            !g_give.anim ? "sem animacao" : g_give.full ? "abraco" : "aperto de mao", g_giveAmmo);
    } else Log("dar arma: cancelado (%s)", why);
    if (me && pr >= 0) {
        if (Rd<float>(me, 0x540) > 0.f) { Cmd c(0x01B9); c.I(pr).I(g_give.weapon); RunCmd(c); }   // a sua volta para a mao
        PlayerControl(true);
    }
    if (g_give.anim) { Cmd c(0x04EF); c.Str("GANGS"); RunCmd(c); }
    memset(&g_give, 0, sizeof g_give); g_give.ped = -1;
}
static void GiveStart() {
    char* me = PlayerPed(); int pr = PlayerRef();
    if (!me || pr < 0 || PlayerVehicle() || MenuActive() || g_menuOpen || g_shopOpen || g_kbMode || Rd<float>(me, 0x540) <= 0.f) return;
    int type = WeaponType(me, Rd<signed char>(me, 0x718) & 0x0F);               // CPed::m_nActiveWeaponSlot
    if (!GiveAllowed(type)) { GarHelp("Pegue uma arma na mao para dar a alguem."); return; }
    char* t = FindGiveTarget(me);
    if (!t) { GarHelp("Chegue perto de alguem, de frente, para dar a arma."); return; }
    int ph = PedRef(t);
    if (ph < 0) return;
    bool mission = Rd<BYTE>(t, 0x484) == 2;                                   // CPed::m_nCreatedBy: 2 = script
    bool group = false;
    { int g = -1; Cmd c(0x07AF); c.I(0).Out(0); RunCmd(c, &g, 1); if (g >= 0) { Cmd c2(0x06EE); c2.I(ph).I(g); group = RunCmd(c2); } }
    memset(&g_give, 0, sizeof g_give);
    g_give.ped = ph; g_give.weapon = type; g_give.full = !mission || group; g_give.t0 = GameMs();
    g_give.anim = (g_give.full ? g_giveAnim[0] : g_giveAnimPart[0]) != 0;
    PlayerControl(false);
    { Cmd c(0x01B9); c.I(pr).I(0); RunCmd(c); }                              // guarda a arma durante o cumprimento
    if (g_give.full) { Cmd c(0x05BA); c.I(ph).I(5000); RunCmd(c); }          // para de andar ate o cumprimento
    GivePose();
    Log("dar arma: %s para o pedestre %d (%s%s)", ArmByType(type)->name, Rd<short>(t, 0x22), mission ? "de missao" : "da rua", group ? ", do seu grupo" : "");
    if (!g_give.anim) { GiveEnd(true, ""); return; }
    { Cmd c(0x04ED); c.Str("GANGS"); RunCmd(c); }
    g_give.phase = GV_LOAD;
}
static void GiveTick() {
    if (!g_giveOn) return;
    if (g_giveReq) { g_giveReq = false; if (g_give.phase == GV_NONE) GiveStart(); }
    if (g_give.phase == GV_NONE) return;
    char* me = PlayerPed(); char* p = PedByRef(g_give.ped);
    DWORD now = GameMs();
    if (!me || Rd<float>(me, 0x540) <= 0.f || PlayerVehicle()) { GiveEnd(false, "o CJ saiu ou caiu"); return; }
    if (!p || Rd<float>(p, 0x540) <= 0.f || (Rd<BYTE>(p, 0x46D) & 1)) { GiveEnd(false, "o pedestre sumiu, caiu ou entrou num veiculo"); return; }
    const char* anim = g_give.full ? g_giveAnim : g_giveAnimPart;
    if (g_give.phase == GV_LOAD) {
        bool loaded = false;
        { Cmd c(0x04EE); c.Str("GANGS"); loaded = RunCmd(c); }
        if (!loaded) { if (now - g_give.t0 > 3000) { Log("dar arma: a animacao nao carregou; vai sem ela"); GiveEnd(true, ""); } return; }
        int pr = PlayerRef();
        GivePose();
        GiveAnim(pr, anim, !g_give.full);
        GiveAnim(g_give.ped, anim, !g_give.full);
        g_give.phase = GV_PLAY; g_give.t0 = now;
        return;
    }
    bool playing = false;
    { Cmd c(0x0611); c.I(PlayerRef()).Str(anim); playing = RunCmd(c); }
    if (!playing) { Cmd c(0x0611); c.I(g_give.ped).Str(anim); playing = RunCmd(c); }
    if ((now - g_give.t0 > 600 && !playing) || now - g_give.t0 > 7000) GiveEnd(true, "");
}

// ---------------------------------------------------------------- armas protegidas em missao ([ProtegerArmas])
// Durante uma missao, a missao nao troca nem tira as suas armas de fogo dos espacos protegidos (padrao: pistolas,
// escopetas, submetralhadoras e fuzis de assalto). Rifles e armas pesadas ficam com a missao (o lanca-chamas de
// Are You Going to San Fierro? e o rifle de precisao sao necessarios), e tambem corpo a corpo, arremesso e itens.
// - no comeco da missao, anota as armas de fogo do CJ (espacos 2 a 7) e a municao de cada uma;
// - a missao da outra arma num espaco protegido onde esta a sua: a arma continua a sua e a municao vai para ela;
// - a missao tira a sua (REMOVE_WEAPON_FROM_CHAR): nao tira;
// - a missao da municao infinita (9999 ou mais) ou mexe na municao da sua: no fim volta ao que voce tinha ao
//   comecar; se teve infinita e voce venceu, mais 1 caixa (a do traficante: 9mm 50, calibre 12 25, fuzil 50...);
// - a missao tira todas as armas (REMOVE_ALL_CHAR_WEAPONS): tira, e as suas de fogo voltam quando ela termina;
// - a missao pergunta pela arma dela (tem, esta na mao, quanta municao, poe na mao): responde pela sua;
// - morreu ou foi preso: vale a regra do jogo. Missoes da lista Exclude ficam de fora.
// Os ganchos ficam nas chamadas de dentro dos comandos de script (so script passa por eles):
//   01B2 0x47D335 CPed::GiveWeapon, 0555 0x48D696 CPed::ClearWeapon, 048F 0x48AE86 CPed::ClearWeapons,
//   017B 0x47CDF1 CPed::SetAmmo; 0491 0x48AEAF, 02D8 0x480233, 01B9 0x47D4BD, 041A 0x489B0B: CPool<CPed>::GetAt,
//   logo antes de o comando ler o tipo da arma (ScriptParams[1]).
static int  g_pmOn = 1, g_pmInf = 9999, g_pmBoxOnFail = 0;
static bool g_pmProt[13];
static char g_pmExclude[256];

struct PmSlot { int type, ammo, ammoRemoved; bool infinite, touched, removed; };
static struct {
    bool active, excluded, named;
    char name[9];
    char* ped;
    unsigned t0;
    int passed0;
    PmSlot s[13];
    BYTE alias[64];                                                          // tipo da missao -> o seu
} g_pm;

typedef int   (__thiscall* GiveWeapon_t)(void* ped, int type, int ammo, int likeUnderwater);
typedef void  (__thiscall* ClearWeapon_t)(void* ped, int type);
typedef void  (__thiscall* ClearWeapons_t)(void* ped);
typedef void  (__thiscall* SetAmmo_t)(void* ped, int type, int ammo);
typedef char* (__thiscall* PedPoolAt_t)(void* pool, int handle);
static const GiveWeapon_t   PedGiveWeapon   = (GiveWeapon_t)0x5E6080;
static const ClearWeapon_t  PedClearWeapon  = (ClearWeapon_t)0x5E62B0;
static const ClearWeapons_t PedClearWeapons = (ClearWeapons_t)0x5E6320;
static const SetAmmo_t      PedSetAmmo      = (SetAmmo_t)0x5DF290;
static const PedPoolAt_t    PedPoolAt       = (PedPoolAt_t)0x404910;

static int WSlot(int type) {                                                  // aWeaponInfo[type].m_nSlot
    if (type <= 0 || type > 46) return -1;
    int s = *(const int*)(0xC8AAB8 + type * 0x70 + 0x14);
    return s >= 0 && s < 13 ? s : -1;
}
static int WType(const char* ped, int s) { return Rd<int>(ped, 0x5A0 + s * 0x1C); }
static int WAmmo(const char* ped, int s) { return Rd<int>(ped, 0x5A0 + s * 0x1C + 0xC); }
static const char* WName(int type) {
    const ArmItem* it = ArmByType(type);
    static char buf[4][16]; static int k = 0;
    if (it) return it->name;
    k = (k + 1) & 3; snprintf(buf[k], sizeof buf[k], "arma %d", type);
    return buf[k];
}
static int WBox(int type) { const ArmItem* it = ArmByType(type); return it && it->box > 0 ? it->box : 0; }

static char* MissionScript() {                                                // CTheScripts::pActiveScripts
    char* s = *(char**)0xA8B42C;
    for (int n = 0; s && n < 256; n++, s = *(char**)s)
        if (Rd<BYTE>(s, 0xDC)) return s;                                       // m_bIsMission
    return nullptr;
}
static bool PmExcluded(const char* name) {
    const char* p = g_pmExclude;
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == ';') p++;
        char tok[16]; int n = 0;
        while (*p && *p != ',' && *p != ' ' && *p != ';') { if (n < 15) tok[n++] = *p; p++; }
        tok[n] = 0;
        if (n && !_strnicmp(tok, name, 8) && (n >= 8 || !name[n])) return true;
    }
    return false;
}
static void PmName() {
    char* ms = MissionScript();
    if (!ms) return;
    memcpy(g_pm.name, ms + 8, 8); g_pm.name[8] = 0;
    g_pm.named = true;
    g_pm.excluded = PmExcluded(g_pm.name);
}
static void PmBegin() {
    memset(&g_pm, 0, sizeof g_pm);
    char* ped = PlayerPed();
    g_pm.active = true; g_pm.ped = ped; g_pm.t0 = *TimeMs; g_pm.passed0 = *StatMissionsPassed;
    strcpy(g_pm.name, "?");
    PmName();
    if (g_pm.excluded) { Log("armas: missao %s comecou; esta na lista Exclude, fica como no jogo", g_pm.name); return; }
    char list[256]; int o = 0; list[0] = 0;
    for (int s = 2; s <= 7 && ped; s++) {
        int t = WType(ped, s), a = WAmmo(ped, s);
        if (t <= 0 || t > 46) continue;
        g_pm.s[s].type = t; g_pm.s[s].ammo = a;
        o += snprintf(list + o, sizeof list - o, "%s%s %d%s", o ? ", " : "", WName(t), a, g_pmProt[s] ? "" : " (fica com a missao)");
    }
    Log("armas: missao %s comecou; suas armas de fogo: %s", g_pm.name, o ? list : "nenhuma");
}
static bool PmFor(const char* ped) {                                          // o gancho vale para este ped agora?
    if (!g_pmOn || g_runDepth || !ped || ped != PlayerPed()) return false;
    if (!IsPlayerOnAMission()) return false;
    if (!g_pm.active) PmBegin();
    else if (!g_pm.named) PmName();
    return !g_pm.excluded;
}
static PmSlot* PmProtSlot(int s) { return s >= 2 && s <= 7 && g_pmProt[s] && g_pm.s[s].type ? &g_pm.s[s] : nullptr; }

static int __thiscall PmGive(char* ped, int type, int ammo, int uw) {
    if (PmFor(ped)) {
        int s = WSlot(type);
        PmSlot* p = PmProtSlot(s);
        if (p) {
            if (WType(ped, s) == p->type && type != p->type) {
                g_pm.alias[type] = (BYTE)p->type;
                Log("armas: a missao daria %s no lugar da sua %s; fica a sua, com os %d tiros", WName(type), WName(p->type), ammo);
                type = p->type;
            }
            if (type == p->type && ammo >= g_pmInf && !p->infinite) {
                p->infinite = p->touched = true;
                Log("armas: municao infinita na sua %s; no fim volta a %d%s", WName(p->type), p->ammo, WBox(p->type) ? " (mais 1 caixa se vencer)" : "");
            }
        }
    }
    return PedGiveWeapon(ped, type, ammo, uw);
}
static void __thiscall PmClear(char* ped, int type) {
    if (PmFor(ped)) {
        int s = WSlot(type);
        PmSlot* p = PmProtSlot(s);
        if (p && WType(ped, s) == p->type && (type == p->type || g_pm.alias[type] == p->type)) {
            Log("armas: a missao tiraria a sua %s; ficou com voce", WName(p->type));
            return;
        }
    }
    PedClearWeapon(ped, type);
}
static void __thiscall PmClearAll(char* ped) {
    if (PmFor(ped)) {
        int n = 0;
        for (int s = 2; s <= 7; s++) {
            PmSlot& p = g_pm.s[s];
            if (!p.type || WType(ped, s) != p.type) continue;
            if (!p.removed) p.ammoRemoved = WAmmo(ped, s);
            p.removed = true; n++;
        }
        Log("armas: a missao tirou todas as armas; %s", n ? "as suas de fogo voltam quando ela terminar" : "nenhuma arma de fogo sua estava com o CJ");
    }
    PedClearWeapons(ped);
}
static void __thiscall PmSetAmmo(char* ped, int type, int ammo) {
    if (PmFor(ped)) {
        int s = WSlot(type);
        PmSlot* p = PmProtSlot(s);
        if (p && WType(ped, s) == p->type) {
            if (type != p->type) g_pm.alias[type] = (BYTE)p->type;
            type = p->type;
            if (!p->touched) Log("armas: a missao poe %d tiros na sua %s; no fim volta a %d", ammo, WName(p->type), p->ammo);
            p->touched = true;
            if (ammo >= g_pmInf && !p->infinite) { p->infinite = true; Log("armas: e municao infinita (mais 1 caixa no fim se vencer)"); }
        }
    }
    PedSetAmmo(ped, type, ammo);
}
static char* __thiscall PmPedAt(void* pool, int handle) {                    // comandos que perguntam pela arma
    char* ped = PedPoolAt(pool, handle);
    if (ped && g_pm.active && PmFor(ped)) {
        int t = *(volatile int*)0xA43C7C;                                     // ScriptParams[1]: tipo da arma
        if (t > 0 && t < 64 && g_pm.alias[t]) {
            int y = g_pm.alias[t];
            if (WType(ped, WSlot(y)) == y) *(volatile int*)0xA43C7C = y;
        }
    }
    return ped;
}

static void PmGiveBack(char* ped, int type, int ammo) {
    const ArmItem* it = ArmByType(type);
    g_runDepth++;
    bool ok = it ? ArmGive(*it, ammo) : (PedGiveWeapon(ped, type, ammo, 1), true);
    g_runDepth--;
    if (ok) PedSetAmmo(ped, type, ammo);
    Log("armas: %s %s com %d tiros", ok ? "voltou a sua" : "NAO voltou (modelo nao carregou):", WName(type), ammo);
}
static void PmEnd(const char* why) {
    g_pm.active = false;
    if (g_pm.excluded) return;
    char* ped = PlayerPed();
    if (!ped || ped != g_pm.ped || *TimeMs < g_pm.t0) { Log("armas: missao %s terminou junto com um jogo carregado; nada mudou", g_pm.name); return; }
    if (Rd<BYTE>(PlayerInfo(), 0xDC) != 0 || Rd<float>(ped, 0x540) <= 0.f) {  // m_nPlayerState: morreu / preso
        Log("armas: missao %s terminou com o CJ morto ou preso; vale a regra do jogo", g_pm.name);
        return;
    }
    bool passed = *StatMissionsPassed != g_pm.passed0;
    Log("armas: missao %s terminou (%s%s)", g_pm.name, why, passed ? ", vencida" : "");
    for (int s = 2; s <= 7; s++) {
        PmSlot& p = g_pm.s[s];
        if (!p.type) continue;
        int ct = WType(ped, s), ca = WAmmo(ped, s);
        if (!g_pmProt[s]) {                                                    // rifles e pesadas: so a devolucao
            if (p.removed && ct <= 0) PmGiveBack(ped, p.type, p.ammoRemoved);
            continue;
        }
        if (ct != p.type && !p.removed) {
            if (ct > 0) Log("armas: no espaco da sua %s agora esta %s (voce pegou); fica", WName(p.type), WName(ct));
            continue;
        }
        int want = -1;
        if (p.infinite) want = p.ammo + ((passed || g_pmBoxOnFail) ? WBox(p.type) : 0);
        else if (p.removed) want = ct == p.type && ca > p.ammoRemoved ? ca : p.ammoRemoved;
        else if (p.touched) want = ca > p.ammo ? ca : p.ammo;
        if (ct != p.type) { PmGiveBack(ped, p.type, want >= 0 ? want : p.ammoRemoved); continue; }
        if (want >= 0 && want != ca) {
            PedSetAmmo(ped, p.type, want);
            Log("armas: sua %s volta a %d tiros (estava com %d%s)", WName(p.type), want, ca, p.infinite && want > p.ammo ? ", +1 caixa" : "");
        }
    }
}
static void ProtOnLoad() { if (g_pm.active) Log("armas: jogo carregado durante a missao %s; a protecao recomeca", g_pm.name); memset(&g_pm, 0, sizeof g_pm); }
static void ProtTick() {
    if (!g_pmOn) return;
    bool on = IsPlayerOnAMission();
    if (g_pm.active) {
        if (!on) { PmEnd("acabou"); return; }
        char* ms = MissionScript();
        if (ms && g_pm.named && strncmp(ms + 8, g_pm.name, 8)) { PmEnd("outra missao comecou"); PmBegin(); return; }
        if (ms && !g_pm.named) PmName();
    } else if (on && PlayerPed()) PmBegin();
}

static bool PatchCallSite(DWORD site, DWORD target, void* fn, const char* what) {
    BYTE exp[5]; exp[0] = 0xE8; *(int*)(exp + 1) = (int)(target - (site + 5));
    BYTE rep[5]; rep[0] = 0xE8; *(int*)(rep + 1) = (int)((DWORD)fn - (site + 5));
    return PatchBytes(site, exp, 5, rep, 5, what);
}
// Pay 'n' Spray: carro seu nao muda de cor. O conserto sorteia cores novas (CGarage::Update 0x44B125 ->
// CVehicleModelInfo::ChooseVehicleColour 0x4C8500, para o carro do jogador); aqui, se o carro e seu, as "novas"
// sao as que ele ja tem. O resto do servico (conserto, lavagem, estrelas) fica igual.
typedef void (__thiscall* ChooseColour_t)(void* mi, BYTE* c1, BYTE* c2, BYTE* c3, BYTE* c4, int variation);
static void __thiscall PnsColour(void* mi, BYTE* c1, BYTE* c2, BYTE* c3, BYTE* c4, int variation) {
    ((ChooseColour_t)0x4C8500)(mi, c1, c2, c3, c4, variation);
    char* v = PlayerVehicle();
    if (!g_garOn || !v || FindGarVeh(v) < 0) return;
    *c1 = Rd<BYTE>(v, 0x434); *c2 = Rd<BYTE>(v, 0x435); *c3 = Rd<BYTE>(v, 0x436); *c4 = Rd<BYTE>(v, 0x437);
    Log("garagem: Pay 'n' Spray no %s seu: conserta e lava, a cor fica", CarName(Rd<short>(v, 0x22)));
}
// Resposta a pedestre que puxou conversa, com skin: o jogo faz o CJ responder com falas que so a voz do jogador tem
// (LIKE_*_REPLY, LIKE_DISMISS_*, LIKE_NEGATIVE_*, WHERE_YOU_FROM_*, GANGBANG_YES/NO; coluna "player" da tabela
// 0x8C6A68). Com a voz de um pedestre (tipo 0 comum ou 3 gangue) elas nao existem e a skin fica muda. Aqui, nas
// chamadas de CPed::Say (0x5EFFE0) da conversa (CPedToPlayerConversations::Update, 0x43B4xx-0x43C0C7), a fala do CJ
// vira a fala de conversa que essas vozes tem (PCONV_*), com o tom da resposta:
//   sim a um elogio: AGREE_GOOD (amigavel); sim a uma critica: AGREE_BAD (a contragosto);
//   nao a um elogio: DISMISS (de pouco caso); nao a uma critica ou a gangue: ANS_NO (irritado).
// Se a voz nao tiver a fala, tenta a proxima do mesmo tom. Fala de pedestre e voz do CJ passam direto.
typedef short (__thiscall* PedSay_t)(void* ped, int ctx, int offset, float vol, int a, int b, int c);
enum { PC_AGREE_BAD = 0xA1, PC_AGREE_GOOD = 0xA2, PC_ANS_NO = 0xA3, PC_DISMISS = 0xA4, PC_STATE_BAD = 0xAA, PC_STATE_GOOD = 0xAB };
static short __thiscall ConvSay(char* ped, int ctx, int offset, float vol, int a, int b, int c) {
    PedSay_t say = (PedSay_t)0x5EFFE0;
    WORD k = (WORD)ctx;
    if (!ped || ped != PlayerPed()) return say(ped, ctx, offset, vol, a, b, c);
    int vtype = Rd<short>(ped + 0x294, 0x92);                                // m_nVoiceType: 2 = jogador
    if (vtype != 0 && vtype != 3) return say(ped, ctx, offset, vol, a, b, c);
    bool liked = *(volatile BYTE*)0x9691B1 != 0;                              // o pedestre elogiou (1) ou criticou (0)
    const int* tries = nullptr; const char* tone = "";
    static const int kYesGood[] = { PC_AGREE_GOOD, PC_STATE_GOOD, PC_AGREE_BAD, 0 };
    static const int kYesBad[]  = { PC_AGREE_BAD, PC_AGREE_GOOD, PC_STATE_BAD, 0 };
    static const int kDismiss[] = { PC_DISMISS, PC_ANS_NO, PC_STATE_BAD, 0 };
    static const int kNo[]      = { PC_ANS_NO, PC_DISMISS, PC_STATE_BAD, 0 };
    // recrutados: MOVE_IN 0x90, FIGHT 0x59 e ATTACK_PLAYER 0x14 (gangue), GREET_MALE 0xA6, PART_MALE 0xA8, QUESTION 0xA9
    static const int kJoin[]    = { 0xA6, 0xA9, 0xA5, PC_STATE_GOOD, 0 };
    static const int kAttack[]  = { 0x59, 0x14, 0x90, PC_STATE_BAD, 0 };
    static const int kDisband[] = { 0xA8, PC_DISMISS, 0xA7, 0 };
    static const int kFollow[]  = { 0x90, 0xA6, PC_STATE_GOOD, PC_AGREE_GOOD, 0 };
    static const int kKeepUp[]  = { 0x90, PC_STATE_BAD, 0xA9, 0 };
    static const int kWait[]    = { PC_STATE_GOOD, PC_AGREE_GOOD, PC_AGREE_BAD, 0 };
    // namorada: GREET_FEM 0xA5, PART_FEM 0xA7
    static const int kListen[]  = { PC_AGREE_GOOD, PC_STATE_GOOD, PC_AGREE_BAD, 0 };
    static const int kHiGf[]    = { 0xA5, 0xA6, PC_STATE_GOOD, PC_AGREE_GOOD, 0 };
    static const int kByeGf[]   = { 0xA7, 0xA8, PC_AGREE_GOOD, 0 };
    switch (k) {
    case 0x81: case 0x82: case 0x86: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0xEA: case 0xEC: case 0x6B:
        if (liked || k == 0xEC || k == 0x6B) { tries = kYesGood; tone = "sim, amigavel"; } else { tries = kYesBad; tone = "sim, a contragosto"; }
        break;
    case 0x83: case 0x84: tries = kDismiss; tone = "nao, de pouco caso"; break;
    case 0x87: case 0x88: case 0xEB: case 0x6A: tries = kNo; tone = "nao, irritado"; break;
    // ordens aos recrutados (CPlayerPed: 0x5F7D82, 0x60A0E1, 0x60A424, 0x60C920, 0x60F7E4)
    case 0x7F: tries = kJoin; tone = "chamar para o grupo"; break;
    case 0x93: case 0x94: tries = kAttack; tone = "mandar atacar"; break;
    case 0x95: case 0x96: tries = kDisband; tone = "dispensar"; break;
    case 0x97: case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: tries = kFollow; tone = "mandar seguir"; break;
    case 0x9D: case 0x9E: tries = kKeepUp; tone = "apressar"; break;
    case 0x9F: case 0xA0: tries = kWait; tone = "mandar esperar"; break;
    case 0xF3: tries = kDisband; tone = "dispensar o grupo"; break;
    // namorada e encontros (gf_date, gf_meeting e telefone, pelos comandos de fala dos scripts)
    case 0xF2: tries = kListen; tone = "ouvindo a namorada"; break;                           // UH_HUH
    case 0xF0: tries = kHiGf; tone = "cumprimentar a namorada"; break;                        // GREETING_GFRIEND
    case 0xF1: tries = kByeGf; tone = "despedir da namorada"; break;                          // PARTING_GFRIEND
    case 0xF4: case 0xF5: case 0xF6: case 0x09: case 0x0A: case 0x2E: case 0xF9: case 0x8F:
        tries = kYesGood; tone = "sim, amigavel"; break;                                      // aceitar convite, cafe, numero...
    case 0x8D: tries = kYesBad; tone = "talvez"; break;                                       // MEET_GFRIEND_AGAIN_MAYBE
    case 0x2F: case 0xFA: case 0x8E: tries = kNo; tone = "nao"; break;                       // recusar cafe, numero, encontro
    default: return say(ped, ctx, offset, vol, a, b, c);
    }
    for (int i = 0; tries[i]; i++) {
        short r = say(ped, tries[i], offset, vol, a, b, c);
        if (r >= 0) { Log("conversa: a skin fala (%s): fala 0x%X no lugar da 0x%X do CJ", tone, tries[i], k); return r; }
    }
    char tl[64]; int o = 0; tl[0] = 0;
    for (int i = 0; tries[i] && o < 56; i++) o += snprintf(tl + o, sizeof tl - o, " 0x%X", tries[i]);
    Log("conversa: a voz da skin nao tocou nenhuma fala (%s, 0x%X; tentou%s; %s)", tone, k, tl,
        Rd<BYTE>(ped + 0x294, 0x98) ? "ja estava falando" : "a voz nao tem essas falas ou repetiu ha pouco");
    return -1;
}
static void ApplyConvSay() {
    static const DWORD sites[] = { 0x43B443, 0x43B472, 0x43B4D2, 0x43B50C, 0x43B58D, 0x43B5DF, 0x43B66C, 0x43B6B4, 0x43B714,
        0x43B74E, 0x43B7AD, 0x43B7E7, 0x43B84C, 0x43B886, 0x43B8DE, 0x43B91C, 0x43B98F, 0x43BA2E, 0x43BB0C, 0x43BC00,
        0x43BD06, 0x43BE4A, 0x43BEC8, 0x43BEF6, 0x43BF11, 0x43BF4C, 0x43C0C7,
        0x5F7D82, 0x60A0E1, 0x60A424, 0x60C920, 0x60F7E4,                   // ordens aos recrutados
        0x4774CD, 0x47AB9F, 0x479361, 0x48E475, 0x682E6D };                 // falas pedidas por script (namorada etc.)
    int ok = 0, n = (int)(sizeof sites / sizeof sites[0]);
    for (int i = 0; i < n; i++) {
        BYTE exp[5]; exp[0] = 0xE8; *(int*)(exp + 1) = (int)(0x5EFFE0 - (sites[i] + 5));
        BYTE rp[5];  rp[0] = 0xE8;  *(int*)(rp + 1) = (int)((DWORD)&ConvSay - (sites[i] + 5));
        if (memcmp((void*)sites[i], exp, 5) != 0) continue;
        DWORD old; VirtualProtect((void*)sites[i], 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void*)sites[i], rp, 5);
        VirtualProtect((void*)sites[i], 5, old, &old);
        ok++;
    }
    Log("conversa: voz da skin na conversa, nas ordens aos recrutados e nas falas de script em %d de %d chamadas", ok, n);
}
// ---------------------------------------------------------------- Vigilante ([Vigilante], 3.34)
// Na missao Vigilante (script 'copcar'):
//  - a policia nao persegue o CJ (nivel de procurado zerado e SET_POLICE_IGNORE_PLAYER enquanto durar);
//  - tempo fora da viatura: o script da 60 s (56@ = 60000 em dois lugares do codigo da missao); troco por OutOfCarSeconds
//    no codigo da missao ja carregado (ele e recarregado a cada vez que a missao comeca);
//  - os suspeitos sao os personagens que a missao cria (tipo MISSION1, 24, criados por script); policiais da rua
//    perto deles (HelpRadius) recebem a tarefa de matar o suspeito mais perto (a pe) ou de perseguir o carro dele;
//  - Sim: reforco. Duas viaturas, cada uma com seu tempo de espera: em uso ate a perseguicao em que ela foi
//    chamada acabar (todos os suspeitos daquela leva mortos ou a missao acabou), depois BackupCooldown s.
//    A primeira de uma perseguicao vem com 2 policiais de pistola; a segunda na mesma perseguicao, com 4 de MP5.
//  - Nao: uma viatura com um policial so, que para perto, desce e deixa a viatura para o CJ (SingleCooldown s).
//  Os policiais do reforco sao policiais de verdade do jogo (CREATE_CHAR tipo COP, CCopPed): a policia nao briga
//  com eles. Modelo: FemaleModel em FemaleChance% das vezes (se o jogo tiver esse modelo), senao revezando o
//  policial da regiao e os de Models.
// Depois do nivel 12 do Vigilante (estatistica 157, maior nivel), fora da missao: de 1 a MaxStars estrelas, o nivel
// cai mais rapido quando o jogo ja esta baixando (sem policial a 18 m) e nao ha policial a CopRadius m: cada
// estrela inteira leva StarSeconds s na cidade (no interior, metade, como no jogo). O jogo baixa 1 ponto de "caos"
// por segundo (2 no interior); a faixa de cada estrela tem 130, 370, 650 e 1200 pontos (CWanted::UpdateWantedLevel).
static int   g_vgOn = 1, g_vgNoChase = 1, g_vgOutSec = 120, g_vgHelpOn = 1, g_vgPerk = 1, g_vgMaxStars = 4;
static float g_vgHelpR = 100.f, g_vgCool = 60.f, g_vgOneCool = 60.f, g_vgStarSec = 65.f, g_vgCopR = 60.f, g_vgStar64 = 32.f;
static float g_vgSpawnMin = 30.f, g_vgSpawnMax = 70.f;
static int   g_vgHealth = 300, g_vgArmour = 100, g_vgBonusEvery = 3, g_vgNoAim = 1;
static char  g_vgModels[96] = "dsher,csher";

struct VgUnit {
    bool  on, squad, single, dbNoted;
    int   car[2], blip[2], ncar, cph[2], tgt[2];                               // cph: 0 dirigindo, 1 a pe, 2 voltando a viatura
    int   ped[4], weap[4], model[4], pcar[4], seat[4], n;                      // seat -1: motorista
    int   phase, target, slot;
    DWORD t0, tCmd, tDB, tFar, tStuck, tResp, tCar[2], tBoard[2];
};
struct VgSlot { int state, chase; DWORD coolUntil; };                        // 0 livre, 1 em uso, 2 esperando
struct VgAssign { int ped, target; DWORD t; bool drive; };
static VgUnit   g_vu[3];                                                     // 0 e 1: reforco; 2: viatura para o CJ
static VgSlot   g_vs[2];
static VgAssign g_va[32];
static struct {
    bool active, ignoreSet, inChase; char* script; int chase, nSus, nFreed, freed[16], lastLvl, nNoAim, noAim[128]; WORD ynWas; DWORD tNoSus, tHelp, oneUntil, tCops;
    bool havePrev, riseUsed; int prevTimer, lastRise, pendLvl, lvlSeen; DWORD tRise, tLvl;   // bonus dobrado (3.40)
    bool timerNoted; DWORD tTimer;                                                            // nivel 32 sem timer (3.41)
} g_vg;
static int g_vgPrevChaos = -1;

typedef void* (__cdecl* GetMIByName_t)(const char*, int*);
typedef void  (__thiscall* UpdWanted_t)(void*);
static int ModelByName(const char* name) {
    if (!name || !*name) return -1;
    int idx = -1;
    void* mi = ((GetMIByName_t)0x4C5940)(name, &idx);                         // CModelInfo::GetModelInfo(name, &index)
    return mi ? idx : -1;
}
static bool VgSuspect(const char* p) {
    return p && Rd<int>(p, 0x598) == 24 && Rd<BYTE>(p, 0x484) == 2 && Rd<float>(p, 0x540) > 0.f;   // MISSION1, script
}
static char* VgCarOf(const char* p) { return (p && (Rd<BYTE>(p, 0x46D) & 1)) ? Rd<char*>(p, 0x58C) : nullptr; }
static float VgSpeed(const char* e) { const float* v = (const float*)(e + 0x44); return sqrtf(v[0] * v[0] + v[1] * v[1]) * 50.f; }
static bool VgOurs(const char* p) {
    int r = PedRef(p);
    for (auto& u : g_vu) if (u.on) for (int i = 0; i < u.n; i++) if (u.ped[i] == r) return true;
    return false;
}
template <class F> static void ForEachPed(F f) {
    char* pool = *(char**)0xB74490;
    if (!pool) return;
    int size = Rd<int>(pool, 8);
    for (int i = 0; i < size; i++) {
        if (Rd<BYTE*>(pool, 4)[i] & 0x80) continue;
        f(Rd<char*>(pool, 0) + i * 0x7C4);
    }
}
static char* VgNearestSuspect(V3 from, float maxD = 1e9f) {
    char* best = nullptr; float bd = maxD;
    ForEachPed([&](char* p) { if (VgSuspect(p)) { float d = Dist2(EntityPos(p), from); if (d < bd) { bd = d; best = p; } } });
    return best;
}
static bool CopNear(V3 at, float r) {
    bool found = false;
    ForEachPed([&](char* p) { if (!found && Rd<int>(p, 0x598) == 6 && Rd<float>(p, 0x540) > 0.f && Dist2(EntityPos(p), at) < r) found = true; });
    return found;
}
// rua perto do CJ para a viatura nascer: de SpawnMin a SpawnMax, fora da tela; sem isso, mais longe; por fim, na tela
static bool VgSpawnNode(V3 pp, V3* out, float* hd, const V3* avoid = nullptr) {
    bool gotOn = false; V3 on = {}; float onHd = 0.f;
    for (int pass = 0; pass < 2; pass++) {
        float dmin = pass ? g_vgSpawnMax : g_vgSpawnMin, dmax = pass ? g_vgSpawnMax * 2.f : g_vgSpawnMax;
        for (int n = 1; n <= 60; n++) {
            int o[4] = {};
            Cmd c(0x06F8); c.F(pp.x).F(pp.y).F(pp.z).I(n).Out(0).Out(1).Out(2).Out(3); RunCmd(c, o, 4);
            V3 q = { AsF(o[0]), AsF(o[1]), AsF(o[2]) };
            if (q.x == 0.f && q.y == 0.f) continue;
            float d = Dist2(q, pp);
            if (d < dmin || d > dmax || fabsf(q.z - pp.z) > 8.f) continue;
            if (avoid && Dist2(q, *avoid) < 10.f) continue;
            bool seen = false;
            { Cmd s2(0x00C2); s2.F(q.x).F(q.y).F(q.z + 1.f).F(4.f); seen = RunCmd(s2); }
            if (!seen) { *out = q; *hd = AsF(o[3]); return true; }
            if (!gotOn) { on = q; onHd = AsF(o[3]); gotOn = true; }
        }
    }
    if (gotOn) { *out = on; *hd = onHd; return true; }
    return false;
}
static int g_fcChanceV = 40;
static int VgPickModel(int zoneCop, int femModel) {
    static int turn = 0;
    if (femModel > 0 && (rand() % 100) < g_fcChanceV) return femModel;
    int list[8]; int n = 0; list[n++] = zoneCop;
    char buf[96]; strncpy(buf, g_vgModels, sizeof buf - 1); buf[sizeof buf - 1] = 0;
    for (char* t = strtok(buf, ", ;"); t && n < 8; t = strtok(nullptr, ", ;")) { int m = ModelByName(t); if (m > 0 && m != zoneCop) list[n++] = m; }
    return list[(turn++) % n];
}
static bool VgCarUsable(int ref) {
    char* car = VehByRef(ref);
    if (!car) return false;
    { Cmd c(0x02BF); c.I(ref); if (RunCmd(c)) return false; }                  // na agua
    { Cmd c(0x01F4); c.I(ref); if (RunCmd(c)) return false; }                  // de rodas para cima
    int h = 1000; { Cmd c(0x0227); c.I(ref).Out(0); RunCmd(c, &h, 1); }
    return h > 300;
}
// policial do reforco: arma na mao, triplo de vida, colete, sem morte de um tiro so, e a mira do CJ nao trava nele
static void VgSetupCop(int o, int w) {
    { Cmd c(0x01B2); c.I(o).I(w).I(9999); RunCmd(c); }
    { Cmd c(0x01B9); c.I(o).I(w); RunCmd(c); }
    { Cmd c(0x08AF); c.I(o).I(g_vgHealth); RunCmd(c); }
    { Cmd c(0x0223); c.I(o).I(g_vgHealth); RunCmd(c); }
    if (g_vgArmour > 0) { Cmd c(0x035F); c.I(o).I(g_vgArmour); RunCmd(c); }
    { Cmd c(0x0446); c.I(o).I(0); RunCmd(c); }
    if (g_vgNoAim) { Cmd c(0x0568); c.I(o).I(1); RunCmd(c); }
}
static void VgDropBlips(VgUnit& u) {
    for (int c = 0; c < 2; c++) if (u.blip[c] >= 0) { Cmd x(0x0164); x.I(u.blip[c]); RunCmd(x); u.blip[c] = -1; }
}
static void VgReleaseUnit(VgUnit& u, bool driveOff) {
    if (!u.on) return;
    VgDropBlips(u);
    for (int c = 0; c < u.ncar; c++) {
        char* car = VehByRef(u.car[c]);
        if (!car) continue;
        { Cmd x(0x0397); x.I(u.car[c]).I(0); RunCmd(x); }
        char* drv = Rd<char*>(car, 0x460);
        if (driveOff && drv && VgOurs(drv)) {
            { Cmd x(0x00AD); x.I(u.car[c]).F(15.f); RunCmd(x); }
            { Cmd x(0x00AE); x.I(u.car[c]).I(0); RunCmd(x); }
            { Cmd x(0x00A8); x.I(u.car[c]); RunCmd(x); }
        }
    }
    for (int i = 0; i < u.n; i++) if (PedByRef(u.ped[i])) {
        { Cmd x(0x0568); x.I(u.ped[i]).I(0); RunCmd(x); }
        { Cmd x(0x01C2); x.I(u.ped[i]); RunCmd(x); }
    }
    for (int c = 0; c < u.ncar; c++) if (VehByRef(u.car[c])) { Cmd x(0x01C3); x.I(u.car[c]); RunCmd(x); }
    u.on = false;
}
// cria as viaturas e os policiais de u (u.n, u.model e u.weap ja definidos) perto de 'nearPt'.
// Viatura de 2 lugares (Police Ranger no interior): vem uma segunda viatura com os outros policiais.
static bool VgBuild(VgUnit& u, V3 nearPt) {
    int level = 1;
    { Cmd c(0x07EF); c.F(nearPt.x).F(nearPt.y).F(nearPt.z).Out(0); RunCmd(c, &level, 1); }
    static const int kCar[4] = { 599, 596, 597, 598 }, kPed[4] = { 283, 280, 281, 282 };
    if (level < 0 || level > 3) level = 1;
    int carM = kCar[level], zoneCop = kPed[level];
    RequestModel(carM, 2);
    for (int i = 0; i < u.n; i++) RequestModel(u.model[i], 2);
    RequestModel(zoneCop, 2);
    RequestModel(346, 2); RequestModel(348, 2); RequestModel(349, 2); RequestModel(353, 2);   // pistola, Desert Eagle, escopeta, MP5
    LoadAllRequestedModels(false);
    for (int i = 0; i < u.n; i++) if (!ModelLoaded(u.model[i])) u.model[i] = zoneCop;
    if (!ModelLoaded(carM) || !ModelLoaded(zoneCop)) { Log("vigilante: modelos da viatura nao carregaram"); return false; }
    int nCops = u.n; u.n = 0; u.ncar = 0;
    int mdl[4], wp[4];
    memcpy(mdl, u.model, sizeof mdl); memcpy(wp, u.weap, sizeof wp);
    V3 first = {}; bool haveFirst = false; float firstHd = 0.f;
    int next = 0;
    while (next < nCops && u.ncar < 2) {
        V3 n; float hd = 0.f;
        if (!VgSpawnNode(nearPt, &n, &hd, haveFirst ? &first : nullptr)) {
            if (!haveFirst) { Log("vigilante: nao achei rua perto para a viatura"); break; }
            float r = firstHd * 3.14159265f / 180.f;                           // 3.41: a segunda vem logo atras da primeira
            n = { first.x + sinf(r) * 9.f, first.y - cosf(r) * 9.f, first.z }; hd = firstHd;
            Log("vigilante: sem outra rua; a segunda viatura nasce atras da primeira");
        }
        int cr = -1;
        { Cmd c(0x00A5); c.I(carM).F(n.x).F(n.y).F(n.z).Out(0); RunCmd(c, &cr, 1); }
        char* car = VehByRef(cr);
        if (!car) break;
        { Cmd c(0x0175); c.I(cr).F(hd); RunCmd(c); }
        int maxp = 3;
        { Cmd c(0x01EA); c.I(cr).Out(0); RunCmd(c, &maxp, 1); }
        if (maxp < 0) maxp = 0;
        if (maxp > 3) maxp = 3;
        int ci = u.ncar++;
        u.car[ci] = cr; u.cph[ci] = 0; u.tgt[ci] = -1; u.tCar[ci] = 0; u.blip[ci] = -1;
        for (int s = -1; s < maxp && next < nCops; s++, next++) {
            int o = -1;
            if (s < 0) { Cmd c(0x0129); c.I(cr).I(6).I(mdl[next]).Out(0); RunCmd(c, &o, 1); }
            else       { Cmd c(0x01C8); c.I(cr).I(6).I(mdl[next]).I(s).Out(0); RunCmd(c, &o, 1); }
            if (!PedByRef(o)) continue;
            VgSetupCop(o, wp[next]);
            u.ped[u.n] = o; u.weap[u.n] = wp[next]; u.model[u.n] = mdl[next]; u.pcar[u.n] = ci; u.seat[u.n] = s; u.n++;
        }
        { Cmd c(0x0397); c.I(cr).I(1); RunCmd(c); }                           // sirene
        if (u.squad) {
            { Cmd c(0x02AC); c.I(cr).I(1).I(0).I(0).I(0).I(0); RunCmd(c); }   // a prova de bala
            { Cmd c(0x0229); c.I(cr).I(0).I(0); RunCmd(c); }                   // toda preta
        }
        { Cmd c(0x0186); c.I(cr).Out(0); RunCmd(c, &u.blip[ci], 1); }
        if (u.blip[ci] >= 0) { Cmd c(0x0165); c.I(u.blip[ci]).I(2); RunCmd(c); }   // azul
        if (!haveFirst) { first = n; firstHd = hd; haveFirst = true; }
        Log("vigilante: viatura %d (%d lugares) nasceu a %.0f m", carM, maxp + 1, Dist2(n, nearPt));
    }
    { Cmd c(0x0249); c.I(carM); RunCmd(c); }
    for (int i = 0; i < 4; i++) { Cmd c(0x0249); c.I(mdl[i]); RunCmd(c); }
    if (!u.n) {
        for (int c = 0; c < u.ncar; c++) { Cmd x(0x00A6); x.I(u.car[c]); RunCmd(x); }
        VgDropBlips(u); u.ncar = 0;
        return false;
    }
    return true;
}
static bool VgSpawn(VgUnit& u, int nCops, bool squad, bool single) {
    char* me = PlayerPed();
    if (!me) return false;
    V3 pp = EntityPos(me);
    int level = 1;
    { Cmd c(0x07EF); c.F(pp.x).F(pp.y).F(pp.z).Out(0); RunCmd(c, &level, 1); }
    static const int kPed[4] = { 283, 280, 281, 282 };
    int zoneCop = kPed[(level < 0 || level > 3) ? 1 : level];
    int fem = FemCopModel((level < 0 || level > 3) ? 1 : level);              // policial feminina da regiao (3.41)
    static int femNoted = 0;
    if (femNoted != fem) { femNoted = fem; Log("vigilante: policial feminina da regiao %s", fem > 0 ? "no revezamento" : "nao existe neste jogo; fica so o revezamento"); }
    memset(&u, 0, sizeof u); u.blip[0] = u.blip[1] = -1; u.target = -1;
    u.squad = squad; u.single = single; u.n = nCops;
    // pesado: todos de MP5. Normal: motorista de pistola, carona da frente de escopeta, os de tras sorteados
    static const int kRear[4] = { 29, 25, 22, 24 };                           // MP5, escopeta, pistola, Desert Eagle
    for (int i = 0; i < nCops; i++) {
        u.model[i] = VgPickModel(zoneCop, fem);
        u.weap[i] = squad ? 29 : single ? 22 : i == 0 ? 22 : i == 1 ? 25 : kRear[rand() % 4];
    }
    if (!VgBuild(u, pp)) return false;
    u.on = true; u.t0 = u.tCmd = GameMs(); u.phase = 0;
    char ml[80]; int o = 0; ml[0] = 0;
    for (int i = 0; i < u.n; i++) o += snprintf(ml + o, sizeof ml - o, "%s%d/%d", i ? "," : "", u.model[i], u.weap[i]);
    Log("vigilante: %s com %d policiais em %d viatura(s) (modelo/arma %s%s)", single ? "viatura para voce" : squad ? "reforco pesado" : "reforco",
        u.n, u.ncar, ml, squad ? ", viatura preta a prova de bala" : "");
    return true;
}
// sempre de arma na mao: o policial recebe a arma (se nao tiver) e ela vira a atual antes de cada ordem de atacar
static void VgArm(int ped, int w) {
    { Cmd c(0x01B2); c.I(ped).I(w).I(200); RunCmd(c); }
    { Cmd c(0x01B9); c.I(ped).I(w); RunCmd(c); }
}
static void VgKill(int ped, int target, int w = 22) { VgArm(ped, w); Cmd c(0x05E2); c.I(ped).I(target); RunCmd(c); }
static bool VgAlive(int ref) { char* p = PedByRef(ref); return p && Rd<float>(p, 0x540) > 0.f; }
// reforco perdido (longe dos suspeitos, preso, na agua, sem viatura): some e volta perto do suspeito, nas viaturas
static void VgRespawn(VgUnit& u, V3 nearPt, const char* why) {
    VgUnit nu = u;
    int k = 0;
    for (int i = 0; i < u.n; i++) if (VgAlive(u.ped[i])) { nu.model[k] = u.model[i]; nu.weap[k] = u.weap[i]; k++; }
    VgDropBlips(u);
    for (int i = 0; i < u.n; i++) if (PedByRef(u.ped[i])) { Cmd x(0x009B); x.I(u.ped[i]); RunCmd(x); }
    for (int c = 0; c < u.ncar; c++) if (VehByRef(u.car[c])) { Cmd x(0x00A6); x.I(u.car[c]); RunCmd(x); }
    nu.n = k; nu.dbNoted = false;
    if (!k || !VgBuild(nu, nearPt)) { u.on = false; Log("vigilante: reforco perdido (%s) e nao deu para trazer de volta", why); return; }
    DWORD now = GameMs();
    nu.on = true; nu.tResp = now; nu.tFar = 0; nu.tStuck = 0; nu.t0 = now;
    u = nu;
    Log("vigilante: reforco perdido (%s); %d policiais voltaram em %d viatura(s) perto do suspeito", why, u.n, u.ncar);
}
static void VgSingleTick(VgUnit& u, V3 pp, DWORD now) {
    char* car = VehByRef(u.car[0]);
    if (!car) { VgReleaseUnit(u, false); return; }
    char* drv = u.n ? PedByRef(u.ped[0]) : nullptr;
    bool drvIn = drv && Rd<float>(drv, 0x540) > 0.f && VgCarOf(drv) == car;
    float d = Dist2(EntityPos(car), pp);
    if (u.phase == 0) {
        if (!drvIn) { u.phase = 2; return; }
        if (d < 14.f) { Cmd c(0x00AD); c.I(u.car[0]).F(0.f); RunCmd(c); u.phase = 1; u.tCmd = now; return; }
        if (now - u.tCmd > 2000 || u.tCmd == u.t0) {
            u.tCmd = now;
            { Cmd c(0x00AD); c.I(u.car[0]).F(25.f); RunCmd(c); }
            { Cmd c(0x00AE); c.I(u.car[0]).I(2); RunCmd(c); }
            { Cmd c(0x00A7); c.I(u.car[0]).F(pp.x).F(pp.y).F(pp.z); RunCmd(c); }
        }
    } else if (u.phase == 1) {
        if (VgSpeed(car) < 1.f || now - u.tCmd > 4000) {
            { Cmd c(0x0397); c.I(u.car[0]).I(0); RunCmd(c); }
            { Cmd c(0x020A); c.I(u.car[0]).I(1); RunCmd(c); }                // destrancada
            if (drv) { Cmd c(0x0633); c.I(u.ped[0]); RunCmd(c); }
            u.phase = 2; u.tCmd = now;
            Log("vigilante: a viatura para voce chegou a %.0f m; o policial desce", d);
        }
    } else if (u.phase == 2) {
        if (drv && !VgCarOf(drv) && now - u.tCmd > 500) {
            { Cmd c(0x05DE); c.I(u.ped[0]); RunCmd(c); }                       // anda pela rua
            { Cmd c(0x01C2); c.I(u.ped[0]); RunCmd(c); }
            u.n = 0; u.phase = 3;
        } else if (!drv) { u.n = 0; u.phase = 3; }
    }
    if (u.phase == 3 && (PlayerVehicle() == car || d > 150.f)) {
        VgDropBlips(u);
        { Cmd c(0x01C3); c.I(u.car[0]); RunCmd(c); }
        u.on = false;
        Log("vigilante: viatura entregue (%s)", PlayerVehicle() == car ? "voce entrou" : "voce se afastou");
    }
}
static void VgUnitTick(VgUnit& u, V3 pp, DWORD now) {
    if (!u.on) return;
    if (u.single) { VgSingleTick(u, pp, now); return; }
    int alive = 0; V3 at = {};
    for (int i = 0; i < u.n; i++) if (VgAlive(u.ped[i])) { if (!alive) at = EntityPos(PedByRef(u.ped[i])); alive++; }
    if (!alive) { VgReleaseUnit(u, false); return; }
    char* t = VgNearestSuspect(at);
    if (!t) return;                                                          // espera a proxima leva
    int tr = PedRef(t);
    char* tcar = VgCarOf(t);
    V3 tp = EntityPos(t);
    float dU = 1e9f, dJ = 1e9f;                                               // policial mais perto do suspeito / do CJ
    for (int i = 0; i < u.n; i++) if (VgAlive(u.ped[i])) {
        V3 q = EntityPos(PedByRef(u.ped[i]));
        float a = Dist2(q, tp), b = Dist2(q, pp);
        if (a < dU) dU = a;
        if (b < dJ) dJ = b;
    }
    bool anyUsable = false, stuck = false;
    for (int c = 0; c < u.ncar; c++) {
        char* car = VehByRef(u.car[c]);
        bool usable = VgCarUsable(u.car[c]);
        if (usable) anyUsable = true;
        int di = -1, members = 0;
        for (int i = 0; i < u.n; i++) if (u.pcar[i] == c && VgAlive(u.ped[i])) { members++; if (u.seat[i] < 0) di = i; }
        if (!members) continue;
        if (di < 0) {                                                        // motorista morreu: outro da viatura assume
            for (int i = 0; i < u.n; i++) if (u.pcar[i] == c && VgAlive(u.ped[i])) { u.seat[i] = -1; di = i; break; }
        }
        char* drv = PedByRef(u.ped[di]);
        bool drvIn = usable && VgCarOf(drv) == car;
        V3 cpos = car ? EntityPos(car) : EntityPos(drv);
        float d = Dist2(cpos, tp);
        if (u.cph[c] == 0) {
            if (!drvIn) { u.cph[c] = 1; u.tCar[c] = 0; continue; }
            if (VgSpeed(car) < 1.f && d > 30.f) { if (!u.tStuck) u.tStuck = now; else if (now - u.tStuck > 8000) stuck = true; }
            else u.tStuck = 0;
            // caronas atiram de dentro da viatura (o motorista so dirige), cada um com a propria arma
            if (d < 60.f && now - u.tDB > 3000) {
                int shooting = 0;
                for (int i = 0; i < u.n; i++) {
                    if (u.pcar[i] != c || u.seat[i] < 0 || !VgAlive(u.ped[i]) || VgCarOf(PedByRef(u.ped[i])) != car) continue;
                    VgArm(u.ped[i], u.weap[i] ? u.weap[i] : 22);
                    Cmd x(0x0713); x.I(u.ped[i]).I(tr).I(-1).F(0.f).F(0.f).F(0.f).F(60.f).I(8).I(u.seat[i] == 1 ? 0 : 1).I(60); RunCmd(x);
                    shooting++;
                }
                if (shooting && !u.dbNoted) { u.dbNoted = true; Log("vigilante: %d policiais atirando de dentro da viatura (%.0f m do suspeito)", shooting, d); }
            }
            bool arrive = d < (tcar ? 18.f : 25.f) && (!tcar || VgSpeed(tcar) < 4.f);
            if (arrive) { u.cph[c] = 1; u.tCar[c] = 0; Log("vigilante: reforco chegou (%.0f m do suspeito); policiais descem", d); continue; }
            if (tr != u.tgt[c] || now - u.tCar[c] > 2500) {
                u.tgt[c] = tr; u.tCar[c] = now;
                if (tcar) { Cmd x(0x06E1); x.I(u.ped[di]).I(u.car[c]).I(VehRef(tcar)).I(15).F(40.f).I(2); RunCmd(x); }
                else {
                    { Cmd x(0x00AD); x.I(u.car[c]).F(35.f); RunCmd(x); }
                    { Cmd x(0x00AE); x.I(u.car[c]).I(2); RunCmd(x); }
                    { Cmd x(0x00A7); x.I(u.car[c]).F(tp.x).F(tp.y).F(tp.z); RunCmd(x); }
                }
            }
        } else if (u.cph[c] == 1) {
            // a pe; se o suspeito fugiu de carro e a viatura esta inteira e perto, todos voltam para ela
            float nearPt = car ? Dist2(EntityPos(drv), cpos) : 1e9f;
            if (tcar && d > 50.f && usable && nearPt < 45.f) {
                for (int i = 0; i < u.n; i++) {
                    if (u.pcar[i] != c || !VgAlive(u.ped[i])) continue;
                    if (u.seat[i] < 0) { Cmd x(0x05CB); x.I(u.ped[i]).I(u.car[c]).I(15000); RunCmd(x); }
                    else { Cmd x(0x05CA); x.I(u.ped[i]).I(u.car[c]).I(15000).I(u.seat[i]); RunCmd(x); }
                }
                u.cph[c] = 2; u.tBoard[c] = now;
                Log("vigilante: suspeito fugiu de carro (%.0f m); policiais voltam para a viatura", d);
                continue;
            }
            if (now - u.tCar[c] > 3000 || tr != u.tgt[c]) {
                u.tCar[c] = now; u.tgt[c] = tr;
                for (int i = 0; i < u.n; i++) {
                    if (u.pcar[i] != c || !VgAlive(u.ped[i])) continue;
                    char* pt = VgNearestSuspect(EntityPos(PedByRef(u.ped[i])));
                    if (pt) VgKill(u.ped[i], PedRef(pt), u.weap[i] ? u.weap[i] : 22);
                }
            }
        } else if (u.cph[c] == 2) {
            bool allIn = true;
            for (int i = 0; i < u.n; i++) if (u.pcar[i] == c && VgAlive(u.ped[i]) && VgCarOf(PedByRef(u.ped[i])) != car) allIn = false;
            if (drvIn && (allIn || now - u.tBoard[c] > 10000)) { u.cph[c] = 0; u.tgt[c] = -1; u.tDB = 0; }
            else if (now - u.tBoard[c] > 20000 || !usable) { u.cph[c] = 1; u.tCar[c] = 0; }
        }
    }
    if (u.ncar) u.tDB = (now - u.tDB > 3000) ? now : u.tDB;
    // perdido: longe do suspeito, preso, ou a pe sem viatura e o suspeito fugindo de carro; volta se o CJ estiver longe
    const char* why = nullptr;
    if (dU > 120.f) why = "longe do suspeito";
    else if (stuck) why = "viatura presa";
    else if (!anyUsable && tcar && dU > 50.f) why = "sem viatura e o suspeito fugindo de carro";
    if (!why) { u.tFar = 0; return; }
    if (!u.tFar) u.tFar = now;
    if (now - u.tFar > 5000 && dJ > 80.f && now - u.tResp > 15000) VgRespawn(u, tp, why);
}
// policiais da rua: matam o suspeito mais perto (ou perseguem o carro dele, se estao dirigindo)
static char* TgNearest(V3 from, float maxD, bool attackers);
static void VgStreetCops(DWORD now, bool targets = false) {
    int issued = 0;
    ForEachPed([&](char* p) {
        if (issued >= 6 || Rd<int>(p, 0x598) != 6 || Rd<float>(p, 0x540) <= 0.f || Rd<BYTE>(p, 0x484) != 1 || VgOurs(p)) return;
        V3 cp = EntityPos(p);
        char* t = targets ? TgNearest(cp, g_vgHelpR, false) : VgNearestSuspect(cp, g_vgHelpR);
        if (!t) return;
        int pr = PedRef(p), tr = PedRef(t);
        VgAssign* a = nullptr;
        for (auto& x : g_va) if (x.ped == pr && x.ped) { a = &x; break; }
        char* mycar = VgCarOf(p); char* tcar = VgCarOf(t);
        bool drive = mycar && tcar && Rd<char*>(mycar, 0x460) == p;
        // dirigindo, a ordem e renovada a cada 2 s: se a viatura bate num civil, o jogo poe o policial atras dele
        if (a && a->target == tr && a->drive == drive && now - a->t < (drive ? 2000u : 6000u)) return;
        if (!a) { for (auto& x : g_va) if (!x.ped || !PedByRef(x.ped) || now - x.t > 30000) { a = &x; break; } }
        if (!a) return;
        bool bikeCop = mycar && Rd<char*>(mycar, 0x460) == p && Rd<int>(mycar, 0x594) == 9;
        if (drive) {
            int cr = VehRef(mycar);
            { Cmd c(0x0397); c.I(cr).I(1); RunCmd(c); }                         // sirene
            { Cmd c(0x06E1); c.I(pr).I(cr).I(VehRef(tcar)).I(15).F(40.f).I(2); RunCmd(c); }
        } else if (bikeCop && g_vgBikeShoot && Dist2(cp, EntityPos(t)) < 35.f) {   // de moto, suspeito a pe: atira da moto
        } else VgKill(pr, tr, 22);                                           // a pe: sempre pistola
        // de moto e perto (35 m): atira montado, como faz com o CJ procurado (TASK_DRIVE_BY 0713 com pistola)
        if (bikeCop && g_vgBikeShoot && Dist2(cp, EntityPos(t)) < 35.f) {
            VgArm(pr, 22);
            Cmd x(0x0713); x.I(pr).I(tr).I(-1).F(0.f).F(0.f).F(0.f).F(60.f).I(8).I(0).I(60); RunCmd(x);
        }
        if (!a->ped || a->ped != pr) issued++;
        a->ped = pr; a->target = tr; a->t = now; a->drive = drive;
    });
    if (issued) Log("vigilante: %d policiais da rua foram atras %s", issued, targets ? "dos seus alvos" : "dos suspeitos");
}
static int VgPatchGrace(char* ms) {
    BYTE* base = Rd<BYTE*>(ms, 0x10);                                         // m_pBaseIP: inicio do codigo da missao
    if (!base) return 0;
    static const BYTE A[] = { 0x18, 0x00, 0x02, 0x78, 0x80, 0x01, 0x60, 0xEA, 0x00, 0x00 };   // $8222 > 60000
    static const BYTE B[] = { 0x06, 0x00, 0x03, 0x38, 0x00, 0x01, 0x60, 0xEA, 0x00, 0x00 };   // 56@ = 60000
    int v = g_vgOutSec * 1000, n = 0;
    for (int i = 0; i < 0x6000; i++)
        if (!memcmp(base + i, A, sizeof A) || !memcmp(base + i, B, sizeof B)) { memcpy(base + i + 6, &v, 4); n++; i += 9; }
    return n;
}
// nivel 64: o codigo da missao carregada passa a aceitar qualquer veiculo e a pe. Nos dois pontos que guardam o
// carro do CJ (13987, 19017), IS_CHAR_IN_ANY_POLICE_VEHICLE $3 vira IS_CHAR_IN_ANY_CAR $3; nos dois que contam o
// tempo fora da viatura (14332, 18674), NOT IS_CHAR_IN_ANY_POLICE_VEHICLE $3 vira NOT IS_PLAYER_PLAYING $2 (falso).
static int VgPatch64(char* ms) {
    BYTE* base = Rd<BYTE*>(ms, 0x10);
    if (!base) return 0;
    static const BYTE pos[5] = { 0x6C, 0x05, 0x02, 0x0C, 0x00 }, neg[5] = { 0x6C, 0x85, 0x02, 0x0C, 0x00 };
    static const BYTE anyCar[5] = { 0xDF, 0x00, 0x02, 0x0C, 0x00 }, notPlaying[5] = { 0x56, 0x82, 0x02, 0x08, 0x00 };
    static const int kPos[2] = { 13987, 19017 }, kNeg[2] = { 14332, 18674 };
    int n = 0;
    for (int i = 0; i < 2; i++) {
        if (!memcmp(base + kPos[i], pos, 5)) { memcpy(base + kPos[i], anyCar, 5); n++; }
        if (!memcmp(base + kNeg[i], neg, 5)) { memcpy(base + kNeg[i], notPlaying, 5); n++; }
    }
    return n;
}
static bool VigActiveNow() { return g_vg.active; }
static bool ArmLegalNow() { int L = VigLevel(); return L >= 64 || (L >= 12 && g_vg.active); }
static bool Vg32() { return VigLevel() >= 32; }                                // nivel 32: reforco sem espera (3.43)
static void VgChaseEnd(DWORD now, const char* why) {
    for (int s = 0; s < 2; s++) if (g_vs[s].state == 1) {
        g_vs[s].state = 2; g_vs[s].coolUntil = now + (Vg32() ? 0 : (DWORD)(g_vgCool * 1000.f));
        VgReleaseUnit(g_vu[s], true);
    }
    g_vg.inChase = false; g_vg.nFreed = 0;
    Log("vigilante: fim da perseguicao (%s)", why);
}
static int g_vgMaxSaved = -1;                                                 // CWanted::MaximumWantedLevel 0x8CDEE4
static void VgNeverWanted(bool on) {
    volatile int* mx = (volatile int*)0x8CDEE4;
    if (on) {
        if (g_vgMaxSaved < 0) { g_vgMaxSaved = *mx; Log("vigilante: nunca procurado durante a missao (maximo de estrelas era %d)", g_vgMaxSaved); }
        if (*mx != 0) ((void(__cdecl*)(int))0x561E70)(0);                     // CWanted::SetMaximumWantedLevel
    } else if (g_vgMaxSaved >= 0) {
        int v = g_vgMaxSaved < 0 || g_vgMaxSaved > 6 ? 6 : g_vgMaxSaved;
        ((void(__cdecl*)(int))0x561E70)(v);
        Log("vigilante: maximo de estrelas de volta a %d", v);
        g_vgMaxSaved = -1;
    }
}
static void VgStop() {
    DWORD now = GameMs();
    g_vgRes.backupLeft = 0;
    VgNeverWanted(false);
    VgChaseEnd(now, "missao acabou");
    VgReleaseUnit(g_vu[2], false);
    if (g_vg.ignoreSet) { Cmd c(0x01F7); c.I(0).I(0); RunCmd(c); }
    for (int i = 0; i < g_vg.nNoAim; i++) if (PedByRef(g_vg.noAim[i])) { Cmd c(0x0568); c.I(g_vg.noAim[i]).I(0); RunCmd(c); }
    g_vigYN = false;
    memset(&g_va, 0, sizeof g_va);
    bool keep = true; DWORD ou = g_vg.oneUntil; (void)keep;
    memset(&g_vg, 0, sizeof g_vg); g_vg.oneUntil = ou;
    Log("vigilante: missao terminou");
}
static int VgSlotFree(DWORD now) {
    if (Vg32()) {                                                             // vaga livre = viatura que nao esta mais com voce
        for (int s = 0; s < 2; s++) if (g_vs[s].state != 1 || !g_vu[s].on) return s;
        return -1;
    }
    DWORD maxWait = (DWORD)(g_vgCool * 1000.f) + 5000;
    for (int s = 0; s < 2; s++) {
        if (g_vs[s].state == 2 && (DWORD)(g_vs[s].coolUntil - now) > maxWait && (int)(now - g_vs[s].coolUntil) < 0)
            g_vs[s].state = 0;                                                // o relogio do jogo voltou (jogo carregado)
        if (g_vs[s].state == 0 || (g_vs[s].state == 2 && (int)(now - g_vs[s].coolUntil) >= 0)) return s;
    }
    return -1;
}
// reforco: 2 vagas. Ate o nivel 31: so na perseguicao, a vaga fica em uso ate a perseguicao acabar e depois espera
// Cooldown s; a 2a viatura da mesma perseguicao e a pesada. Nivel 32+: a qualquer hora no Vigilante, sem espera,
// ate 2 viaturas com voce; com uma ja com voce, a outra vem sempre pesada (4 de MP5, preta e blindada).
static bool VgBackupReady(DWORD now) { return g_vg.active && (g_vg.inChase || Vg32()) && VgSlotFree(now) >= 0; }
static void VgOneClockFix(DWORD now) {
    if (g_vg.oneUntil && (DWORD)(g_vg.oneUntil - now) > (DWORD)(g_vgOneCool * 1000.f) + 5000 && (int)(now - g_vg.oneUntil) < 0) g_vg.oneUntil = 0;
}
static bool VgCarReady(DWORD now) { return g_vg.active && !g_vu[2].on && !((int)(now - g_vg.oneUntil) < 0 && g_vg.oneUntil); }
static void VgCallBackup(DWORD now) {
    bool lvl32 = Vg32();
    if (!g_vg.inChase && !lvl32) { GarHelp("Sem suspeitos agora. O reforco vem quando houver perseguicao."); return; }
    int s = VgSlotFree(now);
    if (s < 0) {
        DWORD w = 0xFFFFFFFF; bool busy = false;
        for (auto& x : g_vs) { if (x.state == 1) busy = true; else if (x.state == 2) { DWORD r = x.coolUntil - now; if (r < w) w = r; } }
        if (lvl32 || w == 0xFFFFFFFF) GarHelp(busy ? "As duas viaturas de reforco ja estao com voce." : "Reforco indisponivel.");
        else GarHelp("Reforco indisponivel. Proxima viatura em %u s.", (unsigned)((w + 999) / 1000));
        return;
    }
    bool second = lvl32 ? (g_vs[1 - s].state == 1 && g_vu[1 - s].on)
                        : (g_vs[1 - s].state == 1 && g_vs[1 - s].chase == g_vg.chase);
    VgReleaseUnit(g_vu[s], true);
    if (VgSpawn(g_vu[s], 4, second, false)) {
        g_vu[s].slot = s; g_vs[s].state = 1; g_vs[s].chase = g_vg.chase;
        GarHelp(second ? "Reforco pesado a caminho: 4 policiais com MP5, viatura blindada." : "Reforco a caminho: 4 policiais.");
    } else GarHelp("Nao deu para mandar a viatura agora. Tente de novo.");
}
static void VgCallCar(DWORD now) {
    VgOneClockFix(now);
    if (g_vu[2].on) GarHelp("A sua viatura ja esta a caminho.");
    else if ((int)(now - g_vg.oneUntil) < 0 && g_vg.oneUntil) GarHelp("Viatura nova em %u s.", (unsigned)((g_vg.oneUntil - now + 999) / 1000));
    else if (VgSpawn(g_vu[2], 1, false, true)) { g_vg.oneUntil = now + (DWORD)(g_vgOneCool * 1000.f); GarHelp("Uma viatura esta vindo para voce."); }
    else GarHelp("Nao deu para mandar a viatura agora. Tente de novo.");
}
static void VgCmdBackup() { if (g_vg.active) VgCallBackup(GameMs()); }
static void VgCmdCar() { if (g_vg.active) VgCallCar(GameMs()); }
static void VigTick() {
    if (!g_vgOn) return;
    char* ms = MissionScript();
    bool vig = ms && !_strnicmp(ms + 8, "copcar", 7);
    if (!vig) { if (g_vg.active) VgStop(); return; }
    char* me = PlayerPed();
    if (!me) return;
    DWORD now = GameMs();
    if (!g_vg.active || g_vg.script != ms) {
        DWORD ou = g_vg.oneUntil;
        memset(&g_vg, 0, sizeof g_vg); g_vg.oneUntil = ou;
        g_vg.active = true; g_vg.script = ms; g_vg.ynWas = 0xFFFF;
        int n = g_vgOutSec > 0 ? VgPatchGrace(ms) : 0;
        int lvl = VigLevel();
        if (lvl >= 64) { int k = VgPatch64(ms); Log("vigilante: nivel %d: qualquer veiculo e a pe (%d de 4 pontos do script)", lvl, k); }
        if (g_vgNoChase) { Cmd c(0x01F7); c.I(0).I(1); RunCmd(c); g_vg.ignoreSet = true; }
        RequestModel(346, 2); RequestModel(349, 2); LoadAllRequestedModels(false);   // pistola e escopeta dos policiais
        Log("vigilante: missao comecou (policia %s, fora da viatura %d s em %d de 4 pontos do script)",
            g_vgNoChase ? "ignora voce" : "normal", g_vgOutSec, n);
        if (NewButtons()) GarHelp("Vigilante: o botao Reforco chama a policia e o Viatura traz uma viatura para voce.");
        else GarHelp("Vigilante: Sim chama reforco, Nao chama uma viatura para voce.");
    }
    g_vigYN = true;
    if (g_vgNever) VgNeverWanted(true);
    // checkpoint (3.47): as viaturas de reforco que estavam com voce voltam assim que o reforco puder vir
    if (g_vgRes.backupLeft > 0 && *(volatile int*)0xB72914 == 0 && VgBackupReady(now) && (!g_vgRes.tBackup || now - g_vgRes.tBackup > 3000)) {
        g_vgRes.tBackup = now; g_vgRes.backupLeft--;
        Log("checkpoint do Vigilante: reforco de volta (faltam %d)", g_vgRes.backupLeft);
        VgCallBackup(now);
    }
    // interior (restaurante, loja...): as viaturas e os policiais do reforco ficam la fora; o jogo fechou ao sair de um
    // restaurante com o reforco chamado (log de 20/11). Ao entrar, o reforco vai embora e tudo do reforco para ate 3 s
    // depois de sair.
    {
        static bool inside = false; static DWORD tOut = 0;
        bool in = *(volatile int*)0xB72914 != 0;                                // CGame::currArea
        if (in) {
            if (!inside) {
                inside = true;
                for (int s = 0; s < 2; s++) { VgReleaseUnit(g_vu[s], false); g_vs[s].state = 0; }
                VgReleaseUnit(g_vu[2], false);
                Log("vigilante: CJ entrou num interior; reforco dispensado");
            }
            return;
        }
        if (inside) { inside = false; tOut = now; Log("vigilante: CJ saiu do interior"); }
        if (tOut && now - tOut < 3000) return;
        tOut = 0;
    }
    if (g_vgNoChase) {
        char* w = *(char**)0xB7CD9C;                                          // CWanted do jogador
        if (w && Rd<int>(w, 0x2C) > 0) { Cmd c(0x0110); c.I(0); RunCmd(c); }   // CLEAR_WANTED_LEVEL
    }
    // suspeitos e perseguicao
    int nSus = 0;
    ForEachPed([&](char* p) { if (VgSuspect(p)) nSus++; });
    if (nSus > 0 && !g_vg.inChase) { g_vg.inChase = true; g_vg.chase++; Log("vigilante: perseguicao %d (%d suspeitos)", g_vg.chase, nSus); }
    if (g_vg.inChase && nSus == 0) {
        if (!g_vg.tNoSus) g_vg.tNoSus = now;
        else if (now - g_vg.tNoSus > 2500) { VgChaseEnd(now, "suspeitos mortos"); g_vg.tNoSus = 0; }
    } else g_vg.tNoSus = 0;
    g_vg.nSus = nSus;
    // Sim / Nao (overlay ate o 3.6; no 3.7 sao os botoes Reforco e Viatura, comandos 6 e 7)
    WORD raw = NewButtons() ? 0 : g_vigRaw;
    WORD pressed = raw & ~g_vg.ynWas;
    g_vg.ynWas = raw;
    if ((pressed & DPAD_RIGHT) && !g_rcYN) VgCallBackup(now);                // Sim: reforco (o recrutar tem a vez)
    if (pressed & DPAD_LEFT) VgCallCar(now);                                 // Nao: viatura para o CJ
    V3 pp = EntityPos(me);
    for (auto& u : g_vu) VgUnitTick(u, pp, now);
    if (g_vg.inChase && now - g_vg.tCops > 1000) {
        g_vg.tCops = now;
        // o script marca cada suspeito com SET_CHAR_ONLY_DAMAGED_BY_PLAYER (so o CJ fere): a policia atirava e nao
        // fazia dano. Aqui a marca sai; a missao conta o suspeito morto (IS_CHAR_DEAD) seja quem for que matou.
        int freed = 0;
        ForEachPed([&](char* p) {
            if (!VgSuspect(p)) return;
            int r = PedRef(p);
            for (int i = 0; i < g_vg.nFreed; i++) if (g_vg.freed[i] == r) return;
            { Cmd c(0x02A9); c.I(r).I(0); RunCmd(c); }
            if (g_vg.nFreed < 16) g_vg.freed[g_vg.nFreed++] = r;
            freed++;
        });
        if (freed) Log("vigilante: %d suspeitos agora levam dano da policia tambem", freed);
        if (g_vgHelpOn) VgStreetCops(now);
        // a mira automatica do CJ nao trava em policial durante o Vigilante (SET_CHAR_NEVER_TARGETTED; desfeito no fim)
        if (g_vgNoAim) ForEachPed([&](char* p) {
            if (g_vg.nNoAim >= 128 || Rd<int>(p, 0x598) != 6 || Rd<float>(p, 0x540) <= 0.f || VgOurs(p) || RcIsRecruit(p)) return;
            int r = PedRef(p);
            for (int i = 0; i < g_vg.nNoAim; i++) if (g_vg.noAim[i] == r) return;
            { Cmd c(0x0568); c.I(r).I(1); RunCmd(c); }
            g_vg.noAim[g_vg.nNoAim++] = r;
        });
    }
    // bonus de tempo dobrado a cada BonusEvery niveis (3.40): o mod mede quanto o timer $8222 (ms) subiu na troca de
    // nivel ($8227) e soma o mesmo tanto de novo. O timer so desce sozinho; subida e o bonus que o script deu. Vale a
    // subida de ate 6 s antes a 10 s depois da troca. (Ate a 3.39 lia o bonus em 63@, com teto de 60 s; nos niveis 3, 6 e 9
    // nao dobrava.)
    // nivel 32: sem timer. O relogio sai da tela (CLEAR_ONSCREEN_TIMER $8222); sem ele o jogo nao desconta o tempo.
    // O valor fica em 1 h para a conta do tempo fora da viatura (min($8222, OutOfCarSeconds)) continuar igual.
    bool noTimer = VigLevel() >= 32;
    if (noTimer) {
        volatile int* timer = (volatile int*)(0xA49960 + 8222 * 4);
        if (*timer != -100) {
            if (now - g_vg.tTimer >= 500 || now < g_vg.tTimer) { g_vg.tTimer = now; Cmd c(0x014F); c.G(8222); RunCmd(c); }
            if (*timer < 3600000) {
                if (!g_vg.timerNoted) { g_vg.timerNoted = true; Log("vigilante: nivel %d, missao sem timer", VigLevel()); }
                *timer = 3600000;
            }
        }
    }
    if (g_vgBonusEvery > 0 && !noTimer) {
        volatile int* lvlVar = (volatile int*)(0xA49960 + 8227 * 4);
        volatile int* timer  = (volatile int*)(0xA49960 + 8222 * 4);
        int lvl = *lvlVar, t = *timer;
        int l63 = *(volatile int*)(0xA48960 + 63 * 4);
        if (g_vg.havePrev) {
            int rise = t - g_vg.prevTimer;
            if (rise > 500 && rise <= 600000) {
                g_vg.lastRise = rise; g_vg.tRise = now; g_vg.riseUsed = false;
                Log("vigilante: o timer subiu +%d s (nivel %d, 63@=%d)", (rise + 500) / 1000, lvl, l63);
            }
        }
        g_vg.prevTimer = t; g_vg.havePrev = true;
        if (!g_vg.lastLvl) g_vg.lastLvl = lvl;
        else if (lvl > g_vg.lastLvl) {
            g_vg.lastLvl = lvl; g_vg.lvlSeen = lvl; g_vg.tLvl = now;
            g_vg.pendLvl = (lvl % g_vgBonusEvery == 0) ? lvl : 0;
        } else if (lvl < g_vg.lastLvl) { g_vg.lastLvl = lvl; g_vg.pendLvl = 0; }
        if (g_vg.pendLvl) {
            if (!g_vg.riseUsed && g_vg.tRise && (int)(g_vg.tRise - g_vg.tLvl) > -6000 && t > 0) {
                int add = g_vg.lastRise;
                *timer = t + add;
                g_vg.prevTimer = t + add; g_vg.riseUsed = true;
                int sec = (add + 500) / 1000;
                GarHelp("Nivel %d: bonus de tempo dobrado, +%d s a mais.", g_vg.pendLvl, sec);
                Log("vigilante: nivel %d, o jogo deu +%d s e o bonus foi dobrado (+%d s; 63@=%d)", g_vg.pendLvl, sec, sec, l63);
                g_vg.pendLvl = 0;
            } else if (now - g_vg.tLvl > 10000) {
                Log("vigilante: nivel %d, o timer nao subiu ate 10 s depois da troca (timer %d s, 63@=%d); bonus nao dobrado",
                    g_vg.pendLvl, t / 1000, l63);
                g_vg.pendLvl = 0;
            }
        }
    }
}
// depois do nivel 12: o nivel de procurado cai mais rapido, ate MaxStars estrelas
static void WantedPerkTick() {
    char* w = *(char**)0xB7CD9C;
    if (!g_vgPerk || !w) { g_vgPrevChaos = -1; return; }
    int chaos = Rd<int>(w, 0);
    int stat = VigLevel();                                                    // maior nivel do Vigilante
    if (stat < 12 || g_vg.active || g_vgPrevChaos < 0 || chaos >= g_vgPrevChaos) { g_vgPrevChaos = chaos; return; }
    int drop = g_vgPrevChaos - chaos, prev = g_vgPrevChaos;
    g_vgPrevChaos = chaos;
    if (drop > 4) return;                                                    // nao foi o jogo baixando (script, suborno...)
    static const int T[] = { 0, 50, 180, 550, 1200, 2400 };
    int lvl = 0; while (lvl < 5 && prev >= T[lvl + 1]) lvl++;
    if (lvl < 1 || lvl > g_vgMaxStars || lvl > 4) return;
    char* me = PlayerPed();
    if (!me || CopNear(EntityPos(me), g_vgCopR)) return;
    float span = (float)(T[lvl + 1] - T[lvl]);
    if (lvl == 1) span = 130.f;
    float secs = stat >= 64 ? g_vgStar64 : g_vgStarSec;                       // nivel 64: 4x a velocidade do jogo
    float mult = span / (secs > 1.f ? secs : 1.f);
    int extra = (int)(drop * (mult - 1.f) + 0.5f);
    if (extra <= 0) return;
    int nc = chaos - extra; if (nc < 0) nc = 0;
    *(volatile int*)w = nc;
    ((UpdWanted_t)0x561C90)(w);                                               // CWanted::UpdateWantedLevel
    g_vgPrevChaos = nc;
    static DWORD tLog = 0; DWORD t = GetTickCount();
    if (t - tLog > 10000) { tLog = t; Log("procurado: caindo mais rapido (%d estrelas, caos %d -> %d)", lvl, chaos, nc); }
}
// ---------------------------------------------------------------- recompensas do Vigilante (3.41)
// Nivel = estatistica 157 (maior nivel do Vigilante completado) ou [Vigilante] TestLevel, para testar.
//  12+: mirar num policial sem estar procurado nao da nada (CEventGunAimedAt::AffectsPed 0x4B4EE0 e o crime de
//       mirar); mira travada num policial da rua + Sim recruta ele (1); no Vigilante o telefone liga para o
//       armeiro da policia (gratis, Enforcer com um SWAT, a policia nao liga).
//  21+: alarme de carro roubado perto da policia, viatura roubada e batida em viatura nao dao estrela; ate 3
//       recrutas, com Desert Eagle, doze de combate ou M4.
//  32+: Vigilante sem timer: o relogio sai da tela (sem ele o jogo nao desconta o tempo) e o tempo fica alto.
//  64+: Vigilante a qualquer hora: a pe pelo telefone, em qualquer veiculo pelo botao de submissao (o codigo da
//       missao carregada passa a aceitar qualquer veiculo e a pe); armeiro sempre; a policia ignora qualquer
//       crime (estrela so por script de missao) e vai atras dos seus alvos; estrelas caem 4x mais rapido ate a
//       4a; quem traz o carro da garagem e uma policial da regiao.
// Crimes: CCrime::ReportCrime 0x532010, CWanted::RegisterCrime 0x562410 e RegisterCrime_Immediately 0x562430 com
// filtro por tipo; e as chamadas de SetWantedLevelNoDrop que nao passam por eles (alarme 0x66B285/0x6AE0C3,
// viatura roubada 0x66B2EE/0x4BB7D9, batida 0x6A7A54/0x6B90AD, tiro em viatura 0x6D82A4). Zona restrita
// (CCullZones, Area 69) e estrela dada por script ficam como no jogo.
static int   g_vgTestLevel = 0, g_rcOn = 1, g_rcMax12 = 1, g_rcMax21 = 3, g_vg64Help = 1;
static int VigLevel() {
    if (g_vgTestLevel > 0) return g_vgTestLevel;
    return *(volatile int*)(0xB79000 + (157 - 120) * 4);
}
static int WantedNow() { char* w = *(char**)0xB7CD9C; return w ? Rd<int>(w, 0x2C) : 0; }
static bool OnMission() { return *(volatile int*)(0xA49960 + 409 * 4) != 0; }          // $409 ($ONMISSION)
static bool InPlayerGroup(const char* p) {
    char* me = PlayerPed();
    const char* pd = me ? Rd<const char*>(me, 0x480) : nullptr;
    if (!pd || !p) return false;
    unsigned idx = Rd<unsigned>(pd, 0x38);
    if (idx >= 8) return false;
    const char* members = (const char*)0xC09920 + idx * 0x2D4 + 0x8 + 0x4;
    for (int i = 0; i < 7; i++) if (Rd<const char*>(members, i * 4) == p) return true;
    return false;
}

// ---- crimes ignorados
static const char* CrimeName(int t) {
    static const char* n[] = { "nenhum", "tiro", "agressao", "agressao a policial", "dano a carro", "dano a viatura",
        "roubo de carro", "sinal vermelho", "dano a veiculo", "excesso de velocidade", "atropelamento",
        "atropelar policial", "derrubar helicoptero", "fogo em pedestre", "fogo em policial", "fogo em carro",
        "derrubar aviao", "explosao", "facada", "facada em policial", "destruir veiculo", "batida", "mirar em policial" };
    return t >= 0 && t < (int)(sizeof n / sizeof n[0]) ? n[t] : "?";
}
static bool VigActiveNow();
static bool CrimeIgnored(int t) {
    if (g_vgNever && VigActiveNow()) return true;                              // Vigilante: nunca procurado (3.45)
    int L = VigLevel();
    if (L >= 64) return true;
    if (L >= 21 && (t == 6 || t == 8 || t == 21)) return true;               // roubo de carro, dano a veiculo, batida
    if (L >= 12 && t == 22 && WantedNow() == 0) return true;                   // mirar em policial
    return false;
}
static void NoteIgnored(const char* what) {
    static DWORD tLog = 0; static int n = 0;
    n++;
    DWORD t = GetTickCount();
    if (t - tLog > 5000) { tLog = t; Log("policia: ignorou %s (nivel %d do Vigilante; %d no total)", what, VigLevel(), n); }
}
typedef void (__cdecl* ReportCrime_t)(int, void*, void*);
typedef void (__thiscall* RegCrime_t)(void*, int, const void*, unsigned, int);
typedef bool (__thiscall* AimAffects_t)(void*, char*);
typedef void (__thiscall* SetWantedND_t)(void*, int);
static ReportCrime_t g_origReportCrime = nullptr;
static RegCrime_t    g_origRegCrime = nullptr, g_origRegCrimeNow = nullptr;
static AimAffects_t  g_origAimAffects = nullptr;
static void __cdecl ReportCrimeDetour(int t, void* victim, void* by) {
    if (by && by == (void*)PlayerPed() && CrimeIgnored(t)) { NoteIgnored(CrimeName(t)); return; }
    g_origReportCrime(t, victim, by);
}
static void __thiscall RegCrimeDetour(void* w, int t, const void* pos, unsigned id, int dc) {
    if (CrimeIgnored(t)) { NoteIgnored(CrimeName(t)); return; }
    g_origRegCrime(w, t, pos, id, dc);
}
static void __thiscall RegCrimeNowDetour(void* w, int t, const void* pos, unsigned id, int dc) {
    if (CrimeIgnored(t)) { NoteIgnored(CrimeName(t)); return; }
    g_origRegCrimeNow(w, t, pos, id, dc);
}
// mirar num policial sem estar procurado: o policial nem reage (e o crime de mirar nao sai)
static bool __thiscall AimAffectsDetour(void* ev, char* ped) {
    if (ped && Rd<int>(ped, 0x598) == 6 && Rd<char*>((char*)ev, 0x14) == PlayerPed() && VigLevel() >= 12 && WantedNow() == 0)
        return false;
    return g_origAimAffects(ev, ped);
}
static void __thiscall PlrWantedSteal(void* plr, int lvl) {
    if (VigLevel() >= 21) { NoteIgnored("roubo de carro"); return; }
    ((SetWantedND_t)0x609F30)(plr, lvl);
}
static void __thiscall PlrWantedRam(void* plr, int lvl) {
    if (VigLevel() >= 21) { NoteIgnored("batida em viatura"); return; }
    ((SetWantedND_t)0x609F30)(plr, lvl);
}
static void __thiscall PlrWantedShootCar(void* plr, int lvl) {
    if (VigLevel() >= 64) { NoteIgnored("dano a viatura"); return; }
    ((SetWantedND_t)0x609F30)(plr, lvl);
}
static void __thiscall WantedStealCopCar(void* w, int lvl) {
    if (VigLevel() >= 21) { NoteIgnored("roubo de viatura"); return; }
    ((SetWantedND_t)0x562570)(w, lvl);
}
static void ApplyCrimePatches() {
    PatchCallSite(0x66B285, 0x609F30, (void*)&PlrWantedSteal, "policia: alarme de carro roubado (nivel 21)");
    PatchCallSite(0x6AE0C3, 0x609F30, (void*)&PlrWantedSteal, "policia: alarme perto da viatura (nivel 21)");
    PatchCallSite(0x66B2EE, 0x609F30, (void*)&PlrWantedSteal, "policia: viatura roubada (nivel 21)");
    PatchCallSite(0x4BB7D9, 0x562570, (void*)&WantedStealCopCar, "policia: tirar policial da viatura (nivel 21)");
    PatchCallSite(0x6A7A54, 0x609F30, (void*)&PlrWantedRam, "policia: batida em viatura (nivel 21)");
    PatchCallSite(0x6B90AD, 0x609F30, (void*)&PlrWantedRam, "policia: batida em moto da policia (nivel 21)");
    PatchCallSite(0x6D82A4, 0x609F30, (void*)&PlrWantedShootCar, "policia: dano a viatura (nivel 64)");
}
static void InstallCrimeHooks() {
    HookGame(0x532010, (void*)&ReportCrimeDetour, (void**)&g_origReportCrime, "dos crimes (CCrime::ReportCrime)");
    HookGame(0x562410, (void*)&RegCrimeDetour, (void**)&g_origRegCrime, "dos crimes (CWanted::RegisterCrime)");
    HookGame(0x562430, (void*)&RegCrimeNowDetour, (void**)&g_origRegCrimeNow, "dos crimes (RegisterCrime_Immediately)");
    HookGame(0x4B4EE0, (void*)&AimAffectsDetour, (void**)&g_origAimAffects, "da mira em policial (CEventGunAimedAt)");
}

// ---- alvos: quem atacou voce, o seu carro ou os recrutas, e quem voce atacou (60 s)
struct TgEntry { int ref; DWORD until; bool attacker; };
static TgEntry g_tg[16];
static int     g_nTg = 0;
static float   g_tgHp[256];
static BYTE    g_tgId[256];
static float   g_tgMeHp = -1.f, g_tgCarHp = -1.f;
static char*   g_tgCar = nullptr;
static char* PedFromDamager(char* e) {                                       // o pedestre (ou o motorista) que causou o dano
    if (!e) return nullptr;
    BYTE t = Rd<BYTE>(e, 0x36) & 7;
    if (t == 3) return e;
    if (t == 2) return Rd<char*>(e, 0x460);
    return nullptr;
}
static void TgAdd(char* p, bool attacker, DWORD now, const char* why) {
    char* me = PlayerPed();
    if (!p || p == me || (Rd<BYTE>(p, 0x36) & 7) != 3 || Rd<float>(p, 0x540) <= 0.f) return;
    int type = Rd<int>(p, 0x598);
    if (type == 6 || type <= 1 || RcIsRecruit(p) || VgOurs(p) || InPlayerGroup(p)) return;
    int ref = PedRef(p);
    for (int i = 0; i < g_nTg; i++) if (g_tg[i].ref == ref) { g_tg[i].until = now + 60000; g_tg[i].attacker |= attacker; return; }
    if (g_nTg >= 16) { int o = 0; for (int i = 1; i < g_nTg; i++) if ((int)(g_tg[i].until - g_tg[o].until) < 0) o = i; g_tg[o] = g_tg[--g_nTg]; }
    g_tg[g_nTg++] = { ref, now + 60000, attacker };
    static DWORD tLog = 0; DWORD t = GetTickCount();
    if (t - tLog > 3000) { tLog = t; Log("alvos: pedestre %d (tipo %d) %s; %d alvos", Rd<short>(p, 0x22), type, why, g_nTg); }
}
// alvo mais perto de 'from' (so quem atacou, com attackers)
static char* TgNearest(V3 from, float maxD, bool attackers) {
    char* best = nullptr; float bd = maxD;
    for (int i = 0; i < g_nTg; i++) {
        if (attackers && !g_tg[i].attacker) continue;
        char* p = PedByRef(g_tg[i].ref);
        if (!p || Rd<float>(p, 0x540) <= 0.f) continue;
        float d = Dist2(EntityPos(p), from);
        if (d < bd) { bd = d; best = p; }
    }
    return best;
}
static void TgTick(DWORD now) {
    static DWORD tT = 0;
    if (now - tT < 250 && now >= tT) return;
    tT = now;
    char* me = PlayerPed();
    if (!me || VigLevel() < 12) { g_nTg = 0; g_tgMeHp = -1.f; return; }
    for (int i = g_nTg - 1; i >= 0; i--) {
        char* p = PedByRef(g_tg[i].ref);
        if (!p || Rd<float>(p, 0x540) <= 0.f || (int)(now - g_tg[i].until) > 0) g_tg[i] = g_tg[--g_nTg];
    }
    float hp = Rd<float>(me, 0x540) + Rd<float>(me, 0x548);
    if (g_tgMeHp >= 0.f && hp < g_tgMeHp - 0.5f) TgAdd(PedFromDamager(Rd<char*>(me, 0x764)), true, now, "atacou voce");
    g_tgMeHp = hp;
    char* car = PlayerVehicle();
    if (car != g_tgCar) { g_tgCar = car; g_tgCarHp = car ? Rd<float>(car, 0x4C0) : -1.f; }
    else if (car) {
        float h = Rd<float>(car, 0x4C0);
        if (h < g_tgCarHp - 1.f) TgAdd(PedFromDamager(Rd<char*>(car, 0x50C)), true, now, "atacou o seu carro");
        g_tgCarHp = h;
    }
    char* pool = *(char**)0xB74490;
    if (!pool) return;
    int size = Rd<int>(pool, 8); if (size > 256) size = 256;
    for (int i = 0; i < size; i++) {
        BYTE flag = Rd<BYTE*>(pool, 4)[i];
        if (flag & 0x80) { g_tgId[i] = 0xFF; continue; }
        char* p = Rd<char*>(pool, 0) + i * 0x7C4;
        float h = Rd<float>(p, 0x540) + Rd<float>(p, 0x548);
        if (g_tgId[i] == flag && h < g_tgHp[i] - 0.5f && p != me) {
            char* src = PedFromDamager(Rd<char*>(p, 0x764));
            if (src == me) TgAdd(p, false, now, "foi atacado por voce");
            else if (src && RcIsRecruit(p)) TgAdd(src, true, now, "atacou um recruta");
        }
        g_tgId[i] = flag; g_tgHp[i] = h;
    }
}

// ---- mira do drive-by ([MiraDriveBy], 3.42; gancho novo na 3.43)
// No drive-by (moto, carro, motorista ou passageiro) o tiro do CJ sai na direcao da camera (modo 0x31/0x37): a tarefa
// CTaskSimpleGangDriveBy::FireGun (0x627CC0) tira o alvo de Find3rdPersonCamTargetVector a 20 m e chama CWeapon::Fire em
// 0x628328 com ele; em rampa ou desnivel a camera inclina e o tiro vai no chao ou para o alto. O jogo so tem mira
// automatica para as armas do proprio veiculo (Hunter: CWeapon::DoDriveByAutoAiming 0x73D720). A 3.42 acertava o fim
// do tiro dentro de FireInstantHit (CBirds::HandleGunShot), mas o log nao mostrou nenhum acerto: agora o alvo e trocado
// na chamada de CWeapon::Fire do drive-by, antes do jogo calcular o tiro, e o alcance e o da arma (nao os 20 m).
//  1) alvo: suspeito do Vigilante (ou o veiculo em que ele esta) e os alvos de 60 s (quem atacou voce, o seu carro ou
//     os recrutas, e quem voce atacou), ate Cone graus para cada lado e no alcance da arma, sem predio no meio: o tiro
//     vai nele (o mais perto da direcao do tiro, depois o mais perto de voce);
//  2) sem alvo: pedestre ou veiculo ate ConeAltura graus: so a altura do tiro muda (como a "doom aiming" do jogo,
//     0x73CDC0), o lado continua o seu. Policial e viatura so entram se voce estiver com estrelas.
static int   g_daOn = 1, g_daCars = 1;
static float g_daCone = 35.f, g_daConeZ = 12.f;
typedef bool (__cdecl* LosClear_t)(const V3*, const V3*, bool, bool, bool, bool, bool, bool, bool);
static bool DaClear(const V3& a, const V3& b) { return ((LosClear_t)0x56A490)(&a, &b, true, false, false, false, false, false, false); }   // CWorld::GetIsLineOfSightClear, so predios
static float DaAngle(float ax, float ay, float bx, float by) {                 // graus entre dois vetores no plano
    float la = sqrtf(ax * ax + ay * ay), lb = sqrtf(bx * bx + by * by);
    if (la < 1e-3f || lb < 1e-3f) return 180.f;
    float c = (ax * bx + ay * by) / (la * lb);
    if (c > 1.f) c = 1.f;
    if (c < -1.f) c = -1.f;
    return acosf(c) * 57.29578f;
}
static float DaJitter(float r) { return r * (float)((rand() % 201) - 100) / 100.f; }
static int g_daHit = 0;                                                       // ultimo DaAim: 0 nada, 1 alvo, 2 so altura
static void DaAim(V3* s, V3* e, float range) {
    g_daHit = 0;
    char* me = PlayerPed(); char* myCar = PlayerVehicle();
    if (!me || !myCar) return;
    int sub = Rd<int>(myCar, 0x594);                                            // 2 quadriciclo, 9 moto, 10 bicicleta
    bool bike = sub == 2 || sub == 9 || sub == 10;
    if (!bike && !g_daCars) return;
    float fx = e->x - s->x, fy = e->y - s->y, fz = e->z - s->z;
    { float L = sqrtf(fx * fx + fy * fy + fz * fz);                            // o alvo da camera fica a uns 20 m: estica
      if (L > 0.01f) { e->x = s->x + fx / L * range; e->y = s->y + fy / L * range; e->z = s->z + fz / L * range; } }
    // 1) suspeitos e alvos
    const char* bestE = nullptr; V3 bestAt = {}; float best = 1e9f; bool bestCar = false;
    auto consider = [&](const char* ped) {
        if (!ped || ped == me || Rd<float>(ped, 0x540) <= 0.f) return;
        const char* car = VgCarOf(ped);
        if (car == myCar) return;
        V3 at = car ? EntityPos(car) : EntityPos(ped);
        if (!car) at.z += 0.3f;                                                 // peito
        float dx = at.x - s->x, dy = at.y - s->y, dz = at.z - s->z;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d < 1.5f || d > range) return;
        float ang = DaAngle(fx, fy, dx, dy);
        if (ang > g_daCone) return;
        float score = ang / g_daCone + 0.5f * d / range;
        if (score >= best || !DaClear(*s, at)) return;
        best = score; bestAt = at; bestE = car ? car : ped; bestCar = car != nullptr;
    };
    if (g_daCone > 0.f) {
        if (VigActiveNow()) ForEachPed([&](char* p) { if (VgSuspect(p)) consider(p); });
        for (int i = 0; i < g_nTg; i++) consider(PedByRef(g_tg[i].ref));
    }
    static const char* lastE = nullptr; static DWORD tLog = 0, tDiag = 0;
    DWORD now = GetTickCount();
    if (!bestE && VigActiveNow() && now - tDiag > 5000) {
        tDiag = now;
        const char* np = nullptr; float nd = 1e9f, na = 0.f;
        ForEachPed([&](char* p) {
            if (!VgSuspect(p)) return;
            char* c = VgCarOf(p); V3 q = c ? EntityPos(c) : EntityPos(p);
            float dx = q.x - s->x, dy = q.y - s->y, d = sqrtf(dx * dx + dy * dy);
            if (d < nd) { nd = d; np = p; na = DaAngle(fx, fy, dx, dy); }
        });
        if (np) Log("mira do drive-by: sem alvo; suspeito mais perto a %d m e %d graus (alcance %d, abertura %d)", (int)nd, (int)na, (int)range, (int)g_daCone);
        else Log("mira do drive-by: sem alvo; nenhum suspeito agora");
    }
    if (bestE) {
        float j = bestCar ? 0.3f : 0.1f;
        bestAt.x += DaJitter(j); bestAt.y += DaJitter(j); bestAt.z += DaJitter(j * 0.5f);
        float dx = bestAt.x - s->x, dy = bestAt.y - s->y, dz = bestAt.z - s->z;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        float L = range > d + 3.f ? range : d + 3.f;
        e->x = s->x + dx / d * L; e->y = s->y + dy / d * L; e->z = s->z + dz / d * L;
        g_daHit = 1;
        if (bestE != lastE && now - tLog > 2000) {
            tLog = now; lastE = bestE;
            if (bestCar) Log("mira do drive-by: no %s do alvo, a %d m", CarName(Rd<short>(bestE, 0x22)), (int)d);
            else Log("mira do drive-by: no alvo (pedestre %d), a %d m", Rd<short>(bestE, 0x22), (int)d);
        }
        return;
    }
    // 2) so a altura: o pedestre ou veiculo mais perto da direcao do tiro
    if (g_daConeZ <= 0.f) return;
    bool wanted = WantedNow() > 0;
    const char* zE = nullptr; V3 zAt = {}; float zBest = g_daConeZ;
    float f2 = sqrtf(fx * fx + fy * fy);
    auto considerZ = [&](const char* ent, V3 at) {
        float dx = at.x - s->x, dy = at.y - s->y;
        float d2 = sqrtf(dx * dx + dy * dy);
        if (d2 < 1.5f || d2 > range) return;
        float ang = DaAngle(fx, fy, dx, dy) + d2 / range;                       // empate: o mais perto
        if (ang >= zBest || !DaClear(*s, at)) return;
        zBest = ang; zE = ent; zAt = at;
    };
    ForEachPed([&](char* p) {
        if (p == me || Rd<float>(p, 0x540) <= 0.f || (Rd<BYTE>(p, 0x46D) & 1)) return;
        int t = Rd<int>(p, 0x598);
        if (t <= 1 || (t == 6 && !wanted) || RcIsRecruit(p) || InPlayerGroup(p) || VgOurs(p)) return;
        V3 at = EntityPos(p); at.z += 0.3f;
        considerZ(p, at);
    });
    char* pool = *(char**)0xB74494;
    if (pool) {
        int size = Rd<int>(pool, 8);
        for (int i = 0; i < size; i++) {
            if (Rd<BYTE*>(pool, 4)[i] & 0x80) continue;
            char* v = Rd<char*>(pool, 0) + i * 0xA18;
            if (v == myCar || Rd<float>(v, 0x4C0) <= 0.f) continue;
            char* drv = Rd<char*>(v, 0x460);
            if (drv && Rd<int>(drv, 0x598) == 6 && !wanted) continue;
            considerZ(v, EntityPos(v));
        }
    }
    if (!zE || f2 < 1e-3f) return;
    float dx = zAt.x - s->x, dy = zAt.y - s->y;
    float t = f2 / sqrtf(dx * dx + dy * dy);
    e->z = s->z + (zAt.z - s->z) * t;
    g_daHit = 2;
}
// 3.46: o log da 3.45 mostrou que o tiro do CJ nunca passou pela chamada de 0x628328 (o FireGun de la e o dos bandidos
// da IA). Agora o gancho e no proprio CWeapon::Fire (0x742300, MinHook): todo tiro do CJ dentro de um veiculo (drive-by
// de lado, SMG para a frente na moto) passa por DaAim, com o alcance da arma. Sem alvo da camera, o alvo novo so entra
// se DaAim achar alguem. O tiro para a frente do mod (FwTick) ja chega mirado e nao passa de novo.
typedef bool (__thiscall* WeaponFire_t)(void*, void*, V3*, V3*, void*, V3*, V3*);
static WeaponFire_t g_fireOrig = (WeaponFire_t)0x742300;
static volatile bool g_fwFiring = false;
static bool __fastcall FireHook(void* w, void*, char* by, V3* start, V3* barrel, void* tEnt, V3* tPos, V3* alt) {
    if (g_daOn && !g_fwFiring && by && by == PlayerPed() && PlayerVehicle()) {
        int type = Rd<int>(w, 0);
        static int noted = 0;
        if (noted < 3) { noted++; Log("mira do drive-by: tiro do CJ no veiculo (arma %d, alvo %s, chamado de 0x%X)", type, tPos ? "da camera" : "nenhum", (unsigned)(DWORD)__builtin_return_address(0)); }
        if (type >= 22 && type <= 38) {
            V3 src = start ? *start : barrel ? *barrel : EntityPos(by);
            const char* info = GetWeaponInfo(type, GetWeaponSkill(by, type) & 0xFF);
            float range = info ? Rd<float>(info, 8) : 45.f;
            if (range < 20.f) range = 20.f;
            if (tPos) { V3 end = *tPos; DaAim(&src, &end, range); *tPos = end; }
            else {
                int ci = *(volatile BYTE*)0xB6F081;
                const float* fr = (const float*)(0xB6F19C + ci * 0x238 + 0x190);  // CCam::Front da camera ativa
                static V3 end;
                end = { src.x + fr[0] * range, src.y + fr[1] * range, src.z + fr[2] * range };
                DaAim(&src, &end, range);
                if (g_daHit) return g_fireOrig(w, by, start, barrel, tEnt, &end, alt);
            }
        }
    }
    return g_fireOrig(w, by, start, barrel, tEnt, tPos, alt);
}
static void InstallDriveByAim() {
    HookGame(0x742300, (void*)&FireHook, (void**)&g_fireOrig, "da mira do drive-by (CWeapon::Fire)");
}

// ---- armas no veiculo e tiro para a frente ([ArmasNoVeiculo], 3.44)
typedef char* (__thiscall* FindTask_t)(void*, int);                          // CTaskManager::FindActiveTaskByType 0x681740
// Trocar arma: no veiculo o jogo nao deixa (CPlayerPedData m_bInVehicleDontAllowWeaponChange) e, ao entrar, poe a SMG na
// mao (CPed::RemoveWeaponWhenEnteringVehicle). Os comandos 10 e 11 do overlay (deslizar no Trocar arma do carro) passam
// pelos slots 0 (sem arma), 2 pistola, 3 escopeta, 4 SMG, 5 fuzil e 6 (so o rifle de caca) com CPed::SetCurrentWeapon
// (0x5E61F0). Ao sair, o jogo volta a arma de antes de entrar.
// Drive-by: CTaskSimpleCarDrive::ProcessPed so cria o CTaskSimpleGangDriveBy se houver SMG no slot 4 com municao
// (motorista 0x6446C3, passageiro 0x644A27: [ped+0x610] e [ped+0x61C]). As duas checagens viram uma chamada que olha a
// arma na mao; o FireGun atira a arma na mao ([ped+0x718]). Os bandidos da IA ja fazem drive-by com fuzil e escopeta.
// Tiro para a frente (carro e monster truck; a moto ja tem): Atirar sem Olhar para os lados e sem drive-by ativo atira
// do para-brisa para a frente (sem animacao), com a mira do drive-by (DaAim). CWeapon::Fire (0x742300) faz som,
// municao, recarga e ritmo; o FireInstantHit so atira para um alvo com o bit 0 de CPedIK (ped+0x50C+0x1C, "arma
// apontada") ligado, como o FireGun faz, entao o bit liga so durante a chamada. O tiro ignora o proprio carro, entao o
// para-brisa quebra por DAMAGE_CAR_PANEL (0730, painel 4): trinca no 1o tiro e quebra no 4o.
static const char* CwName(int t) {
    if (t <= 1) return "Sem arma";
    for (auto& it : g_armItems) if (it.type == t) return it.name;
    return "arma";
}
static void CwCycle(int dir) {
    char* me = PlayerPed(); char* v = PlayerVehicle();
    if (!g_cwOn || !me || !v) return;
    static const int kSlots[6] = { 0, 2, 3, 4, 5, 6 };
    int cur = Rd<BYTE>(me, 0x718), ci = 0;
    for (int i = 0; i < 6; i++) if (kSlots[i] == cur) ci = i;
    for (int k = 1; k <= 6; k++) {
        int i = ((ci + dir * k) % 6 + 6) % 6, sl = kSlots[i];
        int t = WeaponType(me, sl);
        if (sl != 0 && !(CwAllowed(t) && WeaponAmmo(me, sl) > 0)) continue;
        if (sl != 0) {
            const char* wi = GetWeaponInfo(t, 1);
            int mid = wi ? Rd<int>(wi, 0xC) : -1;
            if (mid > 0 && !ModelLoaded(mid)) { RequestModel(mid, 2); LoadAllRequestedModels(false); }
        }
        ((void(__thiscall*)(char*, int))0x5E61F0)(me, sl);                      // CPed::SetCurrentWeapon(slot)
        GarHelp("%s", sl == 0 ? "Sem arma" : CwName(t));
        Log("veiculo: arma trocada para %s (slot %d)", sl == 0 ? "nenhuma" : CwName(t), sl);
        return;
    }
}
static int __cdecl CwDriveByOk(char* ped) {
    bool orig = Rd<int>(ped, 0x610) != 0 && Rd<int>(ped, 0x61C) > 0;           // como o jogo: SMG no slot 4
    if (!g_cwOn || ped != PlayerPed()) return orig;
    int sl = Rd<BYTE>(ped, 0x718), t = WeaponType(ped, sl);
    char* v = (Rd<BYTE>(ped, 0x46D) & 1) ? Rd<char*>(ped, 0x58C) : nullptr;
    int sub = v ? Rd<int>(v, 0x594) : 0;
    bool smg = t == 28 || t == 29 || t == 32;
    if ((sub == 9 || sub == 10 || sub == 2) && !smg && !g_cwBikeAnim) return 0; // moto sem AnimacaoNaMoto: tiro pelo mod
    return CwAllowed(t) && WeaponAmmo(ped, sl) > 0;
}
// ---- drive-by do jogo na moto com qualquer arma ([ArmasNoVeiculo] AnimacaoNaMoto, 3.48)
// O drive-by do CJ pilotando moto, bicicleta ou quadriciclo nao e a tarefa CTaskSimpleGangDriveBy (por isso o gancho
// de 0x628328 nunca viu o tiro dele): e o CVehicle::DoDriveByShootings (0x741FD0), chamado pelo ProcessControl dos
// veiculos so para o motorista jogador com m_bCanDoDriveBy (+0x153 do CPlayerData). Ele toca as animacoes de drive-by
// do grupo de pilotagem da moto (0xCA, 0xCB, 0xCC: esquerda, direita, frente), atira por CWeapon::FireFromCar
// (0x73FA20 -> FireInstantHitFromCar 0x73EC40 -> FireInstantHitFromCar2 0x73CBA0, que faz a linha do tiro) e, com o
// Atirar solto, enche o pente na hora. A unica trava de arma e em 0x742051: slot da arma (CWeaponInfo+0x14) == 4, SMG.
// O mod troca essa conferencia por CwDdbOk: em moto, bicicleta e quadriciclo, qualquer arma de CwAllowed.
// Ritmo: depois do tiro o jogo poe o proximo em agora + 70 ms (0x742296), o da SMG; para as outras, CwDdbTick troca
// pelo laco da animacao da arma (como o tiro do mod). Mira: gancho no FireInstantHitFromCar2 (DaAim no fim do tiro do
// CJ), que tambem pega a SMG na moto (o tiro dela nao passava por CWeapon::Fire).
static char*    g_ddbW = nullptr;
static unsigned g_ddbT = 0;
static int      g_ddbType = 0;
static int __cdecl CwDdbOk(char* wi, char* w, char* veh) {
    bool orig = wi && Rd<int>(wi, 0x14) == 4;
    if (!g_cwOn || !g_cwBikeAnim || !veh || !w) return orig;
    int sub = Rd<int>(veh, 0x594);
    if (sub != 9 && sub != 10 && sub != 2) return orig;
    if (orig) return 1;
    int t = Rd<int>(w, 0);
    bool ok = CwAllowed(t);
    static bool noted[64] = {};
    if (ok && t >= 0 && t < 64 && !noted[t]) { noted[t] = true; Log("moto: drive-by do jogo com %s (com animacao)", CwName(t)); }
    return ok;
}
typedef void (__thiscall* Fihfc2_t)(void*, float, float, float, float, float, float, char*, char*);
static Fihfc2_t g_fihfc2Orig = (Fihfc2_t)0x73CBA0;
static void __fastcall Fihfc2Hook(void* w, void*, float sx, float sy, float sz, float ex, float ey, float ez, char* veh, char* owner) {
    char* me = PlayerPed();
    if (me && owner == me && w) {
        int t = Rd<int>(w, 0);
        static int noted = 0;
        if (noted < 4) { noted++; Log("drive-by do veiculo: tiro do CJ com %s (FireInstantHitFromCar2, chamado de 0x%X)", CwName(t), (unsigned)(DWORD)__builtin_return_address(0)); }
        if (g_daOn) {
            V3 s3 = { sx, sy, sz }, e3 = { ex, ey, ez };
            const char* info = GetWeaponInfo(t, GetWeaponSkill(me, t) & 0xFF);
            float range = info ? Rd<float>(info, 8) : 45.f;
            if (range < 20.f) range = 20.f;
            DaAim(&s3, &e3, range);
            ex = e3.x; ey = e3.y; ez = e3.z;
        }
        if (t != 28 && t != 29 && t != 32) { g_ddbW = (char*)w; g_ddbT = *TimeMs; g_ddbType = t; }
    }
    g_fihfc2Orig(w, sx, sy, sz, ex, ey, ez, veh, owner);
}
static void CwDdbTick() {
    if (!g_ddbW) return;
    char* w = g_ddbW; g_ddbW = nullptr;
    char* me = PlayerPed();
    if (!me || Rd<int>(w, 0) != g_ddbType) return;
    if (Rd<unsigned>(w, 0x10) != g_ddbT + 70) return;                       // so o ritmo que o DoDriveByShootings acabou de por
    const char* info = GetWeaponInfo(g_ddbType, GetWeaponSkill(me, g_ddbType) & 0xFF);
    float loop = info ? Rd<float>(info, 0x44) - Rd<float>(info, 0x40) : 0.3f;
    unsigned iv = (unsigned)(loop * 1000.f);
    if (iv < 80) iv = 80;
    *(unsigned*)(w + 0x10) = g_ddbT + iv;
    static int noted = 0;
    if (noted < 3) { noted++; Log("moto: ritmo de %s no drive-by: %u ms", CwName(g_ddbType), iv); }
}
static void InstallBikeDriveBy() {
    if (!g_cwOn || !g_cwBikeAnim) return;
    BYTE* st = (BYTE*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!st) { Log("moto: sem memoria para o desvio do drive-by"); return; }
    // add esp,8 | push edi (veiculo) | push ebx (CWeapon) | push eax (CWeaponInfo) | call CwDdbOk | add esp,0xC
    // | test eax,eax | je 0x7422F6 (sem drive-by) | jmp 0x742060 (segue)
    BYTE code[27] = { 0x83, 0xC4, 0x08, 0x57, 0x53, 0x50, 0xE8, 0, 0, 0, 0, 0x83, 0xC4, 0x0C, 0x85, 0xC0,
                      0x0F, 0x84, 0, 0, 0, 0, 0xE9, 0, 0, 0, 0 };
    *(DWORD*)(code + 7)  = (DWORD)&CwDdbOk - ((DWORD)st + 11);
    *(DWORD*)(code + 18) = 0x7422F6 - ((DWORD)st + 22);
    *(DWORD*)(code + 23) = 0x742060 - ((DWORD)st + 27);
    memcpy(st, code, sizeof code);
    static const BYTE expect[15] = { 0x8B, 0x48, 0x14, 0x83, 0xC4, 0x08, 0x83, 0xF9, 0x04, 0x0F, 0x85, 0x96, 0x02, 0x00, 0x00 };
    BYTE repl[15]; memset(repl, 0x90, sizeof repl);
    repl[0] = 0xE9; *(DWORD*)(repl + 1) = (DWORD)st - (0x742051 + 5);
    PatchBytes(0x742051, expect, 15, repl, 15, "moto: drive-by do jogo com qualquer arma (CVehicle::DoDriveByShootings)");
}

static void InstallFihfc2() {
    HookGame(0x73CBA0, (void*)&Fihfc2Hook, (void**)&g_fihfc2Orig, "da mira do drive-by do veiculo (CWeapon::FireInstantHitFromCar2)");
}
static void CwPatchSite(DWORD site, int len, DWORD jeTarget, const BYTE* expect, BYTE* stub) {
    BYTE rep[32]; memset(rep, 0x90, sizeof rep);
    rep[0] = 0xE8; *(DWORD*)(rep + 1) = (DWORD)stub - (site + 5);
    rep[5] = 0x85; rep[6] = 0xC0;
    rep[7] = 0x0F; rep[8] = 0x84; *(DWORD*)(rep + 9) = jeTarget - (site + 13);
    PatchBytes(site, expect, len, rep, len, "armas no veiculo: drive-by com a arma na mao");
}
static void InstallCarWeapons() {
    if (!g_cwOn) return;
    BYTE* stub = (BYTE*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!stub) { Log("armas no veiculo: sem memoria para o desvio"); return; }
    BYTE code[] = { 0x51, 0x52, 0x56, 0xE8, 0, 0, 0, 0, 0x83, 0xC4, 0x04, 0x5A, 0x59, 0xC3 };   // push ecx/edx/esi; call; add esp,4; pop edx/ecx; ret
    *(DWORD*)(code + 4) = (DWORD)&CwDriveByOk - ((DWORD)stub + 8);
    memcpy(stub, code, sizeof code);
    static const BYTE a[24] = { 0x8B,0x86,0x10,0x06,0x00,0x00, 0x85,0xC0, 0x0F,0x84,0x86,0x00,0x00,0x00,
                                0x8B,0x86,0x1C,0x06,0x00,0x00, 0x85,0xC0, 0x7E,0x7C };
    static const BYTE b[28] = { 0x8B,0x86,0x10,0x06,0x00,0x00, 0x85,0xC0, 0x0F,0x84,0xC2,0x00,0x00,0x00,
                                0x8B,0x86,0x1C,0x06,0x00,0x00, 0x85,0xC0, 0x0F,0x8E,0xB4,0x00,0x00,0x00 };
    CwPatchSite(0x6446C3, 24, 0x644757, a, stub);                                // motorista
    CwPatchSite(0x644A27, 28, 0x644AF7, b, stub);                                // passageiro
}
static void FwTick() {
    static DWORD tNext = 0; static char* glassCar = nullptr; static int glassShots = 0; static bool noted = false;
    char* me = PlayerPed(); char* v = PlayerVehicle();
    if (!g_fwOn || !me || !v || Rd<char*>(v, 0x460) != me || !NewButtonsAlive()) return;
    int sub = Rd<int>(v, 0x594);
    bool bike = sub == 9 || sub == 2;                                           // moto e quadriciclo: armas que nao sao SMG
    if (sub != 0 && sub != 1 && !bike) return;                                  // carro e monster truck: so para a frente
    bool lookL = g_host.lt > 40, lookR = g_host.rt > 40;
    if (!(g_host.buttons & 0x2000) || (!bike && (lookL || lookR)) || (lookL && lookR)) return;   // Atirar (B)
    { int at = WeaponType(me, Rd<BYTE>(me, 0x718)); if (bike && (g_cwBikeAnim || at == 28 || at == 29 || at == 32)) return; }   // o jogo
    if (MenuActive() || g_menuOpen || g_shopOpen) return;
    char* intel = Rd<char*>(me, 0x47C);
    if (intel && ((FindTask_t)0x681740)(intel + 4, 1022)) return;             // drive-by de lado em andamento
    int sl = Rd<BYTE>(me, 0x718), t = WeaponType(me, sl);
    if (!CwAllowed(t) || WeaponAmmo(me, sl) <= 0) return;
    char* w = me + 0x5A0 + sl * 0x1C;
    int st = Rd<int>(w, 4);
    if (st != 0 && st != 1) return;                                             // pronta ou atirando (nao recarregando)
    DWORD now = GetTickCount();
    if ((int)(now - tNext) < 0) return;
    const char* info = GetWeaponInfo(t, GetWeaponSkill(me, t) & 0xFF);
    float range = info ? Rd<float>(info, 8) : 45.f;
    if (range < 20.f) range = 20.f;
    float loop = info ? Rd<float>(info, 0x44) - Rd<float>(info, 0x40) : 0.3f;
    DWORD iv = (DWORD)(loop * 1000.f); if (iv < 80) iv = 80;
    const float* m = Rd<const float*>(v, 0x14);
    if (!m) return;
    V3 vp = EntityPos(v);
    V3 fw = { m[4], m[5], m[6] }, up = { m[8], m[9], m[10] };
    float fwd = 1.3f, upd = 0.6f;
    if (bike) {                                                                 // moto: do CJ para a frente ou para o lado
        V3 rt = { m[0], m[1], m[2] };
        float sgn = lookL ? -1.f : 1.f;
        if (lookL || lookR) fw = { rt.x * sgn, rt.y * sgn, 0.f };
        fwd = 0.6f; upd = 1.0f;
    }
    V3 start = { vp.x + fw.x * fwd + up.x * upd, vp.y + fw.y * fwd + up.y * upd, vp.z + fw.z * fwd + up.z * upd };
    V3 end = { start.x + fw.x * range, start.y + fw.y * range, start.z + fw.z * range };
    if (g_daOn) DaAim(&start, &end, range);
    DWORD* ik = (DWORD*)(me + 0x50C + 0x1C);
    DWORD oldIk = *ik; *ik |= 1;
    // 3.46: o espalhamento do tiro do CJ e (100 - precisao)/precisao da arma x CPlayerData+0x2C (m_fAttackButtonCounter),
    // que sobe a cada tiro e so desce a pe: na moto ele so crescia (fuzil espalhando muito). Zera antes de cada tiro.
    char* pd = Rd<char*>(me, 0x480);
    if (pd) *(float*)(pd + 0x2C) = 0.f;
    g_fwFiring = true;
    bool ok = g_fireOrig(w, me, &start, &start, nullptr, &end, nullptr);
    g_fwFiring = false;
    if (pd) *(float*)(pd + 0x2C) = 0.f;
    *ik = oldIk;
    if (!ok) return;
    tNext = now + iv;
    if (!noted) { noted = true; Log("veiculo: tiro pelo mod com %s (%s, a cada %u ms, alcance %d m)", CwName(t), bike ? "moto" : "carro, para a frente", (unsigned)iv, (int)range); }
    if (g_fwGlass && !bike && (sub == 0 || sub == 1)) {
        if (glassCar != v) { glassCar = v; glassShots = 0; }
        glassShots++;
        if (glassShots == 1 || glassShots == 4) { int r = VehRef(v); if (r >= 0) { Cmd c(0x0730); c.I(r).I(4); RunCmd(c); } }
    }
}

// 3.46: recarga no veiculo. Pente vazio: o CWeapon::Fire poe a arma em recarga (estado 2) e o CWeapon::Update termina
// na hora marcada; se 1,5 s depois ela ainda estiver em recarga, o mod termina (o log diz se isso acontece). Toque no
// icone da arma no veiculo (overlay 3.9, comando 4): recarrega o pente como a pe, sem animacao, com o som do jogo.
static void CwReloadNow(char* ped) {
    int sl = Rd<BYTE>(ped, 0x718), t = WeaponType(ped, sl), ms = 0;
    if (!CwAllowed(t) || !CanReload(ped, &ms)) { Log("recarregar no veiculo: a arma nao precisa ou nao da agora"); return; }
    char* w = ped + 0x5A0 + sl * W_STRIDE;
    *(int*)(w + 4) = 2;                                                       // WEAPONSTATE_RELOADING
    *(unsigned*)(w + 0x10) = *TimeMs + (unsigned)ms;
    Log("recarregar no veiculo: %s, pente com %d de %d, %d ms", CwName(t), Rd<int>(w, 8), Rd<int>(w, 0xC), ms);
}
static void CwReloadTick() {
    char* me = PlayerPed();
    if (!g_cwOn || !me || !PlayerVehicle()) return;
    int sl = Rd<BYTE>(me, 0x718);
    if (sl > 12) return;
    char* w = me + 0x5A0 + sl * W_STRIDE;
    int t = Rd<int>(w, 0);
    if (!CwAllowed(t) || Rd<int>(w, 4) != 2) return;
    int late = (int)(*TimeMs - Rd<unsigned>(w, 0x10));
    if (late < 1500 || late > 600000) return;
    ((void(__thiscall*)(char*, char*))0x73AEB0)(w, me);                      // CWeapon::Reload
    *(int*)(w + 4) = 0;
    static int n = 0;
    if (n < 5) { n++; Log("veiculo: recarga de %s terminada pelo mod (o jogo nao terminou)", CwName(t)); }
}

// ---- mira a pe (3.43)
// 1) Prioridade: no Vigilante, quando a mira trava (o alvo era nenhum), se ela pegou alguem que nao e suspeito nem
//    agressor, troca para o suspeito mais perto da direcao da camera, a pe ou no carro (trava no suspeito de dentro
//    do carro; o tiro pega no carro). Fora disso, so para agressor.
// 2) Hitman: com a arma no nivel hitman (pistolas, SMGs, escopetas, fuzis), quando o alvo travado morre, a mira vai
//    sozinha para o proximo agressor visivel (atacando voce, o seu carro ou os recrutas; suspeito do Vigilante;
//    alvos de 60 s). Policial so com Policiais=1. Sem outro, fica no morto, como no jogo.
// Agressor: tarefa de matar (TASK_COMPLEX_KILL_PED_ON_FOOT 1000 / 1028, alvo em +0x10; melee 1001 e armado 1002,
// +0xC; drive-by 1022, +0x34) com o alvo sendo o CJ, o carro dele ou alguem do grupo. A mira e CPed+0x71C
// (CleanUpOldReference 0x571A00 / RegisterReference 0x571B70, como em FindNextWeaponLockOnTarget).
static int g_apOn = 1, g_htOn = 1, g_htCops = 0, g_htDelay = 200;
static char* AimTaskTarget(char* p, int type, int off) {
    char* intel = Rd<char*>(p, 0x47C);
    if (!intel) return nullptr;
    char* t = ((FindTask_t)0x681740)(intel + 4, type);                          // CTaskManager::FindActiveTaskByType
    return t ? Rd<char*>(t, off) : nullptr;
}
static bool AimMineOrGroup(char* e, char* me) {
    if (!e) return false;
    if (e == me || e == PlayerVehicle()) return true;
    if ((Rd<BYTE>(e, 0x36) & 7) == 3) return InPlayerGroup(e) || RcIsRecruit(e);
    return false;
}
static bool AimAggressor(char* p, char* me) {
    if (!p || p == me || Rd<float>(p, 0x540) < 1.f) return false;
    int t = Rd<int>(p, 0x598);
    if (t <= 1 || RcIsRecruit(p) || InPlayerGroup(p) || VgOurs(p)) return false;
    if (t == 6 && !g_htCops) return false;
    if (VigActiveNow() && VgSuspect(p)) return true;
    int r = PedRef(p);
    for (int i = 0; i < g_nTg; i++) if (g_tg[i].ref == r && g_tg[i].attacker) return true;
    static const int kT[6] = { 1000, 1028, 1001, 1002, 1022, 0 }, kO[6] = { 0x10, 0x10, 0xC, 0xC, 0x34, 0 };
    for (int i = 0; kT[i]; i++) if (AimMineOrGroup(AimTaskTarget(p, kT[i], kO[i]), me)) return true;
    return false;
}
static void AimSetLock(char* me, char* e) {
    char** field = (char**)(me + 0x71C);
    char* old = *field;
    if (old == e) return;
    if (old) ((void(__thiscall*)(char*, char**))0x571A00)(old, field);
    *field = e;
    if (e) ((void(__thiscall*)(char*, char**))0x571B70)(e, field);
}
static bool AimOnScreen(char* e) { return ((bool(__thiscall*)(char*))0x534540)(e); }  // CEntity::GetIsOnScreen
// o melhor alvo: suspeito/agressor visivel, no alcance, mais perto da direcao (ref: frente da camera ou o morto)
static char* AimBest(char* me, char* skip, float range, float fx, float fy, bool inCars) {
    V3 mp = EntityPos(me); V3 head = mp; head.z += 0.6f;
    char* best = nullptr; float bs = 1e9f;
    ForEachPed([&](char* p) {
        if (p == skip) return;
        char* car = VgCarOf(p);
        if (car && !inCars) return;
        if (!AimAggressor(p, me)) return;
        V3 q = car ? EntityPos(car) : EntityPos(p);
        if (!car) q.z += 0.3f;
        float dx = q.x - mp.x, dy = q.y - mp.y, dz = q.z - mp.z;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d > range || d < 1.f) return;
        float ang = DaAngle(fx, fy, dx, dy);
        float sc = ang / 90.f + 0.5f * d / range;
        if (sc >= bs || !AimOnScreen(car ? car : p) || !DaClear(head, q)) return;
        bs = sc; best = car ? car : p;
    });
    return best;
}
static void AimTick(DWORD now) {
    static char* last = nullptr; static char* deadT = nullptr; static DWORD tDead = 0, tTry = 0, tLog = 0;
    char* me = PlayerPed();
    if (!me || PlayerVehicle()) { last = nullptr; deadT = nullptr; return; }
    char* t = Rd<char*>(me, 0x71C);
    int slot = Rd<BYTE>(me, 0x718), type = WeaponType(me, slot);
    const char* info = (type >= 22 && type <= 38) ? GetWeaponInfo(type, GetWeaponSkill(me, type) & 0xFF) : nullptr;
    float range = info ? Rd<float>(info, 4) : 0.f;
    if (range < 10.f) range = 10.f;
    // 1) mira acabou de travar em alguem que nao interessa: vai para o suspeito
    if (g_apOn && t && !last && info && VigActiveNow() && (Rd<BYTE>(t, 0x36) & 7) == 3 && !AimAggressor(t, me)) {
        int ci = *(volatile BYTE*)0xB6F081;
        const float* fr = (const float*)(0xB6F19C + ci * 0x238 + 0x190);          // CCam::Front da camera ativa
        char* b = AimBest(me, t, range, fr[0], fr[1], true);
        if (b) {
            AimSetLock(me, b);
            if (now - tLog > 3000 || now < tLog) { tLog = now; Log("mira: no %s %d em vez do pedestre %d", (Rd<BYTE>(b, 0x36) & 7) == 2 ? "carro do suspeito, modelo" : "suspeito/agressor, pedestre", Rd<short>(b, 0x22), Rd<short>(t, 0x22)); }
            t = b;
        }
    }
    last = t;
    // 2) hitman: o alvo travado morreu
    if (!g_htOn || !t || (Rd<BYTE>(t, 0x36) & 7) != 3 || Rd<float>(t, 0x540) >= 1.f) { deadT = nullptr; return; }
    if (!info || type < 22 || type > 32 || (GetWeaponSkill(me, type) & 0xFF) != 2) return;
    if (deadT != t) { deadT = t; tDead = now; tTry = 0; return; }
    if (now - tDead < (DWORD)g_htDelay || (tTry && now - tTry < 300)) return;
    tTry = now;
    V3 mp = EntityPos(me), dp = EntityPos(t);
    char* b = AimBest(me, t, range, dp.x - mp.x, dp.y - mp.y, false);
    if (!b) return;
    AimSetLock(me, b); deadT = nullptr; last = b;
    Log("mira hitman: proximo agressor (pedestre %d, %d m)", Rd<short>(b, 0x22), (int)Dist2(EntityPos(b), mp));
}

// ---- pistola silenciada (3.43): no intermediario atira no ritmo da pistola, no hitman 1,5x; pente de 12 (.45 ACP)
// CWeaponInfo (0x70): +0x20 pente (short), +0x38 precisao (maior = mais certeira; o espalhamento e dividido por ela e
// cresce com o toque rapido no Atirar), +0x40/+0x44/+0x48 inicio/fim/tiro do laco da animacao (s), +0x4C.. o laco
// agachado. Fim do laco como o jogo calcula: inicio + floor(0,1 + quadros/30 x 50)/50 - 0,006.
static int g_psOn = 1, g_psClip = 12, g_psStdFr = 9, g_psProFr = 6;
static float g_psStdAcc = 1.35f, g_psProAcc = 1.6f;
static void PsLoop(char* info, int frames) {
    for (int k = 0; k < 2; k++) {
        float* st = (float*)(info + 0x40 + k * 12);
        float start = st[0];
        st[1] = start + floorf(0.1f + (frames / 30.f) * 50.f) / 50.f - 0.006f;
        st[2] = start;                                                          // o tiro sai no comeco do laco
    }
}
static void PsApply() {
    if (!g_psOn) return;
    for (int sk = 0; sk < 3; sk++) {
        char* info = GetWeaponInfo(23, sk);
        if (!info) continue;
        if (g_psClip > 0) *(short*)(info + 0x20) = (short)g_psClip;
        if (sk == 1) { if (g_psStdFr > 0) PsLoop(info, g_psStdFr); *(float*)(info + 0x38) = g_psStdAcc; }
        if (sk == 2) { if (g_psProFr > 0) PsLoop(info, g_psProFr); *(float*)(info + 0x38) = g_psProAcc; }
    }
    static bool noted = false;
    if (!noted) { noted = true; Log("pistola silenciada: pente %d, intermediario %d quadros por tiro (precisao %.2f), hitman %d (precisao %.2f)",
                  g_psClip, g_psStdFr, g_psStdAcc, g_psProFr, g_psProAcc); }
}

// ---- policiais recrutados (nivel 12: 1, nivel 21: ate 3)
struct Recruit { int ref, weap, lastTgt; bool hates, vigHate; DWORD tDB, tKill, tEnter, tBack; };
static Recruit g_rc[4];
static int     g_nRc = 0, g_rcCand = -1;
static WORD    g_rcYnWas = 0;
static DWORD   g_rcHoldUntil = 0;                                            // Chamar: sem ordem de ataque a pe ate aqui (3.46)
static float   g_rcTgtR = 35.f, g_rcBackD = 40.f;                             // [Vigilante] RecruitTargetRadius / RecruitReturnDistance
static const int kRcHate[] = { 7, 9, 10, 11, 12, 13, 14, 15, 16, 17, 20 };     // gangues (menos a Grove, 8), traficantes, criminosos
static bool RcIsRecruit(const char* p) {
    if (!p) return false;
    int r = PedRef(p);
    for (int i = 0; i < g_nRc; i++) if (g_rc[i].ref == r) return true;
    return false;
}
static int RcCount() { return g_nRc; }
static int RcMax() { int L = VigLevel(); return L >= 21 ? g_rcMax21 : L >= 12 ? g_rcMax12 : 0; }
static int PlayerGroupRef() { int g = -1; Cmd c(0x07AF); c.I(0).Out(0); RunCmd(c, &g, 1); return g; }
static void RcHates(Recruit& r, bool on) {
    if (r.hates == on) return;
    for (int t : kRcHate) { Cmd c(on ? 0x077A : 0x077B); c.I(r.ref).I(4).I(t); RunCmd(c); }
    r.hates = on;
}
static void RcVigHate(Recruit& r, bool on) {
    if (r.vigHate == on) return;
    { Cmd c(on ? 0x077A : 0x077B); c.I(r.ref).I(4).I(24); RunCmd(c); }      // suspeitos do Vigilante (MISSION1)
    r.vigHate = on;
}
static void RcRelease(int i, const char* why) {
    char* p = PedByRef(g_rc[i].ref);
    if (p) {
        RcHates(g_rc[i], false); RcVigHate(g_rc[i], false);
        { Cmd c(0x06C9); c.I(g_rc[i].ref); RunCmd(c); }
        { Cmd c(0x0568); c.I(g_rc[i].ref).I(0); RunCmd(c); }
        { Cmd c(0x01C2); c.I(g_rc[i].ref); RunCmd(c); }
    }
    g_rc[i] = g_rc[--g_nRc];
    Log("recrutas: um policial %s (%s); %d com voce", p ? "foi embora" : "se perdeu", why, g_nRc);
}
static void RcReleaseAll(const char* why) { while (g_nRc) RcRelease(g_nRc - 1, why); }
static bool RcCandidate(char* p) {
    return p && (Rd<BYTE>(p, 0x36) & 7) == 3 && Rd<int>(p, 0x598) == 6 && Rd<float>(p, 0x540) > 0.f && Rd<BYTE>(p, 0x484) == 1
        && !(Rd<BYTE>(p, 0x46D) & 1) && !VgOurs(p) && !RcIsRecruit(p);
}
// o policial travado na mira ou, sem mira (no Vigilante a mira nao trava em policial), o mais perto ate 6 m na frente
static char* RcPick(char* me) {
    char* t = Rd<char*>(me, 0x71C);
    if (t && RcCandidate(t)) return t;
    V3 mp = EntityPos(me);
    const float* m = Rd<const float*>(me, 0x14);
    float fx = m ? m[4] : 0.f, fy = m ? m[5] : 1.f;                            // frente do CJ
    char* best = nullptr; float bd = 6.f;
    ForEachPed([&](char* p) {
        if (!RcCandidate(p)) return;
        V3 q = EntityPos(p);
        float dx = q.x - mp.x, dy = q.y - mp.y, d = sqrtf(dx * dx + dy * dy);
        if (d >= bd || fabsf(q.z - mp.z) > 2.f) return;
        if (d > 1.f && (dx * fx + dy * fy) / d < 0.5f) return;                  // ate 60 graus para cada lado
        bd = d; best = p;
    });
    return best;
}
static void RcRecruit(char* p) {
    int ref = PedRef(p), grp = PlayerGroupRef();
    if (ref < 0 || grp < 0) { GarHelp("Nao deu para recrutar agora."); return; }
    static const int w12[2] = { 22, 25 }, w21[3] = { 24, 27, 31 };             // pistola, escopeta / Desert Eagle, doze de combate, M4
    int w = VigLevel() >= 21 ? w21[rand() % 3] : w12[rand() % 2];
    int wm = w == 22 ? 346 : w == 25 ? 349 : w == 24 ? 348 : w == 27 ? 351 : 356;
    RequestModel(wm, 2); LoadAllRequestedModels(false);
    *(BYTE*)(p + 0x484) = 2;                                                   // como personagem de script: o jogo nao apaga
    { Cmd c(0x0687); c.I(ref); RunCmd(c); }
    { Cmd c(0x0631); c.I(grp).I(ref); RunCmd(c); }
    bool member = false; { Cmd c(0x06EE); c.I(ref).I(grp); member = RunCmd(c); }
    if (!member) { *(BYTE*)(p + 0x484) = 1; { Cmd c(0x0249); c.I(wm); RunCmd(c); } GarHelp("O seu grupo esta cheio."); return; }
    { Cmd c(0x01B2); c.I(ref).I(w).I(9999); RunCmd(c); }
    { Cmd c(0x01B9); c.I(ref).I(w); RunCmd(c); }
    { Cmd c(0x08AF); c.I(ref).I(g_vgHealth); RunCmd(c); }
    { Cmd c(0x0223); c.I(ref).I(g_vgHealth); RunCmd(c); }
    if (g_vgArmour > 0) { Cmd c(0x035F); c.I(ref).I(g_vgArmour); RunCmd(c); }
    { Cmd c(0x0446); c.I(ref).I(0); RunCmd(c); }                               // sem morte de um tiro so na cabeca
    { Cmd c(0x0568); c.I(ref).I(1); RunCmd(c); }                               // a mira do CJ nao trava nele
    { Cmd c(0x0249); c.I(wm); RunCmd(c); }
    Recruit& r = g_rc[g_nRc++];
    memset(&r, 0, sizeof r); r.ref = ref; r.weap = w; r.lastTgt = -1;
    RcHates(r, true);
    static const char* wn[] = { "pistola", "escopeta", "Desert Eagle", "doze de combate", "M4" };
    const char* nm = w == 22 ? wn[0] : w == 25 ? wn[1] : w == 24 ? wn[2] : w == 27 ? wn[3] : wn[4];
    GarHelp("Policial recrutado (%s). %d de %d com voce.", nm, g_nRc, RcMax());
    Log("recrutas: policial modelo %d recrutado com %s; %d de %d", Rd<short>(p, 0x22), nm, g_nRc, RcMax());
}
static int RcSeat(const char* car, const char* p) {
    for (int s = 0; s < 8; s++) if (Rd<const char*>(car, 0x464 + s * 4) == p) return s;
    return -1;
}
static volatile bool g_rcCanRecruit = false;                                // mira num policial recrutavel (botao)
static void RcTick(DWORD now) {
    g_rcYN = false; g_rcCanRecruit = false;
    char* me = PlayerPed();
    if (!me || !g_rcOn) return;
    {   // Chamar (3.46): o botao do grupo do overlay (seta para cima) sem mira travada faz os recrutas largarem o que
        // estao fazendo e voltarem para voce; por 10 s nenhuma ordem de ataque nova a pe
        static WORD was = 0;
        WORD b = NewButtonsAlive() ? g_host.buttons : 0;
        if ((b & 1) && !(was & 1) && g_nRc && !PlayerVehicle() && !Rd<char*>(me, 0x71C) && !g_menuOpen && !g_shopOpen) {
            for (int i = 0; i < g_nRc; i++) { Cmd c(0x0687); c.I(g_rc[i].ref); RunCmd(c); g_rc[i].lastTgt = -1; }
            g_rcHoldUntil = now + 10000;
            Log("recrutas: chamados de volta (%d)", g_nRc);
        }
        was = b;
    }
    static DWORD tM = 0;
    if (now - tM >= 500 || now < tM) {
        tM = now;
        int grp = g_nRc ? PlayerGroupRef() : -1;
        for (int i = g_nRc - 1; i >= 0; i--) {
            char* p = PedByRef(g_rc[i].ref);
            if (!p || Rd<float>(p, 0x540) <= 0.f) { RcRelease(i, "caiu"); continue; }
            bool member = false; if (grp >= 0) { Cmd c(0x06EE); c.I(g_rc[i].ref).I(grp); member = RunCmd(c); }
            if (!member) RcRelease(i, "saiu do grupo");
        }
        if (g_nRc && WantedNow() > 0) { RcReleaseAll("voce ficou procurado"); GarHelp("Voce esta procurado: os policiais recrutados foram embora."); }
        while (g_nRc > RcMax()) RcRelease(g_nRc - 1, "nivel menor");
        char* myCar = PlayerVehicle();
        bool vig = g_vg.active;
        V3 pp = EntityPos(me);
        static bool wasInCar = false;
        bool enteredCar = myCar && !wasInCar;
        wasInCar = myCar != nullptr;
        for (int i = 0; i < g_nRc; i++) {
            Recruit& r = g_rc[i];
            char* p = PedByRef(r.ref);
            if (!p) continue;
            char* car = VgCarOf(p);
            bool inMyCar = car && car == myCar;
            // 3.46: o odio (gangues, criminosos, suspeitos) so vale com voce a pe. Com voce num veiculo, a IA fazia o
            // recruta descer (da garupa ou do carro) para atirar e correr atras do suspeito; agora, no veiculo, ele so
            // atira pelo drive-by que o mod manda, e a pe ele entra no seu veiculo
            bool fight = !myCar;
            RcVigHate(r, vig && fight);
            RcHates(r, fight);
            V3 rp = EntityPos(p);
            if (inMyCar) {
                int seat = RcSeat(car, p);
                if (seat < 0) continue;
                char* t = vig ? VgNearestSuspect(rp, 60.f) : nullptr;
                if (!t) t = TgNearest(rp, 60.f, true);                         // fora do Vigilante: so quem atacou
                if (!t) continue;
                int tr = PedRef(t);
                char* db = AimTaskTarget(p, 1022, 0x34);                       // drive-by em andamento: em quem?
                if (db == t && tr == r.lastTgt && now - r.tDB < 8000) continue;
                if (db != t && now - r.tDB < 1500) continue;
                r.tDB = now; r.lastTgt = tr;
                VgArm(r.ref, r.weap);
                Cmd x(0x0713); x.I(r.ref).I(tr).I(-1).F(0.f).F(0.f).F(0.f).F(60.f).I(8).I(seat == 1 ? 0 : 1).I(60); RunCmd(x);
            } else if (myCar) {                                                // voce no veiculo, ele fora: entra como passageiro
                if (car) continue;                                             // noutro veiculo: o grupo resolve
                if (enteredCar || r.lastTgt != -1) { Cmd c(0x0687); c.I(r.ref); RunCmd(c); r.lastTgt = -1; }
                if (Dist2(rp, pp) > 80.f || now - r.tEnter < 4000) continue;
                { char* intel = Rd<char*>(p, 0x47C); if (intel && ((FindTask_t)0x681740)(intel + 4, 700)) continue; }   // ja entrando
                int maxP = Rd<BYTE>(myCar, 0x488), seat = -1;
                for (int k = 0; k < maxP && k < 8; k++) if (!Rd<char*>(myCar, 0x464 + k * 4)) { seat = k; break; }
                if (seat < 0) continue;
                r.tEnter = now;
                int vr = VehRef(myCar);
                if (vr >= 0) { Cmd c(0x05CA); c.I(r.ref).I(vr).I(20000).I(seat); RunCmd(c); }
            } else if (!car) {                                                 // os dois a pe
                char* lt = r.lastTgt != -1 ? PedByRef(r.lastTgt) : nullptr;
                if (lt && Rd<float>(lt, 0x540) > 0.f && Dist2(EntityPos(lt), pp) > g_rcBackD + 10.f) {
                    Cmd c(0x0687); c.I(r.ref); RunCmd(c);                      // o alvo fugiu para longe de voce: volta
                    r.lastTgt = -1; r.tBack = now;
                    continue;
                }
                if (Dist2(rp, pp) > g_rcBackD) {                               // longe de voce: larga tudo e volta
                    if (now - r.tBack > 5000) { Cmd c(0x0687); c.I(r.ref); RunCmd(c); r.lastTgt = -1; r.tBack = now; }
                    continue;
                }
                if ((int)(now - g_rcHoldUntil) < 0) continue;                  // chamado ha pouco
                char* t = TgNearest(rp, 45.f, false);
                if (!t && vig) t = VgNearestSuspect(rp, 45.f);
                if (!t || Dist2(EntityPos(t), pp) > g_rcTgtR) continue;       // so alvo perto de voce
                int tr = PedRef(t);
                if (tr == r.lastTgt && now - r.tKill < 6000) continue;
                r.lastTgt = tr; r.tKill = now;
                VgKill(r.ref, tr, r.weap);
            }
        }
    }
    // mira travada num policial da rua, sem estar procurado: Sim recruta
    int L = VigLevel();
    char* t = (L >= 12 && !PlayerVehicle()) ? RcPick(me) : nullptr;
    if (!t || WantedNow() > 0 || MenuActive() || g_menuOpen || g_shopOpen) { g_rcCand = -1; return; }
    int ref = PedRef(t);
    if (g_nRc >= RcMax()) {
        if (g_rcCand != ref) { g_rcCand = ref; GarHelp("Voce ja tem %d policial(is) com voce. Dispense pelo telefone para recrutar outro.", g_nRc); }
        return;
    }
    if (NewButtons()) {                                                       // overlay 3.7: botao Recrutar policial
        g_rcCanRecruit = true;
        if (g_rcCand != ref) { g_rcCand = ref; GarHelp("Recrutar policial: chama este policial para o seu grupo (%d de %d).", g_nRc + 1, RcMax()); }
        return;
    }
    g_rcYN = true;
    WORD raw = g_vigRaw;
    if (g_rcCand != ref) {
        g_rcCand = ref; g_rcYnWas = raw;
        GarHelp("Sim: recrutar este policial (%d de %d).", g_nRc + 1, RcMax());
        return;
    }
    WORD pressed = raw & ~g_rcYnWas;
    g_rcYnWas = raw;
    if (pressed & DPAD_RIGHT) { RcRecruit(t); g_rcCand = -1; }
}

static void RcCmdRecruit() {
    char* me = PlayerPed();
    if (!me || !g_rcOn || VigLevel() < 12 || PlayerVehicle()) return;
    char* t = RcPick(me);
    if (!t || WantedNow() > 0) return;
    if (g_nRc >= RcMax()) { GarHelp("Voce ja tem %d policial(is) com voce. Dispense pelo telefone para recrutar outro.", g_nRc); return; }
    RcRecruit(t); g_rcCand = -1; g_rcCanRecruit = false;
}
// ---- nivel 64: policiais da rua vao atras dos seus alvos; Vigilante a qualquer hora
static bool Vig64CanStart(const char** why) {
    *why = nullptr;
    if (VigLevel() < 64) return false;
    if (OnMission()) { *why = "Termine a missao atual antes."; return false; }
    { Cmd c(0x0256); c.I(0); if (!RunCmd(c)) { *why = "Agora nao."; return false; } }
    { Cmd c(0x09BE); if (RunCmd(c)) { *why = "Agora nao."; return false; } }
    if ((*(volatile int*)(0xA49960 + 390 * 4) >> 1) & 1) { *why = "Agora nao."; return false; }
    return true;
}
static void Vig64Start(const char* how) {
    { Cmd c(0x00BA); c.Key("COP_M").I(6000).I(5); RunCmd(c); }
    { Cmd c(0x0417); c.I(124); RunCmd(c); }
    Log("vigilante: comecou %s (nivel %d)", how, VigLevel());
}
static void Vig64Stop(const char* how) {
    if (!g_vg.active) return;
    *(volatile int*)(0xA48960 + 58 * 4) = 1;                                    // 58@ = 1: a missao termina sem falhar
    GarHelp("Vigilante encerrado.");
    Log("vigilante: encerrado %s", how);
}
static void Vig64Stop() { Vig64Stop("pelo telefone"); }
static volatile bool g_vg64Can = false;                                       // da para comecar agora (botao Vigilante)
static void Vig64Toggle(const char* how) {
    if (VigLevel() < 64) return;
    if (g_vg.active) { Vig64Stop(how); return; }
    const char* why = nullptr;
    if (Vig64CanStart(&why)) Vig64Start(how);
    else if (why) GarHelp("%s", why);
}
// ---- checkpoint do Vigilante ([Vigilante] SalvarNaMissao, 3.47)
// O jogo nao salva em missao: CTheScripts::Save (0x5D4C40) grava cada script que nao e externo com o IP, e o codigo da
// missao (copcar, indice 124 do main.scm) nao estaria carregado ao abrir o save. Salvar pelo botao no Vigilante faz,
// so durante o GenericSave: a missao fica fora da lista (m_IsExternal +0xC7 = 1), $409 ($ONMISSION) = 0, o maximo de
// estrelas e a policia ignorando voltam ao normal. O save sai como fora de missao; ao lado vai o GTASAsfN.b.gtmvig com
// o nivel (51@, a tela mostra 51@ + 1), as kills ($8223) e quantas viaturas de reforco estavam com voce. Os blips de
// pedestre e carro o jogo ja grava apagados (CRadar::Save); pedestres e carros nao vao para o save.
// Ao carregar, depois do veiculo do .gtmveh: 0417 124 (como o botao do nivel 64) e, antes do primeiro passo da missao,
// os zeros de 51@ (offset 105 do codigo dela) e de $8223 (126) passam para 1000@ (sem uso); 51@ e $8223 vao direto na
// memoria; 144@ (241: desconto do tempo de bonus, +0.1 a cada nivel ate o 13) recebe o valor do nivel. Suspeitos novos.
// Nivel abaixo de 64: so dirigindo viatura (a missao exige; o .gtmveh traz ela de volta). Fora de interior.
#pragma pack(push, 1)
struct VigCkFile { DWORD magic, version, timeMs; int level, kills, backup, best; DWORD pad[4]; };
#pragma pack(pop)
static const DWORD VIGCK_MAGIC = 0x47495647;                                  // 'GVIG'
static void VgCkPath(char* out) { snprintf(out, MAX_PATH, "%s.gtmvig", SaveFileName); }
static bool VgInCopCar(char* me, char* v) {
    if (!me || !v || Rd<char*>(v, 0x460) != me) return false;
    if (Rd<short>(v, 0x22) == 425) return true;                               // Hunter (o jogo deixa)
    Cmd c(0x056C); c.I(PedRef(me));                                          // IS_CHAR_IN_ANY_POLICE_VEHICLE
    return RunCmd(c);
}
static bool VgCanCheckpoint(const char** why) {
    *why = nullptr;
    if (!g_vgSaveOn || !g_vgOn || !g_vg.active) return false;
    char* ms = MissionScript();
    if (!ms || ms != g_vg.script || _strnicmp(ms + 8, "copcar", 7)) return false;
    if (Rd<BYTE>(ms, 0xC7) || Rd<signed char>(ms, 0xC9) != -1) return false;
    if (*(volatile int*)(0xA48960 + 58 * 4) == 1) return false;               // 58@: a missao esta terminando
    if (*(volatile int*)0xB72914 != 0) { *why = "Vigilante: saia do interior para salvar."; return false; }
    char* me = PlayerPed(); char* v = PlayerVehicle();
    if (VigLevel() < 64) {
        if (!g_keepVehicle) { *why = "Vigilante: o veiculo junto do save esta desligado ([Save] KeepVehicle)."; return false; }
        if (!VgInCopCar(me, v)) { *why = "Vigilante: para salvar, esteja dirigindo a viatura."; return false; }
    } else if (v && Rd<char*>(v, 0x460) != me) { *why = "Vigilante: para salvar, esteja dirigindo ou a pe."; return false; }
    return true;
}
static void VgCkBegin(VgCkState& st) {
    st.ms = MissionScript();
    st.ext = st.ms ? Rd<BYTE>(st.ms, 0xC7) : 0;
    if (st.ms) *(BYTE*)(st.ms + 0xC7) = 1;                                    // CTheScripts::Save pula script externo
    volatile int* onm = (volatile int*)(0xA49960 + 409 * 4);
    st.onm = *onm; *onm = 0;
    st.never = g_vgMaxSaved >= 0;
    if (st.never) VgNeverWanted(false);
    st.ignore = g_vg.ignoreSet;
    if (st.ignore) { Cmd c(0x01F7); c.I(0).I(0); RunCmd(c); }
}
static void VgCkEnd(VgCkState& st) {
    if (st.ignore) { Cmd c(0x01F7); c.I(0).I(1); RunCmd(c); }
    if (st.never) VgNeverWanted(true);
    *(volatile int*)(0xA49960 + 409 * 4) = st.onm;
    if (st.ms) *(BYTE*)(st.ms + 0xC7) = st.ext;
}
static void VgCkWrite(unsigned timeMs) {
    char path[MAX_PATH]; VgCkPath(path);
    VigCkFile f = {};
    f.magic = VIGCK_MAGIC; f.version = 1; f.timeMs = timeMs;
    f.level = *(volatile int*)(0xA48960 + 51 * 4);
    f.kills = *(volatile int*)(0xA49960 + 8223 * 4);
    for (int s = 0; s < 2; s++) if (g_vs[s].state == 1 && g_vu[s].on) f.backup++;
    f.best = VigLevel();
    FILE* fp = fopen(path, "wb");
    if (!fp) { Log("checkpoint do Vigilante: nao gravou %s", path); return; }
    fwrite(&f, sizeof f, 1, fp); fclose(fp);
    GarHelp("Checkpoint do Vigilante salvo: nivel %d, %d kills.", f.level + 1, f.kills);
    Log("checkpoint do Vigilante salvo: nivel %d, %d kills, %d viatura(s) de reforco", f.level + 1, f.kills, f.backup);
}
static void VgCkDelete() { char path[MAX_PATH]; VgCkPath(path); DeleteFileA(path); }
static void VgCkLoad() {
    if (!g_vgSaveOn) return;
    MakeValidSaveName(g_saveSlot - 1);
    char path[MAX_PATH]; VgCkPath(path);
    FILE* fp = fopen(path, "rb");
    if (!fp) return;
    VigCkFile f = {};
    size_t n = fread(&f, sizeof f, 1, fp); fclose(fp);
    if (n != 1 || f.magic != VIGCK_MAGIC || f.version != 1) return;
    if (f.timeMs != g_loadedTimeMs) { Log("checkpoint do Vigilante: o save carregado nao e o do slot %d (t=%u, arquivo t=%u)", g_saveSlot, g_loadedTimeMs, f.timeMs); return; }
    if (f.level < 0 || f.level > 100000) return;
    g_vgRes.pending = true; g_vgRes.level = f.level; g_vgRes.kills = f.kills; g_vgRes.backup = f.backup < 0 ? 0 : f.backup > 2 ? 2 : f.backup;
    Log("checkpoint do Vigilante encontrado: nivel %d, %d kills, %d viatura(s) de reforco", f.level + 1, f.kills, g_vgRes.backup);
}
static void VgResumeTick() {
    if (!g_vgRes.pending) return;
    DWORD now = GetTickCount();
    char* me = PlayerPed();
    if (!me || MenuActive() || now - g_vgRes.tLoad < 2500 || *(volatile signed char*)0xB5F851) return;
    if (OnMission()) {                                                        // outra missao comecou antes
        if (now - g_vgRes.tLoad > 20000) { g_vgRes.pending = false; Log("checkpoint do Vigilante: cancelado (outra missao)"); }
        return;
    }
    if (VigLevel() < 64 && !VgInCopCar(me, PlayerVehicle())) {
        if (now - g_vgRes.tLoad > 12000) {
            g_vgRes.pending = false;
            GarHelp("Checkpoint do Vigilante cancelado: voce nao esta dirigindo a viatura.");
            Log("checkpoint do Vigilante: cancelado (CJ fora da viatura)");
        }
        return;
    }
    g_vgRes.pending = false;
    { Cmd c(0x0417); c.I(124); RunCmd(c); }
    char* ms = MissionScript();
    BYTE* base = ms ? Rd<BYTE*>(ms, 0x10) : nullptr;
    static const BYTE A[7] = { 0x06, 0x00, 0x03, 0x33, 0x00, 0x04, 0x00 };   // SET_LVAR_INT 51@ 0
    static const BYTE B[7] = { 0x04, 0x00, 0x02, 0x7C, 0x80, 0x04, 0x00 };   // SET_VAR_INT $8223 0
    static const BYTE C[10] = { 0x07, 0x00, 0x03, 0x90, 0x00, 0x06, 0, 0, 0, 0 };   // SET_LVAR_FLOAT 144@ 0
    static const BYTE N[7] = { 0x06, 0x00, 0x03, 0xE8, 0x03, 0x04, 0x00 };   // SET_LVAR_INT 1000@ 0
    if (!base || memcmp(base + 105, A, 7) || memcmp(base + 126, B, 7) || memcmp(base + 241, C, 10)) {
        Log("checkpoint do Vigilante: o codigo da missao nao e o esperado; ela comeca do nivel 1");
        GarHelp("Vigilante comecou do nivel 1 (o checkpoint nao deu certo).");
        return;
    }
    memcpy(base + 105, N, 7);
    memcpy(base + 126, N, 7);
    float k = 0.1f * (float)(g_vgRes.level < 13 ? g_vgRes.level : 13);
    memcpy(base + 247, &k, 4);
    *(volatile int*)(0xA48960 + 51 * 4) = g_vgRes.level;
    *(volatile int*)(0xA49960 + 8223 * 4) = g_vgRes.kills;
    g_vgRes.backupLeft = g_vgRes.backup; g_vgRes.tBackup = 0;
    GarHelp("Vigilante retomado no nivel %d, com %d kills.", g_vgRes.level + 1, g_vgRes.kills);
    Log("checkpoint do Vigilante: missao retomada no nivel %d, %d kills (reforco a chamar: %d)", g_vgRes.level + 1, g_vgRes.kills, g_vgRes.backupLeft);
}
static void Vig64Tick(DWORD now) {
    if (VigLevel() < 64) { g_vg64Can = false; return; }
    static DWORD tCan = 0;
    if (now - tCan >= 500 || now < tCan) { tCan = now; const char* why; g_vg64Can = !g_vg.active && Vig64CanStart(&why); }
    // botao de submissao em veiculo que nao tem submissao propria (os outros ficam como no jogo); no overlay 3.7 e o
    // botao Vigilante
    char* v = PlayerVehicle();
    static bool was = false;
    if (v && !g_vg.active && !MenuActive() && !NewButtons()) {
        int mode = *(volatile int*)(0xA49960 + 13 * 4);
        bool down = false; { Cmd c(0x00E1); c.I(0).I(mode == 3 ? 14 : 19); down = RunCmd(c); }
        bool edge = down && !was; was = down;
        if (edge) {
            int m = Rd<short>(v, 0x22), pr = PlayerRef();
            bool own = m == 416 || m == 407 || m == 425 || m == 609;
            if (!own && pr >= 0) { Cmd c(0x0602); c.I(pr); own = RunCmd(c); }   // taxi
            if (!own && pr >= 0) { Cmd c(0x056C); c.I(pr); own = RunCmd(c); }   // policia: o jogo comeca
            const char* why = nullptr;
            if (!own && Vig64CanStart(&why)) Vig64Start("pelo botao de submissao");
            else if (!own && why) GarHelp("%s", why);
        }
    } else was = false;
    if (g_vg64Help && !g_vg.active && g_nTg > 0) {
        static DWORD tH = 0;
        if (now - tH >= 1000 || now < tH) { tH = now; VgStreetCops(now, true); }
    }
}

// botoes do overlay 3.7 (ComputeInfo, a cada quadro)
static DWORD VigButtonState() {
    DWORD st = 0;
    if (!NewButtons()) return 0;
    if (g_vg.active) {
        DWORD now = GameMs();
        VgOneClockFix(now);
        st |= S_VIG;
        if (VgBackupReady(now)) st |= S_VIGBACKUP;
        if (VgCarReady(now)) st |= S_VIGCAR;
    }
    if (g_rcCanRecruit) st |= S_RECRUITCOP;
    if (VigLevel() >= 64 && (g_vg.active || g_vg64Can)) st |= S_VIG64;
    return st;
}

// ---- policiais femininas (SA-MP 0.3.7: wfyclpd, vbfycpd, wfycllv)
// O SA 1.0 guarda os modelos de pedestre num CStore de 278 (0xB478F8; o jogo usa 276). Com Female=1 o mod troca
// esse CStore por um de PedLimit, antes de o jogo ler os IDE (8 referencias em CModelInfo, 0x4C6518 a 0x4C6859).
// As linhas Ped1..Ped8 do INI entram como linhas do peds.ide (CFileLoader::LoadPedObject 0x5B7420), no fim do
// peds.ide. Os .dff e .txd vao no modloader. Sem os arquivos o modelo existe mas nao carrega: o mod nao usa.
// Rotacao na rua: CStreaming::ms_aDefaultCopModel (0x8A5AA0: interior, LS, SF, LV, moto) recebe a policial da
// regiao atual em FemaleChance% dos sorteios (a cada 45 s), so com o modelo ja carregado.
static int   g_fcOn = 1, g_fcChance = 40, g_fcStreet = 1, g_fcNPed = 0, g_fcMax = 350;
static char  g_fcName[4][24] = { "wfycllv", "wfyclpd", "vbfycpd", "wfycllv" };   // interior, LS, SF, LV
static char  g_fcPed[8][160];
static int   g_fcModel[4] = { 0, 0, 0, 0 };
static struct PedStore { int count; BYTE items[400][0x44]; } g_pedStore;
static volatile int* g_pedCount = (volatile int*)0xB478F8;
static bool  g_fcRegistered = false;
static bool FemUsable(int m) {
    return m > 0 && m < 20000 && ((char**)0xA9B0C8)[m] && *(volatile DWORD*)(0x8E4CC0 + m * 0x14 + 0xC) != 0;   // CdSize
}
static int FemCopModel(int level) {
    if (!g_fcOn || level < 0 || level > 3) return -1;
    if (g_fcModel[level] <= 0) { int m = ModelByName(g_fcName[level]); if (m > 0) g_fcModel[level] = m; }
    return FemUsable(g_fcModel[level]) ? g_fcModel[level] : -1;
}
static void MovePedStore() {
    if (*(volatile int*)0xB478F8 != 0) { Log("policiais: o jogo ja tinha modelos de pedestre; o limite ficou em 278"); return; }
    if (g_fcMax > 400) g_fcMax = 400;
    if (g_fcMax <= 278) return;
    DWORD cnt = (DWORD)&g_pedStore.count, items = (DWORD)&g_pedStore.items[0][0];
    struct { DWORD at, orig, repl; } p[] = {
        { 0x4C6518, 0xB478F8, cnt }, { 0x4C6523, 0xB478FC, items }, { 0x4C652F, 0xB478F8, cnt }, { 0x4C660A, 0xB478F8, cnt },
        { 0x4C67A1, 0xB478F8, cnt }, { 0x4C67AD, 0xB478FC, items }, { 0x4C67B3, 0xB478F8, cnt }, { 0x4C6859, 0xB478F8, cnt } };
    for (auto& x : p) if (*(DWORD*)x.at != x.orig) { Log("policiais: codigo em 0x%X nao e o original; o limite de pedestres ficou em 278", x.at); return; }
    for (int i = 0; i < g_fcMax; i++) ((void(__thiscall*)(void*))0x4C57A0)(g_pedStore.items[i]);   // CPedModelInfo::CPedModelInfo
    g_pedStore.count = 0;
    for (auto& x : p) {
        DWORD old; VirtualProtect((void*)x.at, 4, PAGE_EXECUTE_READWRITE, &old);
        *(DWORD*)x.at = x.repl;
        VirtualProtect((void*)x.at, 4, old, &old);
    }
    FlushInstructionCache(GetCurrentProcess(), (void*)0x4C6500, 0x400);
    g_pedCount = &g_pedStore.count;
    Log("policiais: limite de modelos de pedestre de 278 para %d", g_fcMax);
}
static int PedLimit() { return g_pedCount == (volatile int*)0xB478F8 ? 278 : g_fcMax; }
static void FcRegister(const char* when) {
    if (g_fcRegistered) return;
    g_fcRegistered = true;
    int ok = 0;
    for (int i = 0; i < g_fcNPed; i++) {
        char line[160]; strncpy(line, g_fcPed[i], sizeof line - 1); line[sizeof line - 1] = 0;
        for (char* c = line; *c; c++) if (*c == ',' || *c == '\t') *c = ' ';
        int id = atoi(line);
        if (id <= 0 || id >= 20000) continue;
        if (((char**)0xA9B0C8)[id]) { Log("policiais: o modelo %d ja existe; a linha Ped%d fica de fora", id, i + 1); continue; }
        if (*g_pedCount >= PedLimit()) { Log("policiais: sem espaco para mais pedestres (%d de %d)", *g_pedCount, PedLimit()); break; }
        ((int(__cdecl*)(const char*))0x5B7420)(line);                          // CFileLoader::LoadPedObject
        ok++;
    }
    Log("policiais: %d modelos de policial registrados (%s); pedestres: %d de %d", ok, when, *g_pedCount, PedLimit());
}
typedef void (__cdecl* LoadObjTypes_t)(const char*);
static LoadObjTypes_t g_origLoadObjTypes = nullptr;
static bool StrHas(const char* s, const char* sub) {
    for (; s && *s; s++) { int i = 0; while (sub[i] && tolower((unsigned char)s[i]) == sub[i]) i++; if (!sub[i]) return true; }
    return false;
}
static void __cdecl LoadObjTypesDetour(const char* file) {
    if (!g_fcRegistered && file && StrHas(file, "\\maps\\")) FcRegister("antes dos mapas");
    g_origLoadObjTypes(file);
    if (!g_fcRegistered && file && StrHas(file, "peds.ide")) FcRegister("depois do peds.ide");
}
static void ApplyFemaleCops() {
    if (!g_fcOn || !g_fcNPed) return;
    MovePedStore();
    if (MH_CreateHook((void*)0x5B8400, (void*)&LoadObjTypesDetour, (void**)&g_origLoadObjTypes) == MH_OK && MH_EnableHook((void*)0x5B8400) == MH_OK)
        Log("policiais: gancho no carregamento dos IDE (CFileLoader::LoadObjectTypes 0x5B8400)");
    else Log("policiais: falha no gancho dos IDE; as policiais femininas ficam de fora");
}
static int   g_fcOrig[4] = { 283, 280, 281, 282 };
static bool  g_fcOrigRead = false;
static int   g_fcWant[4] = { 0, 0, 0, 0 };
static DWORD g_fcRollT = 0;
static void CopRotTick(DWORD now) {
    if (!g_fcOn || !g_fcStreet) return;
    static DWORD tT = 0;
    if (now - tT < 1000 && now >= tT) return;
    tT = now;
    volatile int* arr = (volatile int*)0x8A5AA0;                              // CStreaming::ms_aDefaultCopModel
    if (!g_fcOrigRead) {
        g_fcOrigRead = true;
        for (int l = 0; l < 4; l++) if (arr[l] > 0 && arr[l] < 20000) g_fcOrig[l] = arr[l];
        int n = 0; for (int l = 0; l < 4; l++) if (FemCopModel(l) > 0) n++;
        Log("policiais: policial feminina na rua em %d de 4 regioes (%s/%s/%s/%s)", n,
            FemCopModel(0) > 0 ? "interior" : "-", FemCopModel(1) > 0 ? "LS" : "-", FemCopModel(2) > 0 ? "SF" : "-", FemCopModel(3) > 0 ? "LV" : "-");
    }
    if (!g_fcRollT || now - g_fcRollT > 45000 || now < g_fcRollT) {
        g_fcRollT = now;
        for (int l = 0; l < 4; l++) g_fcWant[l] = (rand() % 100) < g_fcChance;
    }
    int cur = *(volatile int*)0xBA6718;                                       // CTheZones::m_CurrLevel
    for (int l = 0; l < 4; l++) {
        int fem = FemCopModel(l);
        int want = (l == cur && g_fcWant[l] && fem > 0) ? fem : g_fcOrig[l];
        if (arr[l] == want) continue;
        if (l == cur && !ModelLoaded(want)) { RequestModel(want, 2); continue; }   // so troca com o modelo ja carregado
        arr[l] = want;
        static DWORD tLog = 0;
        if (GetTickCount() - tLog > 60000) { tLog = GetTickCount(); Log("policiais: na rua agora %s (regiao %d)", want == fem ? "policiais femininas" : "o policial de sempre", l); }
    }
}
// motorista da garagem no nivel 64: policial feminina da regiao (ou o policial da regiao)
static int GarageCopDriver(V3 at, int* pedType) {
    if (VigLevel() < 64) return -1;
    int level = 1;
    { Cmd c(0x07EF); c.F(at.x).F(at.y).F(at.z).Out(0); RunCmd(c, &level, 1); }
    if (level < 0 || level > 3) level = 1;
    int fem = FemCopModel(level);
    if (fem > 0) { *pedType = 5; return fem; }
    static const int kPed[4] = { 283, 280, 281, 282 };
    *pedType = 4;
    return kPed[level];
}

static void PsApply();
static void RewardsOnLoad() {                                                // jogo carregado: os pedestres sao outros
    PsApply();
    g_nRc = 0; g_nTg = 0; g_rcCand = -1; g_tgMeHp = -1.f; g_tgCar = nullptr;
    memset(g_tgId, 0xFF, sizeof g_tgId);
}
// ---- tique das recompensas (depois do Vigilante)
static void RewardsTick() {
    DWORD now = GameMs();
    if (!PlayerPed()) return;
    TgTick(now);
    RcTick(now);
    Vig64Tick(now);
    CopRotTick(now);
    AimTick(GetTickCount());
    FwTick();
    CwReloadTick();
    CwDdbTick();
    static bool psDone = false;
    if (!psDone) { psDone = true; PsApply(); }
}

// Trip Skip: o jogo desenha a caixa (CHud::Draw 0x58FC6A -> 0x58A160); enquanto ela aparece, o Sim fica na tela
static void __cdecl TripSkipDraw() { g_tripSkipTick = GetTickCount(); ((void(__cdecl*)())0x58A160)(); }
static void ApplyVigilante() {
    PatchCallSite(0x58FC6A, 0x58A160, (void*)&TripSkipDraw, "trip skip: Sim na tela enquanto a caixa aparece");
}

// ---- transito: nenhum carro surge logo a frente do seu carro (3.33) ----
// O jogo cria carro aleatorio em dois aneis em volta da camera: no anel de longe so se o ponto estiver na tela, no de
// perto so se estiver FORA da tela (CCarCtrl::GenerateCarCreationCoors2, 0x424210, teste do anel de perto em 0x42498E).
// Olhando para tras, a frente fica fora da tela e o jogo enche a pista de carros a poucos metros. Aqui, nesse teste,
// um ponto na faixa a frente do seu carro conta como "na tela": o jogo nao cria carro ali e cria em outro lugar.
// A faixa vai ate a distancia que o carro percorre em Seconds (entre MinDistance e MaxDistance) e abre com a distancia.
static int   g_trOn = 1;
static float g_trSecs = 4.f, g_trMin = 40.f, g_trMax = 160.f, g_trWidth = 12.f;
static DWORD g_trCount = 0, g_trLogAt = 0;
typedef bool (__thiscall* SphereVis_t)(void* cam, const float* pos, float r);
static bool __thiscall TrSpawnVisible(void* cam, const float* pos, float r) {
    bool vis = ((SphereVis_t)0x420D40)(cam, pos, r);                        // CCamera::IsSphereVisible
    if (vis || !g_trOn || !pos) return vis;
    char* v = PlayerVehicle();
    if (!v) return false;
    const float* mv = (const float*)(v + 0x44);                               // m_vecMoveSpeed (x50 = m/s)
    float sx = mv[0] * 50.f, sy = mv[1] * 50.f, sp = sqrtf(sx * sx + sy * sy);
    float fx, fy;
    if (sp > 2.f) { fx = sx / sp; fy = sy / sp; }                             // para onde o carro anda
    else {
        const float* m = Rd<const float*>(v, 0x14);                           // parado: para onde ele aponta
        if (!m) return false;
        fx = m[4]; fy = m[5];
        float l = sqrtf(fx * fx + fy * fy);
        if (l < 0.01f) return false;
        fx /= l; fy /= l;
    }
    V3 c = EntityPos(v);
    float dx = pos[0] - c.x, dy = pos[1] - c.y;
    float ahead = dx * fx + dy * fy, side = fabsf(dy * fx - dx * fy);
    float reach = sp * g_trSecs;
    if (reach < g_trMin) reach = g_trMin;
    if (reach > g_trMax) reach = g_trMax;
    if (ahead < -5.f || ahead > reach || side > g_trWidth + ahead * 0.35f) return false;
    g_trCount++;
    DWORD now = GetTickCount();
    if (now - g_trLogAt > 60000) {
        Log("transito: %u carros deixaram de surgir a sua frente (o ultimo a %.0f m, a %.0f km/h)", g_trCount, ahead, sp * 3.6f);
        g_trLogAt = now; g_trCount = 0;
    }
    return true;
}
static void ApplyTraffic() {
    PatchCallSite(0x42498E, 0x420D40, (void*)&TrSpawnVisible, "transito: sem carro surgindo logo a frente");
}
static void ApplyProtect() {
    int ok = 0;
    ok += PatchCallSite(0x47D335, 0x5E6080, (void*)&PmGive,     "armas: GIVE_WEAPON_TO_CHAR (01B2)");
    ok += PatchCallSite(0x48D696, 0x5E62B0, (void*)&PmClear,    "armas: REMOVE_WEAPON_FROM_CHAR (0555)");
    ok += PatchCallSite(0x48AE86, 0x5E6320, (void*)&PmClearAll, "armas: REMOVE_ALL_CHAR_WEAPONS (048F)");
    ok += PatchCallSite(0x47CDF1, 0x5DF290, (void*)&PmSetAmmo,  "armas: SET_CHAR_AMMO (017B)");
    ok += PatchCallSite(0x48AEAF, 0x404910, (void*)&PmPedAt,    "armas: HAS_CHAR_GOT_WEAPON (0491)");
    ok += PatchCallSite(0x480233, 0x404910, (void*)&PmPedAt,    "armas: IS_CURRENT_CHAR_WEAPON (02D8)");
    ok += PatchCallSite(0x47D4BD, 0x404910, (void*)&PmPedAt,    "armas: SET_CURRENT_CHAR_WEAPON (01B9)");
    ok += PatchCallSite(0x489B0B, 0x404910, (void*)&PmPedAt,    "armas: GET_AMMO_IN_CHAR_WEAPON (041A)");
    if (ok < 8) Log("armas: %d de 8 ganchos aplicados", ok);
}

// ---------------------------------------------------------------- giroscopio ([Gyro] Enabled=1; overlay 3.4)
// O overlay le o giroscopio do iPhone (100 vezes por segundo, ja suavizado nos movimentos bem pequenos) e escreve
// em [96..127] do arquivo compartilhado o quanto o telefone girou desde o inicio (microrradianos, para a direita e
// para cima) e os ajustes do painel. Aqui, a cada quadro, a diferenca gira a camera ativa do jeito que o mouse faz:
// soma no angulo horizontal (CCam +0xBC, para a direita diminui) e no vertical (+0xAC, para cima aumenta), que sao
// os mesmos campos em que o codigo do mouse do jogo soma (0x523F55, 0x525648, 0x510BA6). Vale em qualquer
// configuracao de controle. So gira a camera: o personagem e o carro nao recebem nada.
//  - mira (sempre): mira livre (53), M4/AK (34), rifle (7), lanca-foguetes (8, 51), camera (46), canhao do
//    helicoptero (45), mira de dentro do carro (55) e as variantes "runabout" (39-42, 52);
//  - fora da mira (opcao do painel): a pe (4), carro e moto (18), barco (22).
// Com zoom, o giro diminui na mesma proporcao do zoom (tan(FOV/2)), para a mira andar o mesmo tanto na tela.
//
// Fora da mira, a camera do jogo volta sozinha para tras (a pe, CCam::Process_FollowPed_SA 0x522D40; no carro,
// Process_FollowCar_SA 0x5245B0). A camera a pe "do mouse" (0x50F970) nem e usada na 1.0: CCam::Process so a escolhe
// com 0x8CCF00 == 0, e ele vem 1 do executavel. Entao o plugin segura a camera ele mesmo: depois que o giroscopio
// mexe, a cada quadro os angulos voltam para onde ele deixou (e as velocidades zeram). A pe, o angulo fica fixo;
// no carro, fica fixo em relacao ao carro (acompanha as curvas). No carro, a leitura de m_bUseMouse3rdPerson
// (0xB6EC2E) que liga o "segurar" do mouse (0x525615) le um byte nosso enquanto segura, para o jogo nao brigar.
// Solta: o analogico direito (a pe e no carro), mirar, Olhar para os lados ou para tras (gatilhos, no carro),
// Centralizar (a pe), sair ou entrar no veiculo, cutscene, controle desligado por script ou teletransporte.
// "Centralizar" (comando 5 do overlay), a pe: a camera volta para tras do CJ, na altura de antes, em ~0,2 s.
#pragma pack(push, 1)
struct GyroBlock { DWORD magic, seq, yaw, pitch; WORD sensH, sensV, aimPct; BYTE flags, pad0; BYTE pad[8]; };
#pragma pack(pop)
static_assert(sizeof(GyroBlock) == 32, "layout gyro");
static const DWORD GYRO_MAGIC = 0x594D5447;                                   // "GTMY"
enum { GY_ON = 1, GY_ALWAYS = 2, GY_INVERT = 4, GY_ZOOM = 8, GY_RUN = 16 };
static int   g_gyroOn = 1;
static bool  g_gyHave = false;
static DWORD g_gyPrevYaw = 0, g_gyPrevPitch = 0, g_gyCfgLogged = 0xFFFFFFFF;
static unsigned long long g_gyModesLogged = 0;
static BYTE  g_camMouse = 0;                                                  // lido no lugar de 0xB6EC2E em 0x525615
static bool  g_camPatched = false, g_carHold = false, g_footHold = false, g_restAlphaOk = false;
static float g_carOffset = 0.f, g_carAlpha = 0.f, g_footBeta = 0.f, g_footAlpha = 0.f, g_restAlpha = 0.f;
static V3    g_footPos = {};
static float g_gyMoved = 0.f;                                                 // giro desde a ultima soltura (para nao segurar por ruido)
static DWORD g_holdLogT = 0;
static DWORD g_recenterUntil = 0;
static void ApplyGyroCam() {
    g_camMouse = *(volatile BYTE*)0xB6EC2E;
    static const BYTE orig[] = { 0xA0, 0x2E, 0xEC, 0xB6, 0x00 };             // mov al, [CCamera::m_bUseMouse3rdPerson]
    BYTE r[5]; r[0] = 0xA0; *(DWORD*)(r + 1) = (DWORD)&g_camMouse;
    g_camPatched = PatchBytes(0x525615, orig, sizeof orig, r, sizeof r, "giroscopio: camera do carro que segura (Process_FollowCar_SA)");
}
static float WrapPi(float d) {                                               // -pi..pi; valor invalido vira 0
    if (!isfinite(d)) return 0.f;
    if (d >= -3.14159265f && d <= 3.14159265f) return d;
    d = fmodf(d + 3.14159265f, 6.2831853f);
    if (d < 0.f) d += 6.2831853f;
    return d - 3.14159265f;
}
static float EntityHeading(const char* e) {                                  // direcao da frente no chao (x, y)
    const float* m = Rd<const float*>(e, 0x14);
    if (m) return atan2f(m[5], m[4]);
    return Rd<float>(e, 0x10) + 1.5707963f;
}
static void DoRecenter() {
    if (g_carHold) { g_carHold = false; *(float*)0xB70118 = 0.f; Log("giroscopio: camera do carro solta (centralizar)"); }
    if (g_footHold) { g_footHold = false; Log("giroscopio: camera a pe solta (centralizar)"); }
    g_gyMoved = 0.f;
    g_recenterUntil = GetTickCount() + 600;
}
static bool ReadGyro(GyroBlock* b) {
    if (g_file == INVALID_HANDLE_VALUE) return false;
    EnterCriticalSection(&g_io);
    OVERLAPPED o = {}; o.Offset = 96; DWORD n = 0;
    BOOL ok = ReadFile(g_file, b, sizeof *b, &n, &o);
    LeaveCriticalSection(&g_io);
    return ok && n == sizeof *b && b->magic == GYRO_MAGIC;
}
static bool IsAimCam(int m) {
    switch (m) { case 7: case 8: case 34: case 39: case 40: case 41: case 42: case 45: case 46: case 51: case 52: case 53: case 55: return true; }
    return false;
}
static bool IsFreeCam(int m) { return m == 4 || m == 18 || m == 22; }
static void CamTick(const GyroBlock* b) {                                     // a cada quadro, antes do giro novo
    HostBlock hb;                                                             // sensor parado um instante (pausa): vale o overlay vivo
    bool gyroFree = b && (b->flags & GY_ON) && (b->flags & GY_ALWAYS) && ((b->flags & GY_RUN) || ReadHost(&hb));
    char* me = PlayerPed();
    int active = *(volatile BYTE*)0xB6F081;
    char* cam = active <= 2 ? (char*)0xB6F19C + active * 0x238 : nullptr;
    int mode = cam ? Rd<short>(cam, 0xC) : -1;
    bool gun = false;
    if (me) { char* intel = Rd<char*>(me, 0x47C); gun = intel && GetTaskUseGun(intel); }
    char* veh = PlayerVehicle();
    const short* pad = (const short*)0xB73458;                               // CPad 0, NewState
    bool stick = abs(pad[2]) > 16 || abs(pad[3]) > 16;                       // analogico direito (o toque do lado direito)
    bool scripted = Rd<WORD>((void*)0xB73458, 0x10E) != 0 || *(volatile signed char*)0xB5F851;
    // a pe: volta para onde o giroscopio deixou
    if (g_footHold) {
        V3 p = me ? EntityPos(me) : V3{};
        const char* why = !gyroFree ? "giroscopio fora da mira desligado" : !cam || mode != 4 ? "outra camera" : veh ? "entrou no veiculo" :
                          gun ? "mirando" : stick ? nullptr : scripted ? "cena do jogo" : Dist2(p, g_footPos) > 15.f ? "teletransporte" : nullptr;
        if (why || stick) {
            g_footHold = false; g_gyMoved = 0.f;
            if (why) Log("giroscopio: camera a pe solta (%s)", why);
        } else {
            *(float*)(cam + 0xBC) = g_footBeta; *(float*)(cam + 0xAC) = g_footAlpha;
            *(float*)(cam + 0xC0) = 0.f; *(float*)(cam + 0xB0) = 0.f;          // BetaSpeed, AlphaSpeed
            g_footPos = p;
        }
    }
    // carro: fica virada em relacao ao carro
    if (g_carHold) {
        bool trig = pad[5] > 100 || pad[7] > 100;                            // LeftShoulder2 / RightShoulder2: olhar
        const char* why = !gyroFree ? "giroscopio fora da mira desligado" : !veh ? "saiu do veiculo" : !cam || (mode != 18 && mode != 22) ? "outra camera" :
                          trig ? "olhar" : stick ? "analogico" : scripted ? "cena do jogo" : nullptr;
        if (why) {
            g_carHold = false; g_gyMoved = 0.f;
            *(float*)0xB70118 = 0.f;                                          // sem o resto do segurar: volta ja
            Log("giroscopio: camera do carro solta (%s)", why);
        } else {
            *(float*)(cam + 0xBC) = WrapPi(EntityHeading(veh) + g_carOffset); *(float*)(cam + 0xAC) = g_carAlpha;
            *(float*)(cam + 0xC0) = 0.f; *(float*)(cam + 0xB0) = 0.f;
            *(float*)0xB70118 = 50.f;
        }
    }
    g_camMouse = (*(volatile BYTE*)0xB6EC2E || (g_carHold && g_camPatched)) ? 1 : 0;
    // centralizar a pe: volta para tras do CJ (frente da camera = -cos/sin do Beta) e para a altura de antes
    if (g_recenterUntil) {
        if (GetTickCount() > g_recenterUntil || !me || !cam || mode != 4 || veh) { g_recenterUntil = 0; g_restAlphaOk = false; }
        else {
            float target = EntityHeading(me) + 3.14159265f;                    // Beta tal que (-cos B, -sin B) = frente do CJ
            float d = WrapPi(target - Rd<float>(cam, 0xBC));
            *(float*)(cam + 0xBC) += d * 0.35f;
            if (g_restAlphaOk) *(float*)(cam + 0xAC) += (g_restAlpha - Rd<float>(cam, 0xAC)) * 0.35f;
            if (fabsf(d) < 0.01f) { g_recenterUntil = 0; g_restAlphaOk = false; }
        }
    }
}
// ---------------------------------------------------------------- trocar de alvo na mira automatica ([Gyro])
// Com um alvo travado (CPed +0x71C, a mira automatica do controle), girar o iPhone TargetSwitchDegrees graus para um
// lado, ou dar um toque no analogico direito para o lado, troca para o proximo alvo daquele lado: a mesma chamada que
// o jogo faz com L2/R2 (CPlayerPed::FindNextWeaponLockOnTarget 0x60E530, chamada em 0x686A86 e 0x686C8D).
typedef bool (__thiscall* FindNextLock_t)(void* ped, void* target, bool left);
static float g_tsAcc = 0.f; static void* g_tsTarget = nullptr; static DWORD g_tsLast = 0; static bool g_tsStickArmed = true;
static void TargetSwitch(bool left, const char* how) {
    char* me = PlayerPed();
    void* t = me ? Rd<void*>(me, 0x71C) : nullptr;
    if (!t) return;
    bool ok = ((FindNextLock_t)0x60E530)(me, t, left);
    g_tsLast = GetTickCount(); g_tsAcc = 0.f; g_tsTarget = me ? Rd<void*>(me, 0x71C) : nullptr;
    static int logged = 0;
    if (logged < 6) { logged++; Log("mira: troca de alvo para a %s (%s)%s", left ? "esquerda" : "direita", how, ok ? "" : ", nao ha outro alvo desse lado"); }
}
static bool TargetLocked() {
    char* me = PlayerPed();
    return me && !PlayerVehicle() && Rd<void*>(me, 0x71C) && !MenuActive();
}
static void TargetGyro(int dyMicro) {                                         // giro do iPhone (microrradianos, + = direita)
    if (g_tsDeg <= 0 || !TargetLocked()) { g_tsAcc = 0.f; g_tsTarget = nullptr; return; }
    void* t = Rd<void*>(PlayerPed(), 0x71C);
    if (t != g_tsTarget) { g_tsTarget = t; g_tsAcc = 0.f; }
    if (GetTickCount() - g_tsLast < 250) { g_tsAcc = 0.f; return; }
    g_tsAcc += dyMicro * 1e-6f;
    float thr = g_tsDeg * 0.017453293f;
    if (g_tsAcc >= thr) TargetSwitch(false, "giroscopio");
    else if (g_tsAcc <= -thr) TargetSwitch(true, "giroscopio");
}
static void TargetStickTick() {                                               // toque no analogico direito
    if (!g_tsStick) return;
    int rx = g_touchRX;
    if (!TargetLocked()) { g_tsStickArmed = rx > -9000 && rx < 9000; return; }
    if (rx > -9000 && rx < 9000) { g_tsStickArmed = true; return; }
    if (!g_tsStickArmed || (rx > -20000 && rx < 20000)) return;
    g_tsStickArmed = false;
    TargetSwitch(rx < 0, "analogico");
}
// Volante sem o "mouse": com o controle do jogo em Mouse + Teclado (CCamera::m_bUseMouse3rdPerson) e "Volante com o
// mouse" ligado, o carro (0x6AD7AC), a moto (0x6BE39C), 0x6CE03D, o barco (0x6F0AFA) e os aviao/helicoptero ("Voar com
// o mouse", 0x6C4880 e 0x6CAE07) vao ate o lado das setas aos poucos, como se fosse mouse (atraso no volante). Aqui
// essas leituras passam a ler um byte nosso que vale sempre 0 (mov al,[x] -> mov al,[g_noMouseFlag]): o veiculo sempre
// usa o controle direto. O menu nao muda. O endereco continua sendo um ponteiro valido: o SilentPatch le o operando de
// 0x6AD7AC (o ponteiro da opcao) para usar depois; com "xor al,al" ele lia 0x909090C0 e o jogo fechava dirigindo (3.29).
static volatile BYTE g_noMouseFlag = 0;
static void ApplyNoMouseSteer() {
    static const DWORD site[6] = { 0x6AD7AC, 0x6BE39C, 0x6CE03D, 0x6F0AFA, 0x6C4880, 0x6CAE07 };
    static const DWORD flag[6] = { 0xC1CC02, 0xC1CC02, 0xC1CC02, 0xC1CC02, 0xC1CC03, 0xC1CC03 };
    int ok = 0;
    for (int i = 0; i < 6; i++) {
        BYTE exp[5] = { 0xA0 }; *(DWORD*)(exp + 1) = flag[i];
        BYTE rep[5] = { 0xA0 }; *(DWORD*)(rep + 1) = (DWORD)&g_noMouseFlag;
        char what[64]; snprintf(what, sizeof what, "volante sem mouse (%s)", i < 4 ? "dirigir" : "voar");
        ok += PatchBytes(site[i], exp, 5, rep, 5, what);
    }
    if (ok < 6) Log("volante sem mouse: %d de 6 pontos aplicados", ok);
}
static void GyroTick() {
    if (!g_gyroOn || !g_touchOn) { CamTick(nullptr); return; }
    GyroBlock b;
    bool okb = ReadGyro(&b);
    CamTick(okb ? &b : nullptr);
    if (!okb || !(b.flags & GY_ON) || !(b.flags & GY_RUN)) { g_gyHave = false; return; }
    DWORD cfg = (DWORD)b.sensH | ((DWORD)b.sensV << 10) | ((DWORD)b.aimPct << 20) | ((DWORD)(b.flags & 0xF) << 28);
    if (cfg != g_gyCfgLogged) {
        g_gyCfgLogged = cfg;
        Log("giroscopio: horizontal %.2fx, vertical %.2fx, na mira %u%%, fora da mira %s, inverter %s, zoom %s (mouse na 3a pessoa do jogo: %s)",
            b.sensH / 100.f, b.sensV / 100.f, (unsigned)b.aimPct, (b.flags & GY_ALWAYS) ? "sim" : "nao", (b.flags & GY_INVERT) ? "sim" : "nao",
            (b.flags & GY_ZOOM) ? "sim" : "nao", *(volatile BYTE*)0xB6EC2E ? "sim" : "nao");
    }
    if (!g_gyHave) { g_gyPrevYaw = b.yaw; g_gyPrevPitch = b.pitch; g_gyHave = true; return; }
    int dy = (int)(b.yaw - g_gyPrevYaw), dp = (int)(b.pitch - g_gyPrevPitch);
    g_gyPrevYaw = b.yaw; g_gyPrevPitch = b.pitch;
    if (dy > -600000 && dy < 600000) TargetGyro(dy);
    if (!dy && !dp) return;
    if (dy > 600000 || dy < -600000 || dp > 600000 || dp < -600000) return;   // salto (overlay reiniciou)
    if (!PlayerPed() || *(volatile signed char*)0xB5F851) return;             // sem CJ / cutscene
    if (Rd<WORD>((void*)0xB73458, 0x10E)) return;                             // CPad 0: controles desligados por script
    int active = *(volatile BYTE*)0xB6F081;                                   // TheCamera.m_nActiveCam
    if (active > 2) return;
    char* cam = (char*)0xB6F19C + active * 0x238;                              // TheCamera.m_aCams[ativa]
    int mode = Rd<short>(cam, 0xC);
    bool aim = IsAimCam(mode), free = IsFreeCam(mode);
    if (!aim && mode == 4) {                                                  // mira com controle sem alvo: camera a pe, arma em uso
        char* intel = Rd<char*>(PlayerPed(), 0x47C);                          // CPed::m_pIntelligence
        if (intel && GetTaskUseGun(intel)) aim = true;
    }
    if (!aim && !(free && (b.flags & GY_ALWAYS))) return;
    float sh = b.sensH / 100.f, sv = b.sensV / 100.f;
    if (aim) { float a = b.aimPct / 100.f; sh *= a; sv *= a; }
    float k = 1.f;
    if (b.flags & GY_ZOOM) {
        float fov = Rd<float>(cam, 0xB4);
        if (fov > 1.f && fov < 150.f) { k = tanf(fov * 0.008726646f) / 0.7002075f; if (k < 0.05f) k = 0.05f; if (k > 2.f) k = 2.f; }
    }
    float yaw = dy * 1e-6f * sh * k, pitch = dp * 1e-6f * sv * k;
    if (b.flags & GY_INVERT) pitch = -pitch;
    if (free && mode == 4 && !g_restAlphaOk && !g_recenterUntil) { g_restAlpha = Rd<float>(cam, 0xAC); g_restAlphaOk = true; }
    *(float*)(cam + 0xBC) -= yaw;                                             // Beta: frente = (-cos B, -sin B): para a direita diminui
    *(float*)(cam + 0xAC) += pitch;                                           // Alpha: frente.z = sin A (0x524206): para cima aumenta
    // fora da mira: depois de um giro de verdade (mais de ~1,7 grau, nao o ruido do sensor), a camera fica onde ele deixou
    if (free && !aim) {
        g_gyMoved += fabsf(yaw) + fabsf(pitch);
        bool engage = g_gyMoved > 0.03f;
        DWORD now = GetTickCount();
        if (mode == 4 && !PlayerVehicle() && !g_recenterUntil && (g_footHold || engage)) {
            if (!g_footHold && now - g_holdLogT > 10000) { g_holdLogT = now; Log("giroscopio: camera a pe fica onde voce virou (analogico, mirar ou Centralizar soltam)"); }
            g_footHold = true; g_footBeta = WrapPi(Rd<float>(cam, 0xBC)); g_footAlpha = Rd<float>(cam, 0xAC); g_footPos = EntityPos(PlayerPed());
        }
        if ((mode == 18 || mode == 22) && PlayerVehicle() && (g_carHold || engage)) {
            if (!g_carHold && now - g_holdLogT > 10000) { g_holdLogT = now; Log("giroscopio: camera do carro fica virada em relacao a ele (gatilhos, analogico ou sair soltam)"); }
            g_carHold = true; g_carOffset = WrapPi(Rd<float>(cam, 0xBC) - EntityHeading(PlayerVehicle())); g_carAlpha = Rd<float>(cam, 0xAC);
        }
    }
    if (mode < 64 && !(g_gyModesLogged & (1ull << mode))) {
        g_gyModesLogged |= 1ull << mode;
        Log("giroscopio: girando a camera no modo %d (%s)", mode, aim ? "mira" : "fora da mira");
    }
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
    if (g_garOn || g_armOn) InstallGarage();                                  // depois do MH_Initialize
    InstallCrimeHooks();
    if (g_daOn) InstallDriveByAim();
    InstallCarWeapons();
    InstallBikeDriveBy();
    if (g_daOn || (g_cwOn && g_cwBikeAnim)) InstallFihfc2();
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
    g_noMouseSteer = GetPrivateProfileIntA("Steer", "NoMouseSteering", 1, ini);
    g_tsDeg     = GetPrivateProfileIntA("Gyro", "TargetSwitchDegrees", 10, ini);
    g_tsStick   = GetPrivateProfileIntA("Gyro", "TargetSwitchStick",    1, ini);
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
    {
        char fb[32];
        g_trOn = GetPrivateProfileIntA("Transito", "Enabled", 1, ini);
        GetPrivateProfileStringA("Transito", "Seconds", "4", fb, sizeof fb, ini);     g_trSecs  = (float)atof(fb);
        GetPrivateProfileStringA("Transito", "MinDistance", "40", fb, sizeof fb, ini); g_trMin  = (float)atof(fb);
        GetPrivateProfileStringA("Transito", "MaxDistance", "160", fb, sizeof fb, ini); g_trMax = (float)atof(fb);
        GetPrivateProfileStringA("Transito", "Width", "12", fb, sizeof fb, ini);        g_trWidth = (float)atof(fb);
        if (g_trMax < g_trMin) g_trMax = g_trMin;
    }
    {
        char fb[96];
        g_vgOn      = GetPrivateProfileIntA("Vigilante", "Enabled", 1, ini);
        g_vgNoChase = GetPrivateProfileIntA("Vigilante", "PoliceIgnore", 1, ini);
        g_vgOutSec  = GetPrivateProfileIntA("Vigilante", "OutOfCarSeconds", 120, ini);
        g_vgHelpOn  = GetPrivateProfileIntA("Vigilante", "StreetCopsHelp", 1, ini);
        GetPrivateProfileStringA("Vigilante", "HelpRadius", "100", fb, sizeof fb, ini);     g_vgHelpR = (float)atof(fb);
        GetPrivateProfileStringA("Vigilante", "BackupCooldown", "60", fb, sizeof fb, ini);  g_vgCool = (float)atof(fb);
        GetPrivateProfileStringA("Vigilante", "SingleCooldown", "60", fb, sizeof fb, ini);  g_vgOneCool = (float)atof(fb);
        GetPrivateProfileStringA("Vigilante", "SpawnMin", "30", fb, sizeof fb, ini);        g_vgSpawnMin = (float)atof(fb);
        GetPrivateProfileStringA("Vigilante", "SpawnMax", "70", fb, sizeof fb, ini);        g_vgSpawnMax = (float)atof(fb);
        GetPrivateProfileStringA("Vigilante", "Models", "dsher,csher", g_vgModels, sizeof g_vgModels, ini);
        g_vgPerk     = GetPrivateProfileIntA("Vigilante", "Level12Wanted", 1, ini);
        g_vgMaxStars = GetPrivateProfileIntA("Vigilante", "MaxStars", 4, ini);
        GetPrivateProfileStringA("Vigilante", "StarSeconds", "65", fb, sizeof fb, ini);     g_vgStarSec = (float)atof(fb);
        GetPrivateProfileStringA("Vigilante", "CopRadius", "60", fb, sizeof fb, ini);       g_vgCopR = (float)atof(fb);
        g_vgHealth     = GetPrivateProfileIntA("Vigilante", "CopHealth", 300, ini);
        g_vgArmour     = GetPrivateProfileIntA("Vigilante", "CopArmour", 100, ini);
        g_vgNoAim      = GetPrivateProfileIntA("Vigilante", "NoAutoAimOnCops", 1, ini);
        g_vgBonusEvery = GetPrivateProfileIntA("Vigilante", "DoubleBonusEvery", 3, ini);
        g_vgTestLevel  = GetPrivateProfileIntA("Vigilante", "TestLevel", 0, ini);
        g_rcOn         = GetPrivateProfileIntA("Vigilante", "Recruit", 1, ini);
        g_rcMax12      = GetPrivateProfileIntA("Vigilante", "RecruitMax12", 1, ini);
        g_rcMax21      = GetPrivateProfileIntA("Vigilante", "RecruitMax21", 3, ini);
        g_vg64Help     = GetPrivateProfileIntA("Vigilante", "Level64CopsHelp", 1, ini);
        {
            char fb[32];
            GetPrivateProfileStringA("Vigilante", "RecruitTargetRadius", "35", fb, sizeof fb, ini);   g_rcTgtR = (float)atof(fb);
            GetPrivateProfileStringA("Vigilante", "RecruitReturnDistance", "40", fb, sizeof fb, ini); g_rcBackD = (float)atof(fb);
            if (g_rcTgtR < 10.f) g_rcTgtR = 10.f;
            if (g_rcBackD < 15.f) g_rcBackD = 15.f;
        }
        if (g_rcMax12 < 0) g_rcMax12 = 0;
        if (g_rcMax21 < 0) g_rcMax21 = 0;
        if (g_rcMax12 > 4) g_rcMax12 = 4;
        if (g_rcMax21 > 4) g_rcMax21 = 4;
        {
            char fb[32];
            GetPrivateProfileStringA("Vigilante", "StarSeconds64", "32", fb, sizeof fb, ini); g_vgStar64 = (float)atof(fb);
            if (g_vgStar64 < 5.f) g_vgStar64 = 5.f;
        }
        g_fcOn     = GetPrivateProfileIntA("PoliciaisFemininas", "Enabled", 1, ini);
        g_fcStreet = GetPrivateProfileIntA("PoliciaisFemininas", "Street", 1, ini);
        g_fcChance = GetPrivateProfileIntA("PoliciaisFemininas", "Chance", 40, ini);
        g_fcMax    = GetPrivateProfileIntA("PoliciaisFemininas", "PedLimit", 350, ini);
        if (g_fcChance < 0) g_fcChance = 0;
        if (g_fcChance > 100) g_fcChance = 100;
        g_fcChanceV = g_fcChance;
        GetPrivateProfileStringA("PoliciaisFemininas", "Country", "wfycllv", g_fcName[0], sizeof g_fcName[0], ini);
        GetPrivateProfileStringA("PoliciaisFemininas", "LS", "wfyclpd", g_fcName[1], sizeof g_fcName[1], ini);
        GetPrivateProfileStringA("PoliciaisFemininas", "SF", "vbfycpd", g_fcName[2], sizeof g_fcName[2], ini);
        GetPrivateProfileStringA("PoliciaisFemininas", "LV", "wfycllv", g_fcName[3], sizeof g_fcName[3], ini);
        {
            static const char* def[3] = {
                "265, wfyclpd, wfyclpd, COP, STAT_COP, woman, 1FFF, 0, null, 9, 9, PED_TYPE_GEN, VOICE_GEN_WFYBU, VOICE_GEN_WFYBU",
                "266, vbfycpd, vbfycpd, COP, STAT_COP, woman, 1FFF, 0, null, 9, 9, PED_TYPE_GEN, VOICE_GEN_BFYBU, VOICE_GEN_BFYBU",
                "267, wfycllv, wfycllv, COP, STAT_COP, woman, 1FFF, 0, null, 9, 9, PED_TYPE_GEN, VOICE_GEN_WFYST, VOICE_GEN_WFYST" };
            g_fcNPed = 0;
            for (int i = 0; i < 8; i++) {
                char key[8]; snprintf(key, sizeof key, "Ped%d", i + 1);
                GetPrivateProfileStringA("PoliciaisFemininas", key, i < 3 ? def[i] : "", g_fcPed[g_fcNPed], sizeof g_fcPed[0], ini);
                if (g_fcPed[g_fcNPed][0]) g_fcNPed++;
            }
        }
        if (g_vgHealth < 1) g_vgHealth = 100;
        if (g_vgSpawnMax < g_vgSpawnMin + 10.f) g_vgSpawnMax = g_vgSpawnMin + 10.f;
        if (g_vgOutSec > 600) g_vgOutSec = 600;
    }
    g_daOn   = GetPrivateProfileIntA("MiraDriveBy", "Enabled", 1, ini);
    g_daCars = GetPrivateProfileIntA("MiraDriveBy", "Carros", 1, ini);
    g_daCone = (float)GetPrivateProfileIntA("MiraDriveBy", "Cone", 35, ini);
    g_daConeZ = (float)GetPrivateProfileIntA("MiraDriveBy", "ConeAltura", 12, ini);
    g_cwOn   = GetPrivateProfileIntA("ArmasNoVeiculo", "Enabled", 1, ini);
    g_vgNever = GetPrivateProfileIntA("Vigilante", "NuncaProcurado", 1, ini);
    g_vgBikeShoot = GetPrivateProfileIntA("Vigilante", "PolicialDeMotoAtira", 1, ini);
    g_vgSaveOn = GetPrivateProfileIntA("Vigilante", "SalvarNaMissao", 1, ini);
    g_fwOn   = GetPrivateProfileIntA("ArmasNoVeiculo", "TiroFrente", 1, ini);
    g_fwGlass = GetPrivateProfileIntA("ArmasNoVeiculo", "Parabrisa", 1, ini);
    g_cwBikeAnim = GetPrivateProfileIntA("ArmasNoVeiculo", "AnimacaoNaMoto", 1, ini);
    g_apOn   = GetPrivateProfileIntA("MiraAPe", "PrioridadeSuspeito", 1, ini);
    g_htOn   = GetPrivateProfileIntA("MiraAPe", "HitmanProximoAlvo", 1, ini);
    g_htCops = GetPrivateProfileIntA("MiraAPe", "Policiais", 0, ini);
    g_htDelay = GetPrivateProfileIntA("MiraAPe", "Atraso", 200, ini);
    g_psOn   = GetPrivateProfileIntA("PistolaSilenciada", "Enabled", 1, ini);
    g_psClip = GetPrivateProfileIntA("PistolaSilenciada", "Pente", 12, ini);
    g_psStdFr = GetPrivateProfileIntA("PistolaSilenciada", "QuadrosIntermediario", 9, ini);
    g_psProFr = GetPrivateProfileIntA("PistolaSilenciada", "QuadrosHitman", 6, ini);
    g_psStdAcc = GetPrivateProfileIntA("PistolaSilenciada", "PrecisaoIntermediario", 135, ini) / 100.f;
    g_psProAcc = GetPrivateProfileIntA("PistolaSilenciada", "PrecisaoHitman", 160, ini) / 100.f;
    if (g_daCone > 90.f) g_daCone = 90.f;
    if (g_daConeZ > 45.f) g_daConeZ = 45.f;
    g_skinClothes  = GetPrivateProfileIntA("Skins", "KeepSkin",     1, ini);
    g_garOn        = GetPrivateProfileIntA("Garage", "Enabled",     1, ini);
    g_garPrice     = GetPrivateProfileIntA("Garage", "Price",    1250, ini);
    g_garSellPct   = GetPrivateProfileIntA("Garage", "SellPercent", 15, ini);
    g_gyroOn       = GetPrivateProfileIntA("Gyro", "Enabled",        1, ini);
    g_giveOn       = GetPrivateProfileIntA("ArmaPedestre", "Enabled",        1, ini);
    g_giveAmmo     = GetPrivateProfileIntA("ArmaPedestre", "Ammo",        9999, ini);
    if (g_giveAmmo < 1) g_giveAmmo = 1;
    if (g_giveAmmo > 99999) g_giveAmmo = 99999;
    {
        char v[32];
        GetPrivateProfileStringA("ArmaPedestre", "Anim", "hndshkfa", g_giveAnim, sizeof g_giveAnim, ini);
        GetPrivateProfileStringA("ArmaPedestre", "AnimPartial", "prtial_hndshk_01", g_giveAnimPart, sizeof g_giveAnimPart, ini);
        GetPrivateProfileStringA("ArmaPedestre", "Distance", "1.0", v, sizeof v, ini);
        float d = (float)atof(v);
        g_giveDist = (d >= 0.4f && d <= 2.5f) ? d : 1.0f;
    }
    g_pmOn         = GetPrivateProfileIntA("ProtegerArmas", "Enabled",        1, ini);
    g_pmInf        = GetPrivateProfileIntA("ProtegerArmas", "InfiniteFrom",  9999, ini);
    g_pmBoxOnFail  = GetPrivateProfileIntA("ProtegerArmas", "BoxOnFail",        0, ini);
    if (g_pmInf < 100) g_pmInf = 100;
    {
        char buf[128]; short sl[13];
        GetPrivateProfileStringA("ProtegerArmas", "Slots", "2,3,4,5", buf, sizeof buf, ini);
        int n = ParseList(buf, sl, 13);
        memset(g_pmProt, 0, sizeof g_pmProt);
        for (int i = 0; i < n; i++) if (sl[i] >= 2 && sl[i] <= 7) g_pmProt[sl[i]] = true;
        GetPrivateProfileStringA("ProtegerArmas", "Exclude", "SHRANGE", g_pmExclude, sizeof g_pmExclude, ini);
    }
    g_armOn        = GetPrivateProfileIntA("Traficante", "Enabled",           1, ini);
    g_armCall      = GetPrivateProfileIntA("Traficante", "CallPrice",       250, ini);
    g_armDiscount  = GetPrivateProfileIntA("Traficante", "Discount",         15, ini);
    g_armLockMult  = GetPrivateProfileIntA("Traficante", "LockedMultiplier", 10, ini);
    g_armLockBoxes = GetPrivateProfileIntA("Traficante", "LockedBoxesPerDay", 2, ini);
    g_armCooldown  = GetPrivateProfileIntA("Traficante", "CooldownMinutes", 1440, ini);
    g_armLeave     = GetPrivateProfileIntA("Traficante", "LeaveDistance",    40, ini);
    g_armCopSee    = GetPrivateProfileIntA("Traficante", "CopSeeDistance",   25, ini);
    g_armCopAngle  = GetPrivateProfileIntA("Traficante", "CopSeeAngle",      60, ini);
    g_armDoorDist  = GetPrivateProfileIntA("Traficante", "DoorOpenDistance",  7, ini);
    if (g_armCopSee < 5) g_armCopSee = 5;
    if (g_armCopAngle < 10 || g_armCopAngle > 180) g_armCopAngle = 60;
    if (g_armDoorDist < 2) g_armDoorDist = 2;
    if (g_armDiscount < 0 || g_armDiscount > 90) g_armDiscount = 15;
    if (g_armLockMult < 1) g_armLockMult = 1;
    if (g_armLockBoxes < 0) g_armLockBoxes = 0;
    if (g_armLeave < 15) g_armLeave = 15;
    for (int i = 0; i < kArmItems; i++) {                                     // preco[,caixa,preco da caixa]
        ArmItem& it = g_armItems[i];
        char v[64];
        GetPrivateProfileStringA("Traficante", it.key, "", v, sizeof v, ini);
        if (!v[0]) continue;
        int vals[3] = { -1, -1, -1 }, k = 0;                                  // ate 3 numeros separados por virgula
        for (char* q = v; *q && k < 3; ) {
            while (*q && !isdigit((unsigned char)*q) && *q != '-') q++;
            if (!*q) break;
            vals[k++] = (int)strtol(q, &q, 10);
        }
        int a = vals[0], b = vals[1], c = vals[2];
        if (k >= 1 && a >= 0) it.price = a;
        if (it.pack) { if (k >= 2 && b > 0) it.ammo = b; }
        else if (it.box > 0) { if (k >= 2 && b > 0) it.box = b; if (k >= 3 && c >= 0) it.boxPrice = c; }
    }
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
    Log("GTMTouchSA 3.48 carregado (vigilante=%d transito=%d direcao=%d rampa=%d retorno=%d toque=%d sim/nao por script=%d diag toque=%d ragdoll=%d contagem=%d audio=%d x87=%d pular abertura=%d radar no alto=%d protecao da arma=%d menu de skins=%d andar da skin=%d voz da skin=%d skin nas roupas de missao=%d garagem=%d giroscopio=%d traficante=%d dar arma=%d armas em missao=%d mira do drive-by=%d)",
        g_vgOn, g_trOn, g_steerOn, g_rampMs, g_releaseMs, g_touchOn, g_ynScripts, g_traceTouch, g_traceRagdoll, g_countRagdoll, g_audioHash, g_fixX87, g_skipIntro, g_radarTop, g_weaponGuard, g_skinOn, g_skinAnims, g_skinVoice, g_skinClothes, g_garOn, g_gyroOn, g_armOn, g_giveOn, g_pmOn, g_daOn);
    if (g_armOn) Log("traficante: ligacao $%d, %d%% abaixo da AmmuNation, fora de epoca %dx e %d caixas por arma por dia, sem atender %d min do jogo, vai embora a %d m",
        g_armCall, g_armDiscount, g_armLockMult, g_armLockBoxes, g_armCooldown, g_armLeave);
    if (GetModuleHandleA("GTMSteerSA.asi")) Log("AVISO: GTMSteerSA.asi tambem esta carregado; apague ele da pasta scripts");
    if (!IsSupportedExe()) { Log("executavel nao e o 1.0 US: nada foi alterado"); return TRUE; }
    OpenShared();
    InstallFrameHook();
    if (g_fixX87) ApplyX87Fixes();
    if (g_skipIntro) ApplySkipIntro();
    if (g_radarTop > 0) ApplyRadarTop(g_radarTop);
    if (g_weaponGuard) ApplyWeaponGuard();
    if (g_skinAnims) ApplySkinAnims();
    if (g_skinClothes) ApplySkinClothes();
    if (g_gyroOn) ApplyGyroCam();
    if (g_pmOn) ApplyProtect();
    if (g_noMouseSteer) ApplyNoMouseSteer();
    if (g_skinVoice) ApplyConvSay();
    if (g_trOn) ApplyTraffic();
    ApplyVigilante();
    if (g_garOn) PatchCallSite(0x44B125, 0x4C8500, (void*)&PnsColour, "garagem: Pay 'n' Spray sem trocar a cor do carro seu");
    ApplyCrimePatches();
    g_mhReady = (MH_Initialize() == MH_OK);
    if (!g_mhReady) { Log("MinHook nao iniciou"); return TRUE; }
    ApplyFemaleCops();                                                        // antes de o jogo ler os IDE
    HANDLE t = CreateThread(nullptr, 0, StartupThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t); else Log("nao criou a thread de inicio (erro %lu)", GetLastError());
    return TRUE;
}
