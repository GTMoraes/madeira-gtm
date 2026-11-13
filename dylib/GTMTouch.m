// GTMTouch.dylib - overlay de toque contextual para o GTA San Andreas no Madeira.
//
// Injetado no IPA oficial do Madeira. Desenha os controles por cima do jogo e
// troca o conjunto de botoes sozinho conforme o contexto que o GTMTouchSA.asi
// escreve em Documents/wine/drive_c/gtmtouch.bin (C:\gtmtouch.bin para o jogo).
// Os toques voltam pelo mesmo arquivo; o .asi entrega ao GInput como um controle.
//
// Sem jogo rodando (ou com o menu de pausa aberto) o overlay some e todo toque
// passa direto para o Madeira.

#import <UIKit/UIKit.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <math.h>

// ------------------------------------------------------------------ protocolo
typedef struct __attribute__((packed)) {
    uint32_t magic; uint16_t version, flags; uint32_t seq, context; uint8_t pad[16];
} GTMGameBlock;
typedef struct __attribute__((packed)) {
    uint32_t magic, seq; uint16_t buttons; uint8_t lt, rt; int16_t lx, ly, rx, ry; uint8_t pad[12];
} GTMHostBlock;
_Static_assert(sizeof(GTMGameBlock) == 32 && sizeof(GTMHostBlock) == 32, "layout");
static const uint32_t kGameMagic = 0x544D5447;   // "GTMT"
static const uint32_t kHostMagic = 0x484D5447;   // "GTMH"

enum { CTX_NONE = 0, CTX_FOOT, CTX_CAR, CTX_BIKE, CTX_HELI, CTX_PLANE, CTX_BOAT, CTX_PASSENGER };

// bits XInput; LT/RT viram gatilhos
enum {
    B_UP = 0x1, B_DOWN = 0x2, B_LEFT = 0x4, B_RIGHT = 0x8, B_START = 0x10, B_BACK = 0x20,
    B_L3 = 0x40, B_R3 = 0x80, B_LB = 0x100, B_RB = 0x200, B_A = 0x1000, B_B = 0x2000,
    B_X = 0x4000, B_Y = 0x8000, B_LT = 0x10000, B_RT = 0x20000
};

// ------------------------------------------------------------------ layouts
// Posicoes em pontos numa tela de referencia de 874 x 402 (iPhone 6,3" deitado);
// d = diametro. Tudo e escalado para a tela real.
typedef struct { const char *label; uint32_t bits; double x, y, d; } GTMBtn;
static const double kRefW = 874.0, kRefH = 402.0;

static const GTMBtn kFoot[] = {
    {"Atirar",  B_B,     765, 315, 74}, {"Correr",  B_A,     680, 345, 61},
    {"Pular",   B_X,     680, 255, 61}, {"Entrar",  B_Y,     765, 225, 61},
    {"Mirar",   B_RB,    765, 150, 54}, {"◀ Arma",  B_LT,    640, 100, 46},
    {"Arma ▶",  B_RT,    730, 100, 46}, {"Ação",    B_LB,    110, 140, 51},
    {"Agachar", B_L3,    110, 200, 51}, {"Não",     B_LEFT,  220, 100, 40},
    {"Sim",     B_RIGHT, 280, 100, 40}, {"Câmera",  B_BACK,  390, 112, 51},
    {"Pausa",   B_START, 484, 112, 51},
};
#define CAR_COMMON \
    {"◀",       B_LEFT,  130, 315, 83}, {"▶",       B_RIGHT, 245, 315, 83}, \
    {"Freio de\nmão", B_RB, 765, 225, 58}, {"Atirar", B_B,   665, 240, 54}, \
    {"Sair",    B_Y,     770, 140, 54}, {"Buzina",  B_L3,    187, 225, 58}, \
    {"Missão",  B_R3,    110, 150, 46}, {"Rádio +", B_UP,    220, 100, 44}, \
    {"Rádio −", B_DOWN,  280, 100, 44}, {"Câmera",  B_BACK,  390, 112, 51}, \
    {"Pausa",   B_START, 484, 112, 51}
static const GTMBtn kCar[] = {
    {"Acelerar", B_A, 765, 315, 83}, {"Freio / ré", B_X, 665, 330, 70}, CAR_COMMON
};
static const GTMBtn kHeli[] = {
    {"Subir", B_A, 765, 315, 83}, {"Descer", B_X, 665, 330, 70}, CAR_COMMON,
    {"Girar ◀", B_LT, 640, 160, 46}, {"Girar ▶", B_RT, 640, 85, 46},
};
static const GTMBtn kPlane[] = {
    {"Acelerar", B_A, 765, 315, 83}, {"Frear", B_X, 665, 330, 70}, CAR_COMMON,
    {"Leme ◀", B_LT, 640, 160, 46}, {"Leme ▶", B_RT, 640, 85, 46},
};
static const GTMBtn kPassenger[] = {
    {"Atirar", B_B, 765, 315, 74}, {"Sair", B_Y, 770, 140, 54},
    {"Rádio +", B_UP, 220, 100, 44}, {"Rádio −", B_DOWN, 280, 100, 44},
    {"Câmera", B_BACK, 390, 112, 51}, {"Pausa", B_START, 484, 112, 51},
};

static const GTMBtn *LayoutFor(int ctx, int *count) {
#define L(a) do { *count = (int)(sizeof(a) / sizeof(a[0])); return a; } while (0)
    switch (ctx) {
        case CTX_FOOT: L(kFoot);
        case CTX_CAR: case CTX_BIKE: case CTX_BOAT: L(kCar);
        case CTX_HELI: L(kHeli);
        case CTX_PLANE: L(kPlane);
        case CTX_PASSENGER: L(kPassenger);
        default: *count = 0; return NULL;
    }
#undef L
}

// ------------------------------------------------------------------ ponte de arquivo
static int gFd = -1;
static NSString *GTMSharedPath(void) {
    NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    return [docs stringByAppendingPathComponent:@"wine/drive_c/gtmtouch.bin"];
}
static BOOL GTMOpenShared(void) {
    if (gFd >= 0) return YES;
    NSString *path = GTMSharedPath();
    NSString *dir = [path stringByDeletingLastPathComponent];
    if (![[NSFileManager defaultManager] fileExistsAtPath:dir]) return NO;   // Madeira ainda sem prefixo
    gFd = open(path.fileSystemRepresentation, O_RDWR | O_CREAT, 0644);
    return gFd >= 0;
}

// ------------------------------------------------------------------ toques
typedef enum { T_NONE = 0, T_BUTTON, T_LSTICK, T_RSTICK } GTMTouchKind;
typedef struct { __unsafe_unretained UITouch *touch; GTMTouchKind kind; int button; CGPoint start, cur; } GTMSlot;

@interface GTMPadView : UIView
@property (nonatomic) int context;
@property (nonatomic) BOOL live;
- (void)tick;
@end

@implementation GTMPadView {
    GTMSlot _slots[12];
    const GTMBtn *_layout;
    int _count;
    NSMutableArray<UIView *> *_buttonViews;
    CAShapeLayer *_lBase, *_lKnob, *_rBase, *_rKnob;
    uint32_t _hostSeq, _lastGameSeq;
    CFTimeInterval _lastGameChange;
    UIImpactFeedbackGenerator *_haptic;
}

- (instancetype)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.multipleTouchEnabled = YES;
        self.backgroundColor = UIColor.clearColor;
        self.alpha = 0;
        _buttonViews = [NSMutableArray array];
        _haptic = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
        _lBase = [self ring:YES]; _lKnob = [self ring:NO]; _rBase = [self ring:YES]; _rKnob = [self ring:NO];
        _context = CTX_NONE;
    }
    return self;
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

- (CGFloat)sx { return self.bounds.size.width / kRefW; }
- (CGFloat)sy { return self.bounds.size.height / kRefH; }
- (CGRect)rectFor:(const GTMBtn *)b {
    CGFloat d = b->d * [self sy];
    return CGRectMake(b->x * [self sx] - d / 2, b->y * [self sy] - d / 2, d, d);
}
- (CGFloat)topBand { return 68 * [self sy]; }

- (void)rebuild {
    for (UIView *v in _buttonViews) [v removeFromSuperview];
    [_buttonViews removeAllObjects];
    _layout = LayoutFor(_context, &_count);
    for (int i = 0; i < _count; i++) {
        const GTMBtn *b = &_layout[i];
        UILabel *v = [[UILabel alloc] initWithFrame:[self rectFor:b]];
        v.text = [NSString stringWithUTF8String:b->label];
        v.numberOfLines = 2;
        v.textAlignment = NSTextAlignmentCenter;
        v.textColor = UIColor.whiteColor;
        v.font = [UIFont systemFontOfSize:MAX(10, MIN(15, b->d * [self sy] / 5.2)) weight:UIFontWeightSemibold];
        v.adjustsFontSizeToFitWidth = YES;
        v.minimumScaleFactor = 0.6;
        v.backgroundColor = [UIColor colorWithWhite:0 alpha:0.28];
        v.layer.cornerRadius = v.bounds.size.width / 2;
        v.layer.borderWidth = 1.5;
        v.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.55].CGColor;
        v.clipsToBounds = YES;
        v.userInteractionEnabled = NO;
        [self addSubview:v];
        [_buttonViews addObject:v];
    }
    // botoes que sumiram soltam; analogicos continuam
    for (int s = 0; s < 12; s++) if (_slots[s].kind == T_BUTTON) _slots[s].kind = T_NONE, _slots[s].touch = nil;
    [self refreshPressed];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    for (int i = 0; i < _count && i < (int)_buttonViews.count; i++) {
        UIView *v = _buttonViews[i];
        v.frame = [self rectFor:&_layout[i]];
        v.layer.cornerRadius = v.bounds.size.width / 2;
    }
}

- (int)buttonAt:(CGPoint)p {
    int best = -1; CGFloat bestDist = CGFLOAT_MAX;
    for (int i = 0; i < _count; i++) {
        CGRect r = [self rectFor:&_layout[i]];
        CGFloat dx = p.x - CGRectGetMidX(r), dy = p.y - CGRectGetMidY(r);
        CGFloat dist = sqrt(dx * dx + dy * dy);
        if (dist <= r.size.width / 2 * 1.15 && dist < bestDist) { best = i; bestDist = dist; }
    }
    return best;
}

// fora dos botoes, a faixa do topo fica para o Madeira (menu da sessao)
- (BOOL)pointInside:(CGPoint)p withEvent:(UIEvent *)e {
    if (!_live) return NO;
    if ([self buttonAt:p] >= 0) return YES;
    return p.y > [self topBand];
}

- (GTMSlot *)slotFor:(UITouch *)t create:(BOOL)create {
    for (int s = 0; s < 12; s++) if (_slots[s].touch == t && _slots[s].kind != T_NONE) return &_slots[s];
    if (!create) return NULL;
    for (int s = 0; s < 12; s++) if (_slots[s].kind == T_NONE) return &_slots[s];
    return NULL;
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *t in touches) {
        GTMSlot *s = [self slotFor:t create:YES];
        if (!s) continue;
        CGPoint p = [t locationInView:self];
        int b = [self buttonAt:p];
        s->touch = t; s->start = p; s->cur = p;
        if (b >= 0) { s->kind = T_BUTTON; s->button = b; [_haptic impactOccurred]; }
        else s->kind = (p.x < self.bounds.size.width / 2) ? T_LSTICK : T_RSTICK;
    }
    [self refreshPressed];
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *t in touches) {
        GTMSlot *s = [self slotFor:t create:NO];
        if (!s) continue;
        s->cur = [t locationInView:self];
        if (s->kind == T_BUTTON) {                       // deslizar para outro botao troca o botao
            int b = [self buttonAt:s->cur];
            if (b >= 0 && b != s->button) { s->button = b; [_haptic impactOccurred]; }
        }
    }
    [self refreshPressed];
}
- (void)endTouches:(NSSet<UITouch *> *)touches {
    for (UITouch *t in touches) {
        GTMSlot *s = [self slotFor:t create:NO];
        if (s) { s->kind = T_NONE; s->touch = nil; }
    }
    [self refreshPressed];
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self endTouches:touches]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self endTouches:touches]; }

- (void)refreshPressed {
    BOOL pressed[64] = {0};
    for (int s = 0; s < 12; s++) if (_slots[s].kind == T_BUTTON && _slots[s].button < 64) pressed[_slots[s].button] = YES;
    for (int i = 0; i < _count && i < (int)_buttonViews.count; i++)
        _buttonViews[i].backgroundColor = [UIColor colorWithWhite:pressed[i] ? 1 : 0 alpha:pressed[i] ? 0.35 : 0.28];
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

- (void)tick {
    if (!GTMOpenShared()) return;
    GTMGameBlock g = {0};
    CFTimeInterval now = CACurrentMediaTime();
    if (pread(gFd, &g, sizeof g, 0) == sizeof g && g.magic == kGameMagic && g.seq != _lastGameSeq) {
        _lastGameSeq = g.seq; _lastGameChange = now;
    }
    BOOL fresh = (now - _lastGameChange) < 0.4;
    int ctx = fresh ? (int)g.context : CTX_NONE;
    if (!fresh && g.magic == kGameMagic) ctx = CTX_NONE;
    if (ctx != _context) {
        _context = ctx;
        [self rebuild];
        [UIView animateWithDuration:0.15 animations:^{ self.alpha = (ctx == CTX_NONE) ? 0 : 1; }];
    }
    BOOL live = (ctx != CTX_NONE);
    if (live != _live) {
        _live = live;
        if (!live) { for (int s = 0; s < 12; s++) _slots[s].kind = T_NONE, _slots[s].touch = nil; [self refreshPressed]; }
    }
    if (!live) return;

    GTMHostBlock h = {0};
    h.magic = kHostMagic;
    h.seq = ++_hostSeq;
    uint32_t bits = 0;
    for (int s = 0; s < 12; s++) if (_slots[s].kind == T_BUTTON && _slots[s].button < _count) bits |= _layout[_slots[s].button].bits;
    h.buttons = (uint16_t)(bits & 0xFFFF);
    h.lt = (bits & B_LT) ? 255 : 0;
    h.rt = (bits & B_RT) ? 255 : 0;
    int16_t lx, ly, rx, ry;
    [self stick:T_LSTICK x:&lx y:&ly];
    [self stick:T_RSTICK x:&rx y:&ry];
    h.lx = lx; h.ly = ly; h.rx = rx; h.ry = ry;
    pwrite(gFd, &h, sizeof h, 32);
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
    vc.view = pad;
    w.rootViewController = vc;
    w.hidden = NO;                                      // visivel, mas nunca key: o teclado e o foco seguem no Madeira
    self.window = w; self.pad = pad;
    self.link = [CADisplayLink displayLinkWithTarget:self selector:@selector(frame:)];
    self.link.preferredFrameRateRange = CAFrameRateRangeMake(30, 60, 60);
    [self.link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    NSLog(@"[GTMTouch] overlay ligado; arquivo %@", GTMSharedPath());
}
- (void)frame:(CADisplayLink *)l { [self.pad tick]; }
- (void)sceneEvent:(NSNotification *)n { [self attachIfNeeded]; }
@end

__attribute__((constructor)) static void GTMTouchInit(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        GTMController *c = GTMController.shared;
        [NSNotificationCenter.defaultCenter addObserver:c selector:@selector(sceneEvent:) name:UISceneDidActivateNotification object:nil];
        // tenta tambem por alguns segundos, caso a cena ja esteja ativa
        for (int i = 1; i <= 10; i++)
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(i * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [c attachIfNeeded]; });
    });
}
