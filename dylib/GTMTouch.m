// GTMTouch.dylib 3.4 - overlay de toque contextual para o GTA San Andreas no Madeira.
//
// Injetado no IPA oficial do Madeira. Desenha os controles por cima do jogo e troca o
// conjunto de botoes sozinho conforme o contexto que o GTMTouchSA.asi escreve em
// Documents/wine/drive_c/gtmtouch.bin (C:\gtmtouch.bin para o jogo). Os toques voltam
// pelo mesmo arquivo; o .asi entrega ao GInput como um controle.
//
// 3.0: cada botao pode depender do que esta acontecendo no jogo (Sim/Nao so quando ha
// pergunta, Detonar/Jogar mais com satchel, Nitro, Hidraulica, Missao so em veiculo de
// submissao, Mirar/Trocar arma so quando servem, icone do Atirar pela arma na mao,
// Entrar com o icone do veiculo perto e apagado quando nao ha nenhum, recrutar/grupo,
// natacao, jetpack, cutscene) e ha um layout para o menu de pausa (setas, OK, Voltar),
// que tambem responde o "Retry?" do Retry Mission Mod.
//
// - Icones: PNGs em Documents/gtmtouch/icons/<nome>.png (extraidos do SA mobile do
//   proprio usuario). Sem o PNG, o botao mostra o texto.
// - Editor: o lapis no canto abre o modo edicao; arrastar move, pinca muda o tamanho.
//   Botoes que so aparecem em certas situacoes ficam visiveis (apagados) no editor.
//   O layout fica em Documents/gtmtouch/layout.json, um por situacao.
// - Salvar: botao "Salvar" manda o jogo salvar na hora; avisos de save aparecem no topo.
//
// 3.1: trocar de arma deslizando sobre o icone da arma do jogo (canto de cima a direita: para a
// esquerda volta, para a direita ou um toque avanca); o botao TAB mostra os status do CJ e vira
// telefone ou troca pela arma do chao quando o jogo pede; botoes do canto de cima a esquerda
// abriram espaco para o radar, que o GTMTouchSA 3.8 desenha no alto.
// 3.2: um toque no icone da arma, sem deslizar, recarrega a arma na mao quando o GTMTouchSA 3.12
// diz que da (S_RELOAD); se nao precisar ou nao der, nao faz nada. A troca de arma fica so no deslizar.
// 3.3: teclado para o GTMTouchSA 3.16 ("Trocar placa" da garagem). O jogo pede em [128..191] do arquivo
// (GTMK: numero do pedido, titulo, texto atual); o overlay abre um alerta com o teclado do iPhone (ate 8
// letras ou numeros, maiusculas) e responde em [192..255] (GTMA: 1 salvar, 2 cancelar). Protocolo 4.
// 3.4: giroscopio (GTMTouchSA 3.17). Com o jogo rodando (fora do menu e da cutscene) e o giroscopio ligado no
// painel, le o sensor 100 vezes por segundo, passa para os eixos da tela (virar para os lados usa a vertical do
// mundo, entao funciona com o telefone em pe ou inclinado), suaviza so os movimentos bem pequenos (tremido da mao)
// e escreve em [96..127] (GTMY) o giro acumulado e os ajustes. O jogo gira so a camera, como um mouse. Painel no
// editor (lapis > Giroscopio). Protocolo 5: o bloco do overlay passa a gravar so os 32 bytes dele em [64..95].
//      Botao "Olhar atrás" dirigindo (carro, moto, barco) e de passageiro: aperta os dois gatilhos juntos.
//
// Sem jogo rodando o overlay some e todo toque passa direto para o Madeira. No menu e na
// cutscene so os botoes recebem toque; o resto da tela continua sendo do Madeira.

#import <UIKit/UIKit.h>
#import <CoreMotion/CoreMotion.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <stdlib.h>

// ------------------------------------------------------------------ protocolo (v3, 128 bytes)
typedef struct __attribute__((packed)) {
    uint32_t magic; uint16_t version, flags; uint32_t seq, context;
    uint32_t saveSeq; int32_t saveResult; uint32_t saveKind;
    uint8_t onMission, canSave, slot, pad0;
    uint32_t autosaveIn;
    uint32_t state;                 // v3
    uint8_t weapon, nearVeh, pad1[2];
    uint8_t pad[20];
} GTMGameBlock;
typedef struct __attribute__((packed)) {
    uint32_t magic, seq; uint16_t buttons; uint8_t lt, rt; int16_t lx, ly, rx, ry;
    uint32_t cmdSeq, cmd;
    uint16_t rawButtons, version;   // v3
    uint8_t pad[32];
} GTMHostBlock;
// teclado (protocolo 4): pedido do jogo em 128, resposta do overlay em 192
typedef struct __attribute__((packed)) {
    uint32_t magic, seq; uint8_t maxLen, kind, pad[2]; char title[32]; char text[16]; uint8_t pad2[4];
} GTMKbRequest;
typedef struct __attribute__((packed)) {
    uint32_t magic, seq, result; char text[16]; uint8_t pad[36];
} GTMKbAnswer;
_Static_assert(sizeof(GTMGameBlock) == 64 && sizeof(GTMHostBlock) == 64, "layout");
_Static_assert(sizeof(GTMKbRequest) == 64 && sizeof(GTMKbAnswer) == 64, "layout kb");
static const uint32_t kKbReqMagic = 0x4B4D5447;  // "GTMK"
static const uint32_t kKbAnsMagic = 0x414D5447;  // "GTMA"
static const off_t kKbReqOffset = 128, kKbAnsOffset = 192;
// giroscopio (protocolo 5): escrito pelo overlay em 96, dentro do espaco livre do bloco do overlay
typedef struct __attribute__((packed)) {
    uint32_t magic, seq, yaw, pitch;                    // giro acumulado em microrradianos: direita +, cima + (mod 2^32)
    uint16_t sensH, sensV, aimPct; uint8_t flags, pad0; // sensibilidade x100, mira em %, GY_*
    uint8_t pad[8];
} GTMGyroBlock;
_Static_assert(sizeof(GTMGyroBlock) == 32, "layout gyro");
static const uint32_t kGyroMagic = 0x594D5447;   // "GTMY"
static const off_t kGyroOffset = 96;
enum { GY_ON = 1, GY_ALWAYS = 2, GY_INVERT = 4, GY_ZOOM = 8, GY_RUN = 16 };
static const uint32_t kGameMagic = 0x544D5447;   // "GTMT"
static const uint32_t kHostMagic = 0x484D5447;   // "GTMH"
static const off_t kHostOffset = 64;

enum {
    CTX_NONE = 0, CTX_FOOT, CTX_CAR, CTX_BIKE, CTX_HELI, CTX_PLANE, CTX_BOAT, CTX_PASSENGER,
    CTX_MENU, CTX_SWIM, CTX_JETPACK, CTX_CUTSCENE
};

// estado do jogo (igual ao .asi) + S_CANSAVE, que e local
enum {
    S_YESNO = 1u << 0, S_WEAPONS = 1u << 1, S_AIM = 1u << 2, S_DETONATE = 1u << 3, S_MORESATCHEL = 1u << 4,
    S_VEHNEAR = 1u << 5, S_FIRE = 1u << 6, S_FIRE2 = 1u << 7, S_NITRO = 1u << 8, S_HYDRAULICS = 1u << 9,
    S_SUBMISSION = 1u << 10, S_GROUP = 1u << 11, S_TARGETPED = 1u << 12, S_ARMED = 1u << 13,
    S_REMOTE = 1u << 14, S_GROUPMEMBERS = 1u << 15, S_DRIVEBY = 1u << 16,
    S_ACTPHONE = 1u << 17, S_ACTWEAPON = 1u << 18, S_ACTOTHER = 1u << 19, S_RELOAD = 1u << 20,
};
#define S_CANSAVE 0x80000000u
// .asi 2.0 nao manda estado: mostra o que a 2.0 mostrava
static const uint32_t kCompatState = S_YESNO | S_WEAPONS | S_AIM | S_ARMED | S_FIRE | S_SUBMISSION | S_VEHNEAR;

// bits XInput; LT/RT viram gatilhos; RAWL/RAWR sao setas que nao viram direcao (Sim/Nao)
enum {
    B_UP = 0x1, B_DOWN = 0x2, B_LEFT = 0x4, B_RIGHT = 0x8, B_START = 0x10, B_BACK = 0x20,
    B_L3 = 0x40, B_R3 = 0x80, B_LB = 0x100, B_RB = 0x200, B_A = 0x1000, B_B = 0x2000,
    B_X = 0x4000, B_Y = 0x8000, B_LT = 0x10000, B_RT = 0x20000, B_RAWL = 0x40000, B_RAWR = 0x80000,
    B_STKUP = 0x100000, B_STKDN = 0x200000      // analogico esquerdo para cima/baixo, com rampa (cruz das aeronaves)
};
enum { CMD_NONE = 0, CMD_SAVE = 1, CMD_DETONATE = 2, CMD_SATCHEL = 3, CMD_RELOAD = 4 };
enum { DYN_NONE = 0, DYN_WEAPON, DYN_VEHICLE, DYN_GROUP, DYN_ACTION };

// ------------------------------------------------------------------ layouts padrao
// Posicoes em pontos numa tela de referencia de 874 x 402 (iPhone 6,3" deitado); d = diametro.
// need: bits de estado exigidos para aparecer; lit: para ficar aceso (senao fica apagado).
typedef struct {
    const char *key, *label, *icon, *badge;
    uint32_t bits;
    uint8_t cmd, dyn;
    uint8_t swipe;                  // area de deslizar (invisivel no jogo): esquerda = LT, direita = RT, toque = recarregar
    uint32_t need, lit;
    double x, y, d;
} GTMBtnDef;
static const double kRefW = 874.0, kRefH = 402.0;

#define CAM_BTN   {.key = "cam",   .label = "Câmera", .icon = "cam-toggle", .bits = B_BACK, .x = 390, .y = 112, .d = 51}
#define PAUSE_BTN {.key = "pause", .label = "Pausa",  .bits = B_START, .x = 484, .y = 112, .d = 51}
#define SAVE_BTN  {.key = "save",  .label = "Salvar", .cmd = CMD_SAVE, .lit = S_CANSAVE, .x = 300, .y = 58, .d = 40}
#define YESNO_AT(yy) \
    {.key = "no",  .label = "Não", .icon = "hud_x",     .bits = B_RAWL, .need = S_YESNO, .x = 220, .y = yy, .d = 44}, \
    {.key = "yes", .label = "Sim", .icon = "hud_check", .bits = B_RAWR, .need = S_YESNO, .x = 280, .y = yy, .d = 44}
#define ENTER_BTN \
    {.key = "enter", .label = "Entrar", .icon = "enterandexit", .bits = B_Y, .dyn = DYN_VEHICLE, .lit = S_VEHNEAR, .x = 765, .y = 225, .d = 61}
#define RADIO_BTNS \
    {.key = "radio+", .label = "Rádio +", .icon = "radiostation", .badge = "+", .bits = B_UP,   .x = 220, .y = 100, .d = 44}, \
    {.key = "radio-", .label = "Rádio −", .icon = "radiostation", .badge = "−", .bits = B_DOWN, .x = 280, .y = 100, .d = 44}
#define LOOK_BTNS \
    {.key = "lookl", .label = "Olhar ◀", .icon = "hud_left",  .bits = B_LT, .need = S_DRIVEBY, .x = 600, .y = 160, .d = 46}, \
    {.key = "lookr", .label = "Olhar ▶", .icon = "hud_right", .bits = B_RT, .need = S_DRIVEBY, .x = 680, .y = 160, .d = 46}
// olhar para tras: os dois gatilhos juntos (L2 + R2 do controle do jogo); sem icone no SA mobile, vai o texto
#define LOOKBACK_AT(xx, yy) \
    {.key = "lookb", .label = "Olhar\natrás", .bits = B_LT | B_RT, .x = xx, .y = yy, .d = 50}
#define DRIVE_COMMON \
    {.key = "left",  .label = "◀", .icon = "hud_left",  .bits = B_LEFT,  .x = 130, .y = 315, .d = 83}, \
    {.key = "right", .label = "▶", .icon = "hud_right", .bits = B_RIGHT, .x = 245, .y = 315, .d = 83}, \
    {.key = "horn",  .label = "Buzina", .icon = "horn", .bits = B_L3, .x = 187, .y = 225, .d = 58}, \
    VEHICLE_COMMON
// cruz das aeronaves: ◀ ▶ inclinam as asas (viram direcao no .asi); ▲ ▼ mexem o analogico na vertical
#define AIR_CROSS(upLabel, upBits, downLabel, downBits) \
    {.key = "up",    .label = upLabel,   .icon = "menu_up",   .bits = upBits,   .x = 170, .y = 215, .d = 70}, \
    {.key = "down",  .label = downLabel, .icon = "menu_down", .bits = downBits, .x = 170, .y = 355, .d = 70}, \
    {.key = "left",  .label = "◀", .icon = "hud_left",  .bits = B_LEFT,  .x =  95, .y = 285, .d = 70}, \
    {.key = "right", .label = "▶", .icon = "hud_right", .bits = B_RIGHT, .x = 245, .y = 285, .d = 70}, \
    {.key = "horn",  .label = "Buzina", .icon = "horn", .bits = B_L3, .x = 300, .y = 210, .d = 50}, \
    VEHICLE_COMMON
#define VEHICLE_COMMON \
    {.key = "handbrake", .label = "Freio de\nmão", .icon = "handbrake", .bits = B_RB, .x = 765, .y = 225, .d = 58}, \
    {.key = "fire",  .label = "Atirar", .icon = "shoot",        .bits = B_B, .need = S_FIRE, .x = 665, .y = 240, .d = 54}, \
    {.key = "exit",  .label = "Sair",   .icon = "enterandexit", .bits = B_Y,  .x = 770, .y = 140, .d = 54}, \
    {.key = "mission", .label = "Missão", .icon = "mission_start", .bits = B_R3, .need = S_SUBMISSION, .x = 110, .y = 165, .d = 46}, \
    RADIO_BTNS, YESNO_AT(165), CAM_BTN, PAUSE_BTN, SAVE_BTN

static const GTMBtnDef kFoot[] = {
    {.key = "fire",   .label = "Atirar",  .icon = "punch",  .bits = B_B, .dyn = DYN_WEAPON, .x = 765, .y = 315, .d = 74},
    {.key = "sprint", .label = "Correr",  .icon = "sprint", .bits = B_A, .x = 680, .y = 345, .d = 61},
    {.key = "jump",   .label = "Pular",   .icon = "menu_up", .bits = B_X, .x = 680, .y = 255, .d = 61},
    ENTER_BTN,
    {.key = "aim",    .label = "Mirar",   .icon = "hud_lockon",  .bits = B_RB, .need = S_AIM,     .x = 765, .y = 150, .d = 54},
    {.key = "wswipe", .label = "Trocar\narma", .swipe = 1, .need = S_WEAPONS, .x = 709, .y = 44, .d = 64},
    {.key = "detonate", .label = "Detonar",    .icon = "hud_detonator", .cmd = CMD_DETONATE, .need = S_DETONATE,    .x = 590, .y = 300, .d = 58},
    {.key = "satchel",  .label = "Jogar mais", .icon = "hud_bomb",      .cmd = CMD_SATCHEL,  .need = S_MORESATCHEL, .x = 590, .y = 225, .d = 54},
    {.key = "action", .label = "Status",  .bits = B_LB, .dyn = DYN_ACTION, .x = 110, .y = 180, .d = 51},
    {.key = "crouch", .label = "Agachar", .icon = "crouch",        .bits = B_L3, .x = 110, .y = 240, .d = 51},
    {.key = "follow", .label = "Recrutar", .icon = "LetsGoHand", .bits = B_UP,   .dyn = DYN_GROUP, .need = S_GROUP,     .x = 205, .y = 165, .d = 44},
    {.key = "stay",   .label = "Ficar",    .icon = "StopHand",   .bits = B_DOWN, .need = S_GROUPMEMBERS,                 .x = 265, .y = 165, .d = 44},
    YESNO_AT(100), CAM_BTN, PAUSE_BTN, SAVE_BTN,
};
static const GTMBtnDef kSwim[] = {
    {.key = "dive",     .label = "Mergulhar",     .icon = "hud_dive", .bits = B_B, .x = 765, .y = 315, .d = 74},
    {.key = "swimfast", .label = "Nadar\nrápido", .icon = "hud_swim", .bits = B_A, .x = 680, .y = 345, .d = 61},
    {.key = "surface",  .label = "Subir",         .icon = "menu_up",  .bits = B_X, .x = 680, .y = 255, .d = 61},
    ENTER_BTN,
    YESNO_AT(100), CAM_BTN, PAUSE_BTN, SAVE_BTN,
};
static const GTMBtnDef kJetpack[] = {
    {.key = "fire",    .label = "Atirar", .icon = "shoot",     .bits = B_B, .dyn = DYN_WEAPON, .need = S_ARMED, .x = 765, .y = 315, .d = 74},
    {.key = "up",      .label = "Subir",  .icon = "menu_up",   .bits = B_A, .x = 680, .y = 255, .d = 61},
    {.key = "down",    .label = "Descer", .icon = "menu_down", .bits = B_X, .x = 680, .y = 345, .d = 61},
    {.key = "drop",    .label = "Soltar", .icon = "hud_drop_jetpack", .bits = B_Y, .x = 765, .y = 225, .d = 61},
    {.key = "aim",     .label = "Mirar",  .icon = "hud_lockon", .bits = B_RB, .need = S_ARMED, .x = 765, .y = 150, .d = 54},
    {.key = "strafel", .label = "◀",      .icon = "hud_left",   .bits = B_LT, .x = 640, .y = 100, .d = 46},
    {.key = "strafer", .label = "▶",      .icon = "hud_right",  .bits = B_RT, .x = 730, .y = 100, .d = 46},
    YESNO_AT(100), CAM_BTN, PAUSE_BTN,
};
static const GTMBtnDef kCar[] = {
    {.key = "gas",   .label = "Acelerar",   .icon = "accelerate", .bits = B_A, .x = 765, .y = 315, .d = 83},
    {.key = "brake", .label = "Freio / ré", .icon = "brake",      .bits = B_X, .x = 665, .y = 330, .d = 70},
    {.key = "nitro", .label = "Nitro",      .icon = "hud_nitro",  .bits = B_B, .need = S_NITRO, .x = 580, .y = 300, .d = 58},
    {.key = "hydraulics", .label = "Hidráulica", .icon = "hydraulicCar", .bits = B_R3, .need = S_HYDRAULICS, .x = 300, .y = 225, .d = 58},
    LOOK_BTNS,
    LOOKBACK_AT(580, 225),
    DRIVE_COMMON,
};
static const GTMBtnDef kHeli[] = {
    {.key = "gas",   .label = "Subir",    .icon = "menu_up",   .bits = B_A,  .x = 765, .y = 315, .d = 83},
    {.key = "brake", .label = "Descer",   .icon = "menu_down", .bits = B_X,  .x = 665, .y = 330, .d = 70},
    {.key = "fire2", .label = "Atirar 2", .icon = "leftshoot", .bits = B_LB, .need = S_FIRE2, .x = 580, .y = 240, .d = 54},
    AIR_CROSS("Frente", B_STKUP, "Trás", B_STKDN),
    {.key = "yawl", .label = "Girar ◀", .icon = "hud_tank_left",  .bits = B_LT, .x = 640, .y = 160, .d = 46},
    {.key = "yawr", .label = "Girar ▶", .icon = "hud_tank_right", .bits = B_RT, .x = 640, .y =  85, .d = 46},
};
static const GTMBtnDef kPlane[] = {
    {.key = "gas",   .label = "Acelerar", .icon = "accelerate", .bits = B_A,  .x = 765, .y = 315, .d = 83},
    {.key = "brake", .label = "Frear",    .icon = "brake",      .bits = B_X,  .x = 665, .y = 330, .d = 70},
    {.key = "fire2", .label = "Atirar 2", .icon = "leftshoot",  .bits = B_LB, .need = S_FIRE2, .x = 580, .y = 240, .d = 54},
    AIR_CROSS("Subir", B_STKDN, "Descer", B_STKUP),     // manche para tras sobe o nariz
    {.key = "yawl", .label = "Leme ◀", .icon = "hud_tank_left",  .bits = B_LT, .x = 640, .y = 160, .d = 46},
    {.key = "yawr", .label = "Leme ▶", .icon = "hud_tank_right", .bits = B_RT, .x = 640, .y =  85, .d = 46},
};
static const GTMBtnDef kPassenger[] = {
    {.key = "fire", .label = "Atirar", .icon = "shoot",        .bits = B_B, .need = S_FIRE, .x = 765, .y = 315, .d = 74},
    {.key = "exit", .label = "Sair",   .icon = "enterandexit", .bits = B_Y, .x = 770, .y = 140, .d = 54},
    LOOK_BTNS,
    LOOKBACK_AT(640, 235),
    RADIO_BTNS, YESNO_AT(165), CAM_BTN, PAUSE_BTN,
};
static const GTMBtnDef kMenu[] = {
    {.key = "up",     .label = "▲",      .icon = "menu_up",   .bits = B_UP,    .x = 140, .y = 215, .d = 52},
    {.key = "down",   .label = "▼",      .icon = "menu_down", .bits = B_DOWN,  .x = 140, .y = 335, .d = 52},
    {.key = "left",   .label = "◀",      .icon = "hud_left",  .bits = B_LEFT,  .x =  80, .y = 275, .d = 48},
    {.key = "right",  .label = "▶",      .icon = "hud_right", .bits = B_RIGHT, .x = 200, .y = 275, .d = 48},
    {.key = "ok",     .label = "OK",     .icon = "hud_check", .bits = B_A,     .x = 770, .y = 300, .d = 66},
    {.key = "back",   .label = "Voltar", .icon = "menu_back", .bits = B_Y,     .x = 770, .y = 195, .d = 54},
    {.key = "resume", .label = "Jogo",   .bits = B_START, .x = 770, .y = 110, .d = 46},
};
static const GTMBtnDef kCutscene[] = {
    {.key = "skip", .label = "Pular\ncena", .bits = B_A, .x = 765, .y = 315, .d = 70},
    PAUSE_BTN,
};

static const GTMBtnDef *LayoutFor(int ctx, int *count, NSString **key) {
#define L(a, k) do { *count = (int)(sizeof(a) / sizeof(a[0])); *key = k; return a; } while (0)
    switch (ctx) {
        case CTX_FOOT: L(kFoot, @"foot");
        case CTX_SWIM: L(kSwim, @"swim");
        case CTX_JETPACK: L(kJetpack, @"jetpack");
        case CTX_CAR: case CTX_BIKE: case CTX_BOAT: L(kCar, @"car");
        case CTX_HELI: L(kHeli, @"heli");
        case CTX_PLANE: L(kPlane, @"plane");
        case CTX_PASSENGER: L(kPassenger, @"passenger");
        case CTX_MENU: L(kMenu, @"menu");
        case CTX_CUTSCENE: L(kCutscene, @"cutscene");
        default: *count = 0; *key = nil; return NULL;
    }
#undef L
}
static NSString *LayoutTitle(NSString *key) {
    NSDictionary *t = @{@"foot": @"a pé", @"swim": @"nadando", @"jetpack": @"jetpack",
                        @"car": @"carro, moto e barco", @"heli": @"helicóptero", @"plane": @"avião",
                        @"passenger": @"passageiro", @"menu": @"menu de pausa", @"cutscene": @"cutscene"};
    return t[key] ?: key;
}

// icone do Atirar pela arma na mao (eWeaponType 0..46)
static const char *kWeaponIcon[47] = {
    "punch", "brassknuckle", "hud_golfclub", "hud_nightstick", "hud_knife", "hud_bat", "shovel", "hud_poolcue",
    "hud_katana", "hud_chainsaw", "hud_dildo1", "hud_dildo1", "hud_dildo2", "hud_dildo2", "hud_flowers", "hud_cane",
    "hud_throw", "hud_teargas", "hud_throw", NULL, NULL, NULL, "shoot", "shoot",
    "shoot", "shoot", "shoot", "shoot", "shoot", "shoot", "shoot", "shoot",
    "shoot", "shoot", "shoot", "hud_rockets", "hud_rockets", "flamethrower", "shoot", "hud_throw",
    "hud_detonator", "hud_spraycan", "hud_extinguisher", "hud_camera", "goggles", "goggles", "hud_parachute",
};
// icone do Entrar pela classe do veiculo perto (classe + 1)
static const char *kVehicleIcon[12] = {
    NULL, "hud_car", "hud_monstertruck", "hud_bike", "hud_chopper", "hud_plane", "hud_boat",
    "hud_train", "hud_chopper", "hud_plane", "hud_bike", "hud_bicycle",
};

// ------------------------------------------------------------------ arquivos
static NSString *GTMDocs(void) {
    return NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
}
static NSString *GTMSharedPath(void) { return [GTMDocs() stringByAppendingPathComponent:@"wine/drive_c/gtmtouch.bin"]; }
static NSString *GTMDataDir(void)    { return [GTMDocs() stringByAppendingPathComponent:@"gtmtouch"]; }
static NSString *GTMLayoutPath(void) { return [GTMDataDir() stringByAppendingPathComponent:@"layout.json"]; }

static int gFd = -1;
static BOOL GTMOpenShared(void) {
    if (gFd >= 0) return YES;
    NSString *path = GTMSharedPath();
    if (![[NSFileManager defaultManager] fileExistsAtPath:[path stringByDeletingLastPathComponent]]) return NO;
    gFd = open(path.fileSystemRepresentation, O_RDWR | O_CREAT, 0644);
    return gFd >= 0;
}

static NSMutableDictionary<NSString *, UIImage *> *gIcons;
static UIImage *GTMIcon(const char *name) {
    if (!name) return nil;
    if (!gIcons) gIcons = [NSMutableDictionary dictionary];
    NSString *n = [NSString stringWithUTF8String:name];
    id cached = gIcons[n];
    if (cached) return cached == (id)[NSNull null] ? nil : cached;
    NSString *p = [[GTMDataDir() stringByAppendingPathComponent:@"icons"] stringByAppendingPathComponent:[n stringByAppendingString:@".png"]];
    UIImage *img = [UIImage imageWithContentsOfFile:p];
    gIcons[n] = img ?: (id)[NSNull null];
    return img;
}

// overrides: { "foot": { "fire": [x, y, d], ... }, ... } em unidades da tela de referencia
static NSMutableDictionary *gOverrides;
static void GTMLoadOverrides(void) {
    gOverrides = [NSMutableDictionary dictionary];
    NSData *d = [NSData dataWithContentsOfFile:GTMLayoutPath()];
    if (!d) return;
    NSDictionary *j = [NSJSONSerialization JSONObjectWithData:d options:0 error:nil];
    NSDictionary *layouts = [j isKindOfClass:NSDictionary.class] ? j[@"layouts"] : nil;
    if (![layouts isKindOfClass:NSDictionary.class]) return;
    for (NSString *k in layouts) {
        NSDictionary *m = layouts[k];
        if ([m isKindOfClass:NSDictionary.class]) gOverrides[k] = [m mutableCopy];
    }
}
static void GTMSaveOverrides(void) {
    [[NSFileManager defaultManager] createDirectoryAtPath:GTMDataDir() withIntermediateDirectories:YES attributes:nil error:nil];
    NSData *d = [NSJSONSerialization dataWithJSONObject:@{@"version": @1, @"layouts": gOverrides}
                                                options:NSJSONWritingPrettyPrinted error:nil];
    [d writeToFile:GTMLayoutPath() atomically:YES];
}

// ------------------------------------------------------------------ giroscopio
typedef struct { BOOL on, always, invert, zoom; double sensH, sensV, aim; } GTMGyroCfg;
static GTMGyroCfg GTMGyroDefaults(void) { return (GTMGyroCfg){ YES, NO, NO, YES, 1.0, 1.0, 1.0 }; }
static GTMGyroCfg gGyro;
static NSString *GTMGyroPath(void) { return [GTMDataDir() stringByAppendingPathComponent:@"gyro.json"]; }
static double GTMClamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
static void GTMLoadGyro(void) {
    gGyro = GTMGyroDefaults();
    NSData *d = [NSData dataWithContentsOfFile:GTMGyroPath()];
    NSDictionary *j = d ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil;
    if (![j isKindOfClass:NSDictionary.class]) return;
    if ([j[@"on"] isKindOfClass:NSNumber.class]) gGyro.on = [j[@"on"] boolValue];
    if ([j[@"always"] isKindOfClass:NSNumber.class]) gGyro.always = [j[@"always"] boolValue];
    if ([j[@"invert"] isKindOfClass:NSNumber.class]) gGyro.invert = [j[@"invert"] boolValue];
    if ([j[@"zoom"] isKindOfClass:NSNumber.class]) gGyro.zoom = [j[@"zoom"] boolValue];
    if ([j[@"horizontal"] isKindOfClass:NSNumber.class]) gGyro.sensH = GTMClamp([j[@"horizontal"] doubleValue], 0.2, 4.0);
    if ([j[@"vertical"] isKindOfClass:NSNumber.class]) gGyro.sensV = GTMClamp([j[@"vertical"] doubleValue], 0.2, 4.0);
    if ([j[@"aim"] isKindOfClass:NSNumber.class]) gGyro.aim = GTMClamp([j[@"aim"] doubleValue], 0.2, 1.5);
}
static void GTMSaveGyro(void) {
    [[NSFileManager defaultManager] createDirectoryAtPath:GTMDataDir() withIntermediateDirectories:YES attributes:nil error:nil];
    NSDictionary *j = @{ @"version": @1, @"on": @(gGyro.on), @"always": @(gGyro.always), @"invert": @(gGyro.invert),
                         @"zoom": @(gGyro.zoom), @"horizontal": @(round(gGyro.sensH * 100) / 100),
                         @"vertical": @(round(gGyro.sensV * 100) / 100), @"aim": @(round(gGyro.aim * 100) / 100) };
    NSData *d = [NSJSONSerialization dataWithJSONObject:j options:NSJSONWritingPrettyPrinted error:nil];
    [d writeToFile:GTMGyroPath() atomically:YES];
}

// Sensor numa fila propria (100 Hz). Tudo o que a fila usa (_cfg, acumulados, suavizacao) so e mexido nela.
enum { kGyroSmoothN = 12 };                            // ~0,12 s de suavizacao para o tremido
@interface GTMGyro : NSObject
+ (instancetype)shared;
- (void)setRunning:(BOOL)run;
- (void)setConfig:(GTMGyroCfg)cfg;
- (void)setOrientation:(UIInterfaceOrientation)o;
@end
@implementation GTMGyro {
    CMMotionManager *_mm;
    NSOperationQueue *_q;
    GTMGyroCfg _cfg;
    double _yaw, _pitch, _lastTs;
    double _ring[kGyroSmoothN][2];
    int _ringPos;
    uint32_t _seq;
    BOOL _running, _qRunning;
    volatile int _orient;
}
+ (instancetype)shared { static GTMGyro *g; static dispatch_once_t once; dispatch_once(&once, ^{ g = [GTMGyro new]; }); return g; }
- (instancetype)init {
    if ((self = [super init])) {
        _mm = [CMMotionManager new];
        _mm.deviceMotionUpdateInterval = 1.0 / 100;
        _q = [NSOperationQueue new];
        _q.maxConcurrentOperationCount = 1;
        _q.qualityOfService = NSQualityOfServiceUserInteractive;
        _cfg = GTMGyroDefaults();
        _orient = (int)UIInterfaceOrientationLandscapeRight;
        [NSNotificationCenter.defaultCenter addObserverForName:UIApplicationDidEnterBackgroundNotification object:nil
                                                         queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) { [self setRunning:NO]; }];
    }
    return self;
}
- (void)setOrientation:(UIInterfaceOrientation)o { if (o != UIInterfaceOrientationUnknown) _orient = (int)o; }
- (void)setConfig:(GTMGyroCfg)cfg { [_q addOperationWithBlock:^{ self->_cfg = cfg; [self writeBlock]; }]; }
- (void)setRunning:(BOOL)run {
    if (run == _running) return;
    if (run && !_mm.deviceMotionAvailable) return;
    _running = run;
    if (run) {
        [_q addOperationWithBlock:^{
            self->_qRunning = YES; self->_lastTs = 0; self->_ringPos = 0;
            memset(self->_ring, 0, sizeof self->_ring);
        }];
        [_mm startDeviceMotionUpdatesToQueue:_q withHandler:^(CMDeviceMotion *m, NSError *e) { if (m) [self sample:m]; }];
    } else {
        [_mm stopDeviceMotionUpdates];
        [_q addOperationWithBlock:^{ self->_qRunning = NO; [self writeBlock]; }];
    }
}
- (void)sample:(CMDeviceMotion *)m {
    double dt = _lastTs > 0 ? m.timestamp - _lastTs : 0;
    _lastTs = m.timestamp;
    if (dt < 0) dt = 0;
    if (dt > 0.05) dt = 0.05;
    double U[3] = { 1, 0, 0 }, R[3] = { 0, -1, 0 };    // tela deitada com o botao inicial a direita
    switch (_orient) {
        case UIInterfaceOrientationLandscapeLeft:      U[0] = -1; U[1] = 0; R[0] = 0; R[1] = 1; break;
        case UIInterfaceOrientationPortrait:           U[0] = 0; U[1] = 1; R[0] = 1; R[1] = 0; break;
        case UIInterfaceOrientationPortraitUpsideDown: U[0] = 0; U[1] = -1; R[0] = -1; R[1] = 0; break;
        default: break;
    }
    CMRotationRate r = m.rotationRate;
    CMAcceleration g = m.gravity;
    double gU = r.x * U[0] + r.y * U[1], gR = r.x * R[0] + r.y * R[1], gZ = r.z;
    // virar para os lados: pela vertical do mundo (gravidade), limitado ao giro dos eixos da tela ("player space")
    double yawLeft = gU, gn = sqrt(g.x * g.x + g.y * g.y + g.z * g.z);
    if (gn > 0.5) {
        double wU = -(g.x * U[0] + g.y * U[1]) / gn, wZ = -g.z / gn;
        double world = gU * wU + gZ * wZ, mag = sqrt(gU * gU + gZ * gZ);
        yawLeft = copysign(fmin(fabs(world) * 1.41, mag), world);
    }
    double yawRate = -yawLeft, pitchRate = gR;         // rad/s: direita +, cima +
    // suavizacao em degraus: abaixo de 2,5 graus/s tudo suavizado, acima de 5 graus/s nada
    double speed = sqrt(yawRate * yawRate + pitchRate * pitchRate);
    double w = GTMClamp((speed - 2.5 * M_PI / 180) / (2.5 * M_PI / 180), 0, 1);
    double dy = yawRate * dt, dp = pitchRate * dt;
    _ring[_ringPos][0] = dy * (1 - w); _ring[_ringPos][1] = dp * (1 - w);
    _ringPos = (_ringPos + 1) % kGyroSmoothN;
    double sy = 0, sp = 0;
    for (int i = 0; i < kGyroSmoothN; i++) { sy += _ring[i][0]; sp += _ring[i][1]; }
    _yaw += dy * w + sy / kGyroSmoothN;
    _pitch += dp * w + sp / kGyroSmoothN;
    [self writeBlock];
}
- (void)writeBlock {
    if (gFd < 0) return;
    GTMGyroBlock b = {0};
    b.magic = kGyroMagic; b.seq = ++_seq;
    b.yaw = (uint32_t)(int64_t)llround(_yaw * 1e6);
    b.pitch = (uint32_t)(int64_t)llround(_pitch * 1e6);
    b.sensH = (uint16_t)lround(_cfg.sensH * 100); b.sensV = (uint16_t)lround(_cfg.sensV * 100);
    b.aimPct = (uint16_t)lround(_cfg.aim * 100);
    b.flags = (_cfg.on ? GY_ON : 0) | (_cfg.always ? GY_ALWAYS : 0) | (_cfg.invert ? GY_INVERT : 0) |
              (_cfg.zoom ? GY_ZOOM : 0) | (_qRunning ? GY_RUN : 0);
    pwrite(gFd, &b, sizeof b, kGyroOffset);
}
@end

// ------------------------------------------------------------------ botao na tela
typedef struct { double x, y, d; } GTMPlace;

@interface GTMButtonView : UIView
@property (nonatomic) const GTMBtnDef *def;
@property (nonatomic) GTMPlace place;
@property (nonatomic) BOOL pressed, dimmed, editing, selectedForEdit, contextHidden;
- (void)setIconName:(const char *)name;
@end

@implementation GTMButtonView {
    UIImageView *_img;
    UILabel *_label, *_badge;
    CAShapeLayer *_ring;
    const char *_iconName;
}
- (instancetype)initWithDef:(const GTMBtnDef *)def {
    if ((self = [super initWithFrame:CGRectZero])) {
        _def = def;
        self.userInteractionEnabled = NO;
        _img = [UIImageView new];
        _img.contentMode = UIViewContentModeScaleAspectFit;
        [self addSubview:_img];
        _label = [UILabel new];
        _label.text = [NSString stringWithUTF8String:def->label];
        _label.numberOfLines = 2;
        _label.textAlignment = NSTextAlignmentCenter;
        _label.textColor = UIColor.whiteColor;
        _label.adjustsFontSizeToFitWidth = YES;
        _label.minimumScaleFactor = 0.6;
        _label.backgroundColor = [UIColor colorWithWhite:0 alpha:0.35];
        _label.layer.borderWidth = 2;
        _label.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.75].CGColor;
        _label.clipsToBounds = YES;
        [self addSubview:_label];
        if (def->badge) {
            _badge = [UILabel new];
            _badge.text = [NSString stringWithUTF8String:def->badge];
            _badge.textAlignment = NSTextAlignmentCenter;
            _badge.textColor = UIColor.blackColor;
            _badge.backgroundColor = UIColor.whiteColor;
            _badge.font = [UIFont systemFontOfSize:13 weight:UIFontWeightBold];
            _badge.clipsToBounds = YES;
            [self addSubview:_badge];
        }
        _ring = [CAShapeLayer layer];
        _ring.fillColor = UIColor.clearColor.CGColor;
        _ring.lineWidth = 2;
        _ring.lineDashPattern = @[@6, @4];
        _ring.hidden = YES;
        [self.layer addSublayer:_ring];
        _iconName = "";                                 // forca a primeira troca
        [self setIconName:def->icon];
        [self refresh];
    }
    return self;
}
// troca o icone; sem o PNG cai no icone padrao do botao e, sem ele, no texto
- (void)setIconName:(const char *)name {
    if (!name) name = _def->icon;
    if (name == _iconName || (name && _iconName && !strcmp(name, _iconName))) return;
    _iconName = name;
    UIImage *img = GTMIcon(name) ?: GTMIcon(_def->icon);
    _img.image = img;
    _img.hidden = (img == nil);
    _label.hidden = (img != nil);
    _badge.hidden = (img == nil);
}
- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect b = self.bounds;
    _img.frame = b;
    _label.frame = b;
    _label.layer.cornerRadius = b.size.width / 2;
    _label.font = [UIFont systemFontOfSize:MAX(10, MIN(15, b.size.width / 5.2)) weight:UIFontWeightSemibold];
    CGFloat s = MAX(16, b.size.width * 0.32);
    _badge.frame = CGRectMake(b.size.width - s * 0.9, b.size.height - s * 0.9, s, s);
    _badge.layer.cornerRadius = s / 2;
    _ring.path = [UIBezierPath bezierPathWithOvalInRect:CGRectInset(b, -3, -3)].CGPath;
}
- (void)refresh {
    self.hidden = (_contextHidden || _def->swipe) && !_editing;
    CGFloat a = _dimmed ? 0.3 : (_pressed ? 1.0 : 0.78);
    self.alpha = _editing ? (_contextHidden ? 0.45 : 0.9) : a;
    self.transform = _pressed ? CGAffineTransformMakeScale(0.9, 0.9) : CGAffineTransformIdentity;
    _ring.hidden = !_editing;
    UIColor *ring = _selectedForEdit ? [UIColor colorWithRed:0.3 green:0.9 blue:1 alpha:1]
                  : _contextHidden   ? [UIColor colorWithWhite:0.85 alpha:1]
                                     : [UIColor colorWithRed:1 green:0.85 blue:0.2 alpha:1];
    _ring.strokeColor = ring.CGColor;
    _label.backgroundColor = [UIColor colorWithWhite:_pressed ? 1 : 0 alpha:0.35];
}
- (void)setPressed:(BOOL)p { if (p != _pressed) { _pressed = p; [self refresh]; } }
- (void)setDimmed:(BOOL)d { if (d != _dimmed) { _dimmed = d; [self refresh]; } }
- (void)setContextHidden:(BOOL)h { if (h != _contextHidden) { _contextHidden = h; [self refresh]; } }
- (void)setEditing:(BOOL)e { _editing = e; [self refresh]; }
- (void)setSelectedForEdit:(BOOL)s { _selectedForEdit = s; [self refresh]; }
@end

// ------------------------------------------------------------------ overlay
typedef enum { T_NONE = 0, T_BUTTON, T_LSTICK, T_RSTICK, T_EDIT, T_SWIPE } GTMTouchKind;
typedef struct { __unsafe_unretained UITouch *touch; GTMTouchKind kind; int button; CGPoint start, cur, grab; } GTMSlot;

@interface GTMPadView : UIView
@property (nonatomic) int context;
@property (nonatomic) BOOL live, editing;
@property (nonatomic, weak) UIViewController *host;
- (void)tick;
@end

@implementation GTMPadView {
    GTMSlot _slots[12];
    const GTMBtnDef *_defs;
    int _count;
    NSString *_layoutKey;
    NSMutableArray<GTMButtonView *> *_buttons;
    CAShapeLayer *_lBase, *_lKnob, *_rBase, *_rKnob;
    uint32_t _hostSeq, _lastGameSeq, _cmdSeq, _hostCmd, _lastSaveSeq;
    BOOL _saveSeqKnown;
    uint32_t _kbSeq;                                    // pedido de teclado aberto (0 = nenhum)
    uint32_t _kbDone;                                   // ultimo pedido respondido
    UIAlertController *_kbAlert;
    __weak UIWindow *_kbPrevKey;
    int _kbMax;
    UIView *_gyroPanel;
    CFTimeInterval _lastGameChange;
    UIImpactFeedbackGenerator *_haptic;
    UIButton *_pencil;
    UIView *_toolbar;
    BOOL _toolbarLow;                                   // caixa do editor embaixo (para alcancar botoes do topo)
    UILabel *_toast;
    int _selected;
    double _pinchBase;
    uint32_t _state;
    uint8_t _weapon, _nearVeh;
    UIPinchGestureRecognizer *_pinch;                   // so ligado no editor
    double _pitch;                                      // rampa do ▲ ▼ das aeronaves (-1..1)
    CFTimeInterval _pitchTime;
    uint32_t _pulseBits;                                // toque curto vindo do deslizar (troca de arma)
    CFTimeInterval _pulseUntil;
}

- (instancetype)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.multipleTouchEnabled = YES;
        self.backgroundColor = UIColor.clearColor;
        self.alpha = 0;
        _buttons = [NSMutableArray array];
        _haptic = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
        _lBase = [self ring:YES]; _lKnob = [self ring:NO]; _rBase = [self ring:YES]; _rKnob = [self ring:NO];
        _context = CTX_NONE;
        _selected = -1;
        _state = S_CANSAVE;
        _weapon = 0xFF;
        GTMLoadOverrides();
        GTMLoadGyro();
        [GTMGyro.shared setConfig:gGyro];

        _pencil = [UIButton buttonWithType:UIButtonTypeSystem];
        [_pencil setImage:[UIImage systemImageNamed:@"pencil"] forState:UIControlStateNormal];
        _pencil.tintColor = UIColor.whiteColor;
        _pencil.backgroundColor = [UIColor colorWithWhite:0 alpha:0.35];
        _pencil.layer.borderWidth = 1.5;
        _pencil.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.6].CGColor;
        [_pencil addTarget:self action:@selector(startEditing) forControlEvents:UIControlEventTouchUpInside];
        [self addSubview:_pencil];

        _toolbar = [UIView new];
        _toolbar.backgroundColor = [UIColor colorWithWhite:0 alpha:0.7];
        _toolbar.layer.cornerRadius = 12;
        _toolbar.hidden = YES;
        UILabel *hint = [UILabel new];
        hint.tag = 10;
        hint.textColor = UIColor.whiteColor;
        hint.font = [UIFont systemFontOfSize:13];
        hint.textAlignment = NSTextAlignmentCenter;
        hint.numberOfLines = 2;
        hint.adjustsFontSizeToFitWidth = YES;
        hint.minimumScaleFactor = 0.75;
        [_toolbar addSubview:hint];
        UIButton *done = [self toolButton:@"Concluir" action:@selector(finishEditing)];
        done.tag = 11;
        UIButton *reset = [self toolButton:@"Restaurar padrão" action:@selector(askReset)];
        reset.tag = 12;
        [reset setTitleColor:[UIColor colorWithRed:1 green:0.45 blue:0.4 alpha:1] forState:UIControlStateNormal];
        UIButton *move = [self toolButton:@"Caixa para baixo" action:@selector(moveToolbar)];
        move.tag = 13;
        UIButton *gyro = [self toolButton:@"Giroscópio" action:@selector(toggleGyroPanel)];
        gyro.tag = 14;
        [_toolbar addSubview:done];
        [_toolbar addSubview:reset];
        [_toolbar addSubview:move];
        [_toolbar addSubview:gyro];
        [self addSubview:_toolbar];

        _toast = [UILabel new];
        _toast.textColor = UIColor.whiteColor;
        _toast.backgroundColor = [UIColor colorWithWhite:0 alpha:0.7];
        _toast.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];
        _toast.textAlignment = NSTextAlignmentCenter;
        _toast.layer.cornerRadius = 10;
        _toast.clipsToBounds = YES;
        _toast.alpha = 0;
        [self addSubview:_toast];

        // a pinca so existe no editor: ligada no jogo, com dois dedos na tela (acelerar + direcao)
        // ela segurava o "soltou" do dedo por segundos e o botao ficava preso
        _pinch = [[UIPinchGestureRecognizer alloc] initWithTarget:self action:@selector(pinched:)];
        _pinch.cancelsTouchesInView = NO;
        _pinch.delaysTouchesBegan = NO;
        _pinch.delaysTouchesEnded = NO;
        _pinch.enabled = NO;
        [self addGestureRecognizer:_pinch];
    }
    return self;
}

- (UIButton *)toolButton:(NSString *)title action:(SEL)sel {
    UIButton *b = [UIButton buttonWithType:UIButtonTypeSystem];
    [b setTitle:title forState:UIControlStateNormal];
    b.titleLabel.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];
    [b setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    b.titleLabel.adjustsFontSizeToFitWidth = YES;
    b.titleLabel.minimumScaleFactor = 0.7;
    [b addTarget:self action:sel forControlEvents:UIControlEventTouchUpInside];
    return b;
}

- (CAShapeLayer *)ring:(BOOL)base {
    CAShapeLayer *l = [CAShapeLayer layer];
    l.fillColor = [UIColor colorWithWhite:1 alpha:base ? 0.08 : 0.35].CGColor;
    l.strokeColor = [UIColor colorWithWhite:1 alpha:0.5].CGColor;
    l.lineWidth = 1.5;
    l.hidden = YES;
    [self.layer addSublayer:l];
    return l;
}

// ---- geometria
- (CGFloat)sx { return self.bounds.size.width / kRefW; }
- (CGFloat)sy { return self.bounds.size.height / kRefH; }
- (CGRect)rectForPlace:(GTMPlace)p {
    CGFloat d = p.d * [self sy];
    return CGRectMake(p.x * [self sx] - d / 2, p.y * [self sy] - d / 2, d, d);
}
- (CGFloat)topBand { return 68 * [self sy]; }
- (BOOL)buttonsOnly { return _context == CTX_MENU || _context == CTX_CUTSCENE; }

- (GTMPlace)placeFor:(const GTMBtnDef *)def {
    GTMPlace p = { def->x, def->y, def->d };
    NSArray *o = _layoutKey ? gOverrides[_layoutKey][[NSString stringWithUTF8String:def->key]] : nil;
    if ([o isKindOfClass:NSArray.class] && o.count == 3) {
        p.x = [o[0] doubleValue]; p.y = [o[1] doubleValue]; p.d = [o[2] doubleValue];
    }
    return p;
}

- (void)rebuild {
    for (UIView *v in _buttons) [v removeFromSuperview];
    [_buttons removeAllObjects];
    NSString *key = nil;
    _defs = LayoutFor(_context, &_count, &key);
    _layoutKey = key;
    for (int i = 0; i < _count; i++) {
        GTMButtonView *v = [[GTMButtonView alloc] initWithDef:&_defs[i]];
        v.place = [self placeFor:&_defs[i]];
        v.frame = [self rectForPlace:v.place];
        v.editing = _editing;
        [self insertSubview:v belowSubview:_pencil];
        [_buttons addObject:v];
    }
    for (int s = 0; s < 12; s++)                        // os analogicos seguem (ex.: correndo e caindo na agua)
        if (_slots[s].kind == T_BUTTON || _slots[s].kind == T_EDIT || [self buttonsOnly]) _slots[s].kind = T_NONE, _slots[s].touch = nil;
    _selected = -1;
    [self applyState];
    [self refreshPressed];
    [self setNeedsLayout];
}

// mostra/esconde, acende/apaga e troca icones conforme o estado do jogo
- (const char *)dynamicIconFor:(const GTMBtnDef *)def {
    switch (def->dyn) {
        case DYN_WEAPON:  return _weapon < 47 ? kWeaponIcon[_weapon] : NULL;
        case DYN_VEHICLE: return _nearVeh < 12 ? kVehicleIcon[_nearVeh] : NULL;
        case DYN_GROUP:   return (_state & S_TARGETPED) ? "FistBump" : NULL;
        case DYN_ACTION:  return (_state & S_ACTWEAPON) ? "hud_swapgun" : (_state & S_ACTPHONE) ? "hud_Cellphone"
                               : (_state & S_ACTOTHER) ? "hud_check" : NULL;     // sem pedido: texto "Status"
        default:          return NULL;
    }
}
- (void)applyState {
    BOOL released = NO;
    for (int i = 0; i < (int)_buttons.count; i++) {
        GTMButtonView *v = _buttons[i];
        const GTMBtnDef *d = v.def;
        BOOL hide = (_state & d->need) != d->need;
        v.contextHidden = hide;
        v.dimmed = (_state & d->lit) != d->lit;
        if (d->dyn) [v setIconName:[self dynamicIconFor:d]];
        if (hide && !_editing)                          // sumiu com o dedo em cima: solta
            for (int s = 0; s < 12; s++)
                if (_slots[s].kind == T_BUTTON && _slots[s].button == i) { _slots[s].kind = T_NONE; _slots[s].touch = nil; released = YES; }
    }
    if (released) [self refreshPressed];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    for (GTMButtonView *v in _buttons) v.frame = [self rectForPlace:v.place];
    GTMPlace pp = { 240, 58, 34 };
    _pencil.frame = [self rectForPlace:pp];
    _pencil.layer.cornerRadius = _pencil.bounds.size.width / 2;
    _pencil.hidden = _editing || !_live;
    CGFloat W = self.bounds.size.width, tw = MIN(560, W - 40);
    CGFloat th = 92, ty = _toolbarLow ? self.bounds.size.height - th - 12 * [self sy] : 82 * [self sy];
    _toolbar.frame = CGRectMake((W - tw) / 2, ty, tw, th);
    UILabel *hint = [_toolbar viewWithTag:10];
    hint.frame = CGRectMake(12, 6, tw - 24, 40);
    CGFloat bw = (tw - 40) / 4;                         // Restaurar | Caixa para cima/baixo | Giroscopio | Concluir
    [_toolbar viewWithTag:12].frame = CGRectMake(20, 50, bw, 34);
    [_toolbar viewWithTag:13].frame = CGRectMake(20 + bw, 50, bw, 34);
    [_toolbar viewWithTag:14].frame = CGRectMake(20 + 2 * bw, 50, bw, 34);
    [_toolbar viewWithTag:11].frame = CGRectMake(20 + 3 * bw, 50, bw, 34);
    if (_gyroPanel) {
        CGFloat pw = MIN(440, W - 40), ph = MIN(360, self.bounds.size.height - 24);
        _gyroPanel.frame = CGRectMake((W - pw) / 2, (self.bounds.size.height - ph) / 2, pw, ph);
        [self layoutGyroPanel];
    }
    [self layoutToast];
}

- (void)layoutToast {
    CGSize s = [_toast sizeThatFits:CGSizeMake(400, 40)];
    CGFloat w = s.width + 32, h = 38;
    _toast.frame = CGRectMake((self.bounds.size.width - w) / 2, 165 * [self sy], w, h);
}

- (void)showToast:(NSString *)text {
    _toast.text = text;
    [self layoutToast];
    [_toast.layer removeAllAnimations];
    _toast.alpha = 1;
    [UIView animateWithDuration:0.4 delay:2.2 options:UIViewAnimationOptionBeginFromCurrentState
                     animations:^{ self->_toast.alpha = 0; } completion:nil];
}

- (int)buttonAt:(CGPoint)p {
    int best = -1; CGFloat bestDist = CGFLOAT_MAX;
    for (int i = 0; i < (int)_buttons.count; i++) {
        GTMButtonView *v = _buttons[i];
        if (v.contextHidden && !_editing) continue;
        CGRect r = [self rectForPlace:v.place];
        CGFloat dx = p.x - CGRectGetMidX(r), dy = p.y - CGRectGetMidY(r);
        CGFloat dist = sqrt(dx * dx + dy * dy);
        if (dist <= r.size.width / 2 * 1.15 && dist < bestDist) { best = i; bestDist = dist; }
    }
    return best;
}

// fora dos botoes, a faixa do topo fica para o Madeira (menu da sessao); no menu e na
// cutscene so os botoes sao nossos; no editor, tudo e nosso
- (BOOL)pointInside:(CGPoint)p withEvent:(UIEvent *)e {
    if (_editing) return YES;
    if (!_live) return NO;
    if (!_pencil.hidden && CGRectContainsPoint(CGRectInset(_pencil.frame, -6, -6), p)) return YES;
    if ([self buttonAt:p] >= 0) return YES;
    if ([self buttonsOnly]) return NO;
    return p.y > [self topBand];
}

// ---- toques
- (GTMSlot *)slotFor:(UITouch *)t create:(BOOL)create {
    for (int s = 0; s < 12; s++) if (_slots[s].touch == t && _slots[s].kind != T_NONE) return &_slots[s];
    if (!create) return NULL;
    for (int s = 0; s < 12; s++) if (_slots[s].kind == T_NONE) return &_slots[s];
    return NULL;
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *t in touches) {
        CGPoint p = [t locationInView:self];
        if (_editing) {
            if (CGRectContainsPoint(_toolbar.frame, p)) continue;
            if (_gyroPanel && !_gyroPanel.hidden && CGRectContainsPoint(_gyroPanel.frame, p)) continue;
            int b = [self buttonAt:p];
            [self selectForEdit:b];
            if (b < 0) continue;
            GTMSlot *s = [self slotFor:t create:YES];
            if (!s) continue;
            CGRect r = [self rectForPlace:_buttons[b].place];
            s->touch = t; s->kind = T_EDIT; s->button = b; s->start = p; s->cur = p;
            s->grab = CGPointMake(p.x - CGRectGetMidX(r), p.y - CGRectGetMidY(r));
            continue;
        }
        int b = [self buttonAt:p];
        if (b < 0 && [self buttonsOnly]) continue;
        GTMSlot *s = [self slotFor:t create:YES];
        if (!s) continue;
        s->touch = t; s->start = p; s->cur = p;
        if (b >= 0 && _buttons[b].def->swipe) {
            s->kind = T_SWIPE; s->button = b;
        } else if (b >= 0) {
            s->kind = T_BUTTON; s->button = b; [_haptic impactOccurred];
            uint8_t cmd = _buttons[b].def->cmd;
            if (cmd) { _cmdSeq++; _hostCmd = cmd; }
        } else s->kind = (p.x < self.bounds.size.width / 2) ? T_LSTICK : T_RSTICK;
    }
    [self refreshPressed];
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *t in touches) {
        GTMSlot *s = [self slotFor:t create:NO];
        if (!s) continue;
        s->cur = [t locationInView:self];
        if (s->kind == T_EDIT && s->button < (int)_buttons.count) {
            GTMButtonView *v = _buttons[s->button];
            GTMPlace p = v.place;
            p.x = (s->cur.x - s->grab.x) / [self sx];
            p.y = (s->cur.y - s->grab.y) / [self sy];
            p.x = MAX(p.d / 2, MIN(kRefW - p.d / 2, p.x));
            p.y = MAX(p.d / 2, MIN(kRefH - p.d / 2, p.y));
            v.place = p;
            v.frame = [self rectForPlace:p];
        } else if (s->kind == T_BUTTON) {               // deslizar para outro botao troca o botao (comandos nao)
            int b = [self buttonAt:s->cur];
            if (b >= 0 && b != s->button && !_buttons[b].def->cmd && !_buttons[s->button].def->cmd) { s->button = b; [_haptic impactOccurred]; }
        }
    }
    [self refreshPressed];
}
- (void)endTouches:(NSSet<UITouch *> *)touches {
    for (UITouch *t in touches) {
        GTMSlot *s = [self slotFor:t create:NO];
        if (!s) continue;
        if (s->kind == T_SWIPE) {                       // esquerda volta a arma, direita avanca, toque recarrega
            CGFloat dx = s->cur.x - s->start.x, minDx = 18 * [self sy];
            if (dx < -minDx || dx > minDx) {
                _pulseBits = dx < 0 ? B_LT : B_RT;
                _pulseUntil = CACurrentMediaTime() + 0.12;
                [_haptic impactOccurred];
            } else if (_state & S_RELOAD) {             // so quando o jogo diz que da; senao nada (nem vibra)
                _cmdSeq++; _hostCmd = CMD_RELOAD;
                [_haptic impactOccurred];
            }
        }
        s->kind = T_NONE; s->touch = nil;
    }
    [self refreshPressed];
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self endTouches:touches]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self endTouches:touches]; }

- (void)refreshPressed {
    BOOL pressed[64] = {0};
    for (int s = 0; s < 12; s++) if (_slots[s].kind == T_BUTTON && _slots[s].button < 64) pressed[_slots[s].button] = YES;
    for (int i = 0; i < (int)_buttons.count && i < 64; i++) _buttons[i].pressed = _editing ? NO : pressed[i];
    [self drawStick:T_LSTICK base:_lBase knob:_lKnob];
    [self drawStick:T_RSTICK base:_rBase knob:_rKnob];
}

- (CGFloat)stickReach:(GTMTouchKind)k { return (k == T_LSTICK ? 54 : 60) * [self sy]; }

- (void)drawStick:(GTMTouchKind)k base:(CAShapeLayer *)base knob:(CAShapeLayer *)knob {
    GTMSlot *s = NULL;
    for (int i = 0; i < 12; i++) if (_slots[i].kind == k) { s = &_slots[i]; break; }
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    if (!s) { base.hidden = knob.hidden = YES; [CATransaction commit]; return; }
    CGFloat R = [self stickReach:k], r = R * 0.45;
    CGFloat dx = s->cur.x - s->start.x, dy = s->cur.y - s->start.y, len = sqrt(dx * dx + dy * dy);
    if (len > R) { dx *= R / len; dy *= R / len; }
    base.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(s->start.x - R, s->start.y - R, 2 * R, 2 * R)].CGPath;
    knob.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(s->start.x + dx - r, s->start.y + dy - r, 2 * r, 2 * r)].CGPath;
    base.hidden = knob.hidden = NO;
    [CATransaction commit];
}

- (void)stick:(GTMTouchKind)k x:(int16_t *)ox y:(int16_t *)oy {
    *ox = *oy = 0;
    for (int i = 0; i < 12; i++) if (_slots[i].kind == k) {
        CGFloat R = [self stickReach:k];
        CGFloat dx = (_slots[i].cur.x - _slots[i].start.x) / R, dy = (_slots[i].cur.y - _slots[i].start.y) / R;
        CGFloat len = sqrt(dx * dx + dy * dy);
        if (len > 1) { dx /= len; dy /= len; }
        *ox = (int16_t)lround(dx * 32767);
        *oy = (int16_t)lround(-dy * 32767);           // XInput: para cima e positivo
        return;
    }
}

// ---- editor
- (void)startEditing {
    if (!_layoutKey) return;
    _editing = YES;
    _pinch.enabled = YES;
    for (int s = 0; s < 12; s++) _slots[s].kind = T_NONE, _slots[s].touch = nil;
    for (GTMButtonView *v in _buttons) v.editing = YES;
    UILabel *hint = [_toolbar viewWithTag:10];
    hint.text = [NSString stringWithFormat:@"Layout %@: arraste para mover, pinça para o tamanho.\nOs apagados só aparecem quando o jogo pede.", LayoutTitle(_layoutKey)];
    _toolbar.hidden = NO;
    [self refreshPressed];
    [self setNeedsLayout];
}

- (void)moveToolbar {
    _toolbarLow = !_toolbarLow;
    UIButton *move = [_toolbar viewWithTag:13];
    [move setTitle:(_toolbarLow ? @"Caixa para cima" : @"Caixa para baixo") forState:UIControlStateNormal];
    [UIView animateWithDuration:0.2 animations:^{ [self setNeedsLayout]; [self layoutIfNeeded]; }];
}

- (void)selectForEdit:(int)b {
    _selected = b;
    for (int i = 0; i < (int)_buttons.count; i++) _buttons[i].selectedForEdit = (i == b);
}

- (void)pinched:(UIPinchGestureRecognizer *)g {
    if (!_editing || _selected < 0 || _selected >= (int)_buttons.count) return;
    GTMButtonView *v = _buttons[_selected];
    GTMPlace p = v.place;
    if (g.state == UIGestureRecognizerStateBegan) _pinchBase = p.d;
    p.d = MAX(28, MIN(150, _pinchBase * g.scale));
    v.place = p;
    v.frame = [self rectForPlace:p];
}

- (void)finishEditing {
    NSMutableDictionary *m = [NSMutableDictionary dictionary];
    for (GTMButtonView *v in _buttons) {
        GTMPlace d = { v.def->x, v.def->y, v.def->d }, p = v.place;
        if (fabs(d.x - p.x) > 0.5 || fabs(d.y - p.y) > 0.5 || fabs(d.d - p.d) > 0.5)
            m[[NSString stringWithUTF8String:v.def->key]] = @[@(round(p.x * 10) / 10), @(round(p.y * 10) / 10), @(round(p.d * 10) / 10)];
    }
    if (m.count) gOverrides[_layoutKey] = m; else [gOverrides removeObjectForKey:_layoutKey];
    GTMSaveOverrides();
    [self leaveEditing];
    [self showToast:@"Layout salvo"];
}

- (void)leaveEditing {
    if (_gyroPanel && !_gyroPanel.hidden) { _gyroPanel.hidden = YES; GTMSaveGyro(); }
    _editing = NO;
    _pinch.enabled = NO;
    _toolbar.hidden = YES;
    [self selectForEdit:-1];
    for (int s = 0; s < 12; s++) _slots[s].kind = T_NONE, _slots[s].touch = nil;
    [self rebuild];
}

// ---- painel do giroscopio (no editor)
enum { GYT_ON = 101, GYT_ALWAYS, GYT_H, GYT_V, GYT_AIM, GYT_INVERT, GYT_ZOOM, GYT_HL, GYT_VL, GYT_AIML };
- (UILabel *)gyroLabel:(NSString *)text frame:(CGRect)f {
    UILabel *l = [[UILabel alloc] initWithFrame:f];
    l.text = text; l.textColor = UIColor.whiteColor; l.font = [UIFont systemFontOfSize:14];
    l.adjustsFontSizeToFitWidth = YES; l.minimumScaleFactor = 0.75;
    return l;
}
- (void)buildGyroPanel {
    UIView *p = [UIView new];
    p.backgroundColor = [UIColor colorWithWhite:0.08 alpha:0.94];
    p.layer.cornerRadius = 14;
    p.layer.borderWidth = 1;
    p.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.2].CGColor;
    p.hidden = YES;
    UIScrollView *sc = [UIScrollView new];
    sc.tag = 100;
    [p addSubview:sc];
    CGFloat W = 400, y = 8;
    UILabel *t = [self gyroLabel:@"Giroscópio" frame:CGRectMake(16, y, W - 32, 26)];
    t.font = [UIFont systemFontOfSize:17 weight:UIFontWeightBold];
    [sc addSubview:t]; y += 32;
    static const struct { const char *text; int tag; int kind; double lo, hi; int labelTag; } rows[] = {
        { "Ligado", GYT_ON, 0, 0, 0, 0 },
        { "Fora da mira também (só a câmera)", GYT_ALWAYS, 0, 0, 0, 0 },
        { "Horizontal", GYT_H, 1, 0.2, 4.0, GYT_HL },
        { "Vertical", GYT_V, 1, 0.2, 4.0, GYT_VL },
        { "Na mira (mais preciso)", GYT_AIM, 1, 0.2, 1.5, GYT_AIML },
        { "Inverter vertical", GYT_INVERT, 0, 0, 0, 0 },
        { "Mais lento com zoom", GYT_ZOOM, 0, 0, 0, 0 },
    };
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        NSString *text = [NSString stringWithUTF8String:rows[i].text];
        if (rows[i].kind == 0) {
            [sc addSubview:[self gyroLabel:text frame:CGRectMake(16, y, W - 100, 31)]];
            UISwitch *sw = [UISwitch new];
            sw.tag = rows[i].tag;
            sw.frame = CGRectMake(W - 16 - 51, y, 51, 31);
            [sw addTarget:self action:@selector(gyroChanged:) forControlEvents:UIControlEventValueChanged];
            [sc addSubview:sw];
        } else {
            [sc addSubview:[self gyroLabel:text frame:CGRectMake(16, y, 150, 31)]];
            UISlider *sl = [[UISlider alloc] initWithFrame:CGRectMake(166, y, W - 166 - 70, 31)];
            sl.tag = rows[i].tag; sl.minimumValue = rows[i].lo; sl.maximumValue = rows[i].hi;
            [sl addTarget:self action:@selector(gyroChanged:) forControlEvents:UIControlEventValueChanged];
            [sl addTarget:self action:@selector(gyroSave) forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside | UIControlEventTouchCancel];
            [sc addSubview:sl];
            UILabel *v = [self gyroLabel:@"" frame:CGRectMake(W - 64, y, 50, 31)];
            v.tag = rows[i].labelTag; v.textAlignment = NSTextAlignmentRight;
            v.font = [UIFont monospacedDigitSystemFontOfSize:14 weight:UIFontWeightSemibold];
            [sc addSubview:v];
        }
        y += 38;
    }
    UIButton *def = [self toolButton:@"Valores padrão" action:@selector(askGyroDefaults)];
    [def setTitleColor:[UIColor colorWithRed:1 green:0.45 blue:0.4 alpha:1] forState:UIControlStateNormal];
    def.frame = CGRectMake(16, y, (W - 32) / 2, 34);
    UIButton *close = [self toolButton:@"Fechar" action:@selector(toggleGyroPanel)];
    close.frame = CGRectMake(16 + (W - 32) / 2, y, (W - 32) / 2, 34);
    [sc addSubview:def]; [sc addSubview:close];
    y += 44;
    sc.contentSize = CGSizeMake(W, y);
    _gyroPanel = p;
    [self addSubview:p];
}
- (void)layoutGyroPanel {
    UIScrollView *sc = [_gyroPanel viewWithTag:100];
    CGFloat W = 400;
    sc.frame = CGRectMake((_gyroPanel.bounds.size.width - W) / 2, 0, W, _gyroPanel.bounds.size.height);
}
- (void)showGyroValues {
    ((UISwitch *)[_gyroPanel viewWithTag:GYT_ON]).on = gGyro.on;
    ((UISwitch *)[_gyroPanel viewWithTag:GYT_ALWAYS]).on = gGyro.always;
    ((UISwitch *)[_gyroPanel viewWithTag:GYT_INVERT]).on = gGyro.invert;
    ((UISwitch *)[_gyroPanel viewWithTag:GYT_ZOOM]).on = gGyro.zoom;
    ((UISlider *)[_gyroPanel viewWithTag:GYT_H]).value = gGyro.sensH;
    ((UISlider *)[_gyroPanel viewWithTag:GYT_V]).value = gGyro.sensV;
    ((UISlider *)[_gyroPanel viewWithTag:GYT_AIM]).value = gGyro.aim;
    [self showGyroNumbers];
}
- (void)showGyroNumbers {
    NSString *(^x)(double) = ^NSString *(double v) { return [[NSString stringWithFormat:@"%.1fx", v] stringByReplacingOccurrencesOfString:@"." withString:@","]; };
    ((UILabel *)[_gyroPanel viewWithTag:GYT_HL]).text = x(gGyro.sensH);
    ((UILabel *)[_gyroPanel viewWithTag:GYT_VL]).text = x(gGyro.sensV);
    ((UILabel *)[_gyroPanel viewWithTag:GYT_AIML]).text = [NSString stringWithFormat:@"%d%%", (int)lround(gGyro.aim * 100)];
    BOOL on = gGyro.on;
    for (int tag = GYT_ALWAYS; tag <= GYT_AIML; tag++) {
        UIView *v = [_gyroPanel viewWithTag:tag];
        v.alpha = on ? 1 : 0.4;
        if ([v isKindOfClass:UIControl.class]) ((UIControl *)v).enabled = on;
    }
}
- (void)toggleGyroPanel {
    if (!_gyroPanel) [self buildGyroPanel];
    BOOL show = _gyroPanel.hidden;
    if (!show) GTMSaveGyro();
    _gyroPanel.hidden = !show;
    _toolbar.hidden = show;                             // a caixa do editor volta quando o painel fecha
    if (show) { [self showGyroValues]; [self bringSubviewToFront:_gyroPanel]; }
    [self setNeedsLayout];
    [self layoutIfNeeded];
}
- (void)gyroChanged:(UIControl *)c {
    // sliders andam de 0,1 em 0,1 (na mira, de 5 em 5%)
    switch (c.tag) {
        case GYT_ON:     gGyro.on = ((UISwitch *)c).on; break;
        case GYT_ALWAYS: gGyro.always = ((UISwitch *)c).on; break;
        case GYT_INVERT: gGyro.invert = ((UISwitch *)c).on; break;
        case GYT_ZOOM:   gGyro.zoom = ((UISwitch *)c).on; break;
        case GYT_H:      gGyro.sensH = round(((UISlider *)c).value * 10) / 10; break;
        case GYT_V:      gGyro.sensV = round(((UISlider *)c).value * 10) / 10; break;
        case GYT_AIM:    gGyro.aim = round(((UISlider *)c).value * 20) / 20; break;
    }
    [GTMGyro.shared setConfig:gGyro];
    [self showGyroNumbers];
    if ([c isKindOfClass:UISwitch.class]) GTMSaveGyro();
}
- (void)gyroSave { GTMSaveGyro(); }
- (void)askGyroDefaults {
    UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Tem certeza?" message:@"O giroscópio volta aos ajustes originais."
                                                        preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"Cancelar" style:UIAlertActionStyleCancel handler:nil]];
    [a addAction:[UIAlertAction actionWithTitle:@"Restaurar" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *x) {
        gGyro = GTMGyroDefaults();
        GTMSaveGyro();
        [GTMGyro.shared setConfig:gGyro];
        [self showGyroValues];
        [self showToast:@"Giroscópio no padrão"];
    }]];
    [self.host presentViewController:a animated:YES completion:nil];
}

- (void)askReset {
    NSString *msg = [NSString stringWithFormat:@"O layout %@ volta às posições e tamanhos originais.", LayoutTitle(_layoutKey)];
    UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Tem certeza?" message:msg
                                                        preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"Cancelar" style:UIAlertActionStyleCancel handler:nil]];
    [a addAction:[UIAlertAction actionWithTitle:@"Restaurar" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *x) {
        [gOverrides removeObjectForKey:self->_layoutKey];
        GTMSaveOverrides();
        [self leaveEditing];
        [self showToast:@"Layout padrão restaurado"];
    }]];
    [self.host presentViewController:a animated:YES completion:nil];
}

// ---- teclado pedido pelo jogo (placa da garagem)
- (void)keyboardTick {
    GTMKbRequest r = {0};
    BOOL valid = pread(gFd, &r, sizeof r, kKbReqOffset) == sizeof r && r.magic == kKbReqMagic;
    uint32_t seq = valid ? r.seq : 0;
    if (_kbSeq && seq != _kbSeq) {                      // o jogo desistiu (ou outro pedido): fecha o alerta
        [self closeKeyboardAlert];
        _kbSeq = 0;
    }
    if (!seq || seq == _kbSeq || seq == _kbDone || self.host.presentedViewController) return;
    _kbSeq = seq;
    r.title[sizeof r.title - 1] = 0; r.text[sizeof r.text - 1] = 0;
    _kbMax = (r.maxLen > 0 && r.maxLen <= 15) ? r.maxLen : 8;
    NSString *title = [NSString stringWithUTF8String:r.title] ?: @"Placa";
    NSString *cur = [NSString stringWithUTF8String:r.text] ?: @"";
    NSString *msg = [NSString stringWithFormat:@"Até %d letras ou números. Vazio volta a placa do jogo.", _kbMax];
    UIAlertController *a = [UIAlertController alertControllerWithTitle:title message:msg preferredStyle:UIAlertControllerStyleAlert];
    [a addTextFieldWithConfigurationHandler:^(UITextField *t) {
        t.text = cur;
        t.placeholder = @"GROVE 4L";
        t.autocapitalizationType = UITextAutocapitalizationTypeAllCharacters;
        t.autocorrectionType = UITextAutocorrectionTypeNo;
        t.spellCheckingType = UITextSpellCheckingTypeNo;
        t.smartDashesType = UITextSmartDashesTypeNo;
        t.smartQuotesType = UITextSmartQuotesTypeNo;
        t.keyboardType = UIKeyboardTypeASCIICapable;
        t.returnKeyType = UIReturnKeyDone;
        t.clearButtonMode = UITextFieldViewModeWhileEditing;
        t.textAlignment = NSTextAlignmentCenter;
        t.font = [UIFont monospacedSystemFontOfSize:22 weight:UIFontWeightSemibold];
        [t addTarget:self action:@selector(plateEdited:) forControlEvents:UIControlEventEditingChanged];
    }];
    __weak UIAlertController *wa = a;
    [a addAction:[UIAlertAction actionWithTitle:@"Cancelar" style:UIAlertActionStyleCancel handler:^(UIAlertAction *x) {
        [self answerKeyboard:seq result:2 text:@""];
    }]];
    [a addAction:[UIAlertAction actionWithTitle:@"Salvar" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x) {
        [self answerKeyboard:seq result:1 text:wa.textFields.firstObject.text ?: @""];
    }]];
    _kbAlert = a;
    _kbPrevKey = nil;                                   // o teclado so aparece na janela principal (key)
    for (UIWindow *w in self.window.windowScene.windows) if (w.isKeyWindow) { _kbPrevKey = w; break; }
    [self.window makeKeyWindow];
    [self.host presentViewController:a animated:YES completion:nil];
}

// so A-Z, 0-9 e espaco, em maiusculas, ate o limite
- (void)plateEdited:(UITextField *)t {
    NSString *up = (t.text ?: @"").uppercaseString;
    NSMutableString *out = [NSMutableString string];
    for (NSUInteger i = 0; i < up.length && (int)out.length < _kbMax; i++) {
        unichar c = [up characterAtIndex:i];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == ' ' && out.length > 0)) [out appendFormat:@"%C", c];
    }
    if (![out isEqualToString:t.text ?: @""]) t.text = out;
}

- (void)answerKeyboard:(uint32_t)seq result:(uint32_t)result text:(NSString *)text {
    GTMKbAnswer ans = {0};
    ans.magic = kKbAnsMagic; ans.seq = seq; ans.result = result;
    const char *u = text.UTF8String ?: "";
    strncpy(ans.text, u, sizeof ans.text - 1);
    if (gFd >= 0) pwrite(gFd, &ans, sizeof ans, kKbAnsOffset);
    _kbDone = seq;
    if (_kbSeq == seq) _kbSeq = 0;
    _kbAlert = nil;
    [self restoreKeyWindow];
    if (result == 1) [self showToast:text.length ? [NSString stringWithFormat:@"Placa: %@", text] : @"Placa do jogo"];
}

- (void)closeKeyboardAlert {
    UIAlertController *a = _kbAlert;
    _kbAlert = nil;
    if (a && a.presentingViewController) [a dismissViewControllerAnimated:YES completion:nil];
    [self restoreKeyWindow];
}

- (void)restoreKeyWindow {
    UIWindow *prev = _kbPrevKey;
    _kbPrevKey = nil;
    if (prev && prev != self.window) [prev makeKeyWindow];
}

// ---- ponte com o jogo (60 Hz)
- (void)handleSave:(GTMGameBlock *)g {
    if (!_saveSeqKnown) { _saveSeqKnown = YES; _lastSaveSeq = g->saveSeq; return; }
    if (g->saveSeq == _lastSaveSeq) return;
    _lastSaveSeq = g->saveSeq;
    int slot = g->slot ? g->slot : 8;
    NSString *text = nil;
    switch (g->saveResult) {
        case 0: text = g->saveKind == 1 ? [NSString stringWithFormat:@"Jogo salvo (slot %d)", slot]
                     : g->saveKind == 3 ? [NSString stringWithFormat:@"Missão cumprida: autosave (slot %d)", slot]
                                        : [NSString stringWithFormat:@"Autosave (slot %d)", slot]; break;
        case 1: if (g->saveKind == 1) text = @"Não dá pra salvar durante missão"; break;
        case 2: if (g->saveKind == 1) text = @"Não dá pra salvar agora"; break;
        default: text = @"Erro ao salvar"; break;
    }
    if (text) [self showToast:text];
}

- (void)tick {
    if (!GTMOpenShared()) return;
    GTMGameBlock g = {0};
    CFTimeInterval now = CACurrentMediaTime();
    BOOL valid = pread(gFd, &g, sizeof g, 0) == sizeof g && g.magic == kGameMagic && g.version >= 2;
    if (valid && g.seq != _lastGameSeq) { _lastGameSeq = g.seq; _lastGameChange = now; }
    BOOL fresh = valid && (now - _lastGameChange) < 0.4;
    int ctx = fresh ? (int)g.context : CTX_NONE;
    if (ctx > CTX_CUTSCENE) ctx = CTX_NONE;
    if (fresh && ctx != CTX_MENU) [self handleSave:&g];

    if (_editing) {                                     // o editor segura o layout aberto ate concluir
        if (ctx == CTX_NONE) { [self leaveEditing]; }
        else ctx = _context;
    }
    BOOL stateChanged = NO;
    if (fresh && (int)g.context == ctx) {               // estado so vale para o contexto em que veio
        uint32_t state = g.version >= 3 ? g.state : kCompatState;
        if (g.canSave) state |= S_CANSAVE;
        uint8_t weapon = g.version >= 3 ? g.weapon : 0xFF, nearVeh = g.version >= 3 ? g.nearVeh : 0;
        if (state != _state || weapon != _weapon || nearVeh != _nearVeh) {
            _state = state; _weapon = weapon; _nearVeh = nearVeh;
            stateChanged = YES;
        }
    }
    if (ctx != _context) {
        _context = ctx;
        [self rebuild];                                 // ja aplica o estado novo
        [UIView animateWithDuration:0.15 animations:^{ self.alpha = (ctx == CTX_NONE) ? 0 : 1; }];
    } else if (stateChanged) [self applyState];
    int gameCtx = fresh ? (int)g.context : CTX_NONE;   // o sensor segue o jogo, mesmo com o editor aberto
    UIWindowScene *scene = self.window.windowScene;
    if (scene) [GTMGyro.shared setOrientation:scene.interfaceOrientation];
    [GTMGyro.shared setRunning:gGyro.on && gameCtx > CTX_NONE && gameCtx < CTX_CUTSCENE && gameCtx != CTX_MENU];
    BOOL live = (ctx != CTX_NONE);
    if (live != _live) {
        _live = live;
        if (!live) { for (int s = 0; s < 12; s++) _slots[s].kind = T_NONE, _slots[s].touch = nil; [self refreshPressed]; }
        [self setNeedsLayout];
    }
    if (!live) return;
    [self keyboardTick];

    GTMHostBlock h = {0};
    h.magic = kHostMagic;
    h.seq = ++_hostSeq;
    h.version = 5;
    h.cmdSeq = _cmdSeq; h.cmd = _hostCmd;
    if (!_editing) {
        uint32_t bits = 0;
        for (int s = 0; s < 12; s++)
            if (_slots[s].kind == T_BUTTON && _slots[s].button < (int)_buttons.count) bits |= _buttons[_slots[s].button].def->bits;
        if (now < _pulseUntil) bits |= _pulseBits;
        h.buttons = (uint16_t)(bits & 0xFFFF);
        h.lt = (bits & B_LT) ? 255 : 0;
        h.rt = (bits & B_RT) ? 255 : 0;
        uint16_t raw = (uint16_t)(((bits & B_RAWL) ? B_LEFT : 0) | ((bits & B_RAWR) ? B_RIGHT : 0));
        h.rawButtons = raw;                             // dirigindo, setas normais viram direcao; Sim/Nao vao por fora
        BOOL driving = _context == CTX_CAR || _context == CTX_BIKE || _context == CTX_HELI || _context == CTX_PLANE || _context == CTX_BOAT;
        if (!driving) h.buttons |= raw;
        int16_t lx, ly, rx, ry;
        [self stick:T_LSTICK x:&lx y:&ly];
        [self stick:T_RSTICK x:&rx y:&ry];
        // ▲ ▼: mesma rampa da direcao do .asi (150 ms para ir ao fim, 60 ms para voltar)
        int target = ((bits & B_STKUP) ? 1 : 0) - ((bits & B_STKDN) ? 1 : 0);
        double dt = _pitchTime > 0 ? MIN(0.1, now - _pitchTime) : 0;
        _pitchTime = now;
        double span = (target != 0 && _pitch * target >= 0) ? 0.150 : 0.060;
        double step = dt / span;
        if (_pitch < target) _pitch = MIN((double)target, _pitch + step);
        else if (_pitch > target) _pitch = MAX((double)target, _pitch - step);
        int16_t py = (int16_t)lround(_pitch * 32767);
        if (abs(py) > abs(ly)) ly = py;
        h.lx = lx; h.ly = ly; h.rx = rx; h.ry = ry;
    }
    pwrite(gFd, &h, 32, kHostOffset);                   // so os campos; [96..127] e do giroscopio
}
@end

// ------------------------------------------------------------------ janela por cima do Madeira
@interface GTMWindow : UIWindow @end
@implementation GTMWindow
- (UIView *)hitTest:(CGPoint)p withEvent:(UIEvent *)e {
    UIView *v = [super hitTest:p withEvent:e];
    return (v == self) ? nil : v;          // a propria view decide (pointInside) o que passa para o Madeira
}
@end

@interface GTMRootVC : UIViewController @end
@implementation GTMRootVC
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskAll; }
- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
@end

@interface GTMController : NSObject
@property (nonatomic, strong) GTMWindow *window;
@property (nonatomic, strong) GTMPadView *pad;
@property (nonatomic, strong) CADisplayLink *link;
@end

@implementation GTMController
+ (instancetype)shared { static GTMController *c; static dispatch_once_t once; dispatch_once(&once, ^{ c = [GTMController new]; }); return c; }

- (void)attachIfNeeded {
    if (self.window) return;
    UIWindowScene *scene = nil;
    for (UIScene *s in UIApplication.sharedApplication.connectedScenes)
        if ([s isKindOfClass:UIWindowScene.class] && s.activationState == UISceneActivationStateForegroundActive) { scene = (UIWindowScene *)s; break; }
    if (!scene) return;
    GTMWindow *w = [[GTMWindow alloc] initWithWindowScene:scene];
    w.windowLevel = UIWindowLevelNormal + 5;
    w.backgroundColor = UIColor.clearColor;
    GTMRootVC *vc = [GTMRootVC new];
    GTMPadView *pad = [[GTMPadView alloc] initWithFrame:w.bounds];
    pad.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    pad.host = vc;
    vc.view = pad;
    w.rootViewController = vc;
    w.hidden = NO;                                      // visivel, mas nunca key: o teclado e o foco seguem no Madeira
    self.window = w; self.pad = pad;
    self.link = [CADisplayLink displayLinkWithTarget:self selector:@selector(frame:)];
    self.link.preferredFrameRateRange = CAFrameRateRangeMake(30, 60, 60);
    [self.link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    NSLog(@"[GTMTouch] overlay 3.4 ligado; arquivo %@", GTMSharedPath());
}
- (void)frame:(CADisplayLink *)l { [self.pad tick]; }
- (void)sceneEvent:(NSNotification *)n { [self attachIfNeeded]; }
@end

__attribute__((constructor)) static void GTMTouchInit(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        GTMController *c = GTMController.shared;
        [NSNotificationCenter.defaultCenter addObserver:c selector:@selector(sceneEvent:) name:UISceneDidActivateNotification object:nil];
        for (int i = 1; i <= 10; i++)
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(i * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [c attachIfNeeded]; });
    });
}
