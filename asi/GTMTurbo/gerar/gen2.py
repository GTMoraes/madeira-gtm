import numpy as np, json
exec(open('gen.py').read().split("out={'w'")[0])     # reaproveita funcoes (whistle, bov, engine, norm, b64...)
rng2=np.random.default_rng(11)
def grito(intensity, opt, dur=1.4):
    n=int(SR*dur); t=np.arange(n)/SR
    if opt=='G1':   f0=760+520*intensity; slope=480; dec=0.62; am=(8.5,13.5); dep=0.5
    else:           f0=900+560*intensity; slope=560; dec=0.32; am=(11,16);  dep=0.55
    f=np.maximum(f0-slope*t+40*t*t, 420)
    ph=2*np.pi*np.cumsum(f)/SR
    tone=np.sin(ph)+0.11*np.sin(2*ph+0.4)+0.04*np.sin(3*ph+1.1)
    # tremedeira irregular (o "pente"): duas taxas com fase sorteada e um pouco de ruido
    a1,a2=am; p1,p2=rng2.uniform(0,6.28,2)
    jit=lp(rng2.standard_normal(n),18)*6
    mod=1-dep*(0.5+0.5*np.sin(2*np.pi*a1*t+p1+jit))*(0.6+0.4*np.sin(2*np.pi*a2*t+p2))
    env=np.clip(t/0.025,0,1)*np.exp(-t/dec)*(0.55+0.45*np.exp(-t/0.12))
    whoosh=bp(rng2.standard_normal(n),500,2600)*0.35*np.exp(-t/0.25)
    return (tone*mod+whoosh)*env*intensity
def cut(x, at, fade=0.012, thump=True):
    y=x.copy(); i=int(at*SR); k=int(fade*SR)
    y[i:i+k]*=np.linspace(1,0,k); y[i+k:]=0
    if thump:   # fechada da valvula: "tuc" curto e grave
        m=int(0.03*SR); tt=np.arange(m)/SR
        y[i:i+m]+=0.35*np.max(np.abs(x[:i]))*np.sin(2*np.pi*140*tt)*np.exp(-tt/0.008)
    return y
out={'w':{},'bov':{},'gr':{},'demo':{}}
n=int(SR*4.5); t=np.arange(n)/SR; thr=(t<3.5).astype(float)
for o in 'ABC':
    tcu={'A':0.35,'B':0.5,'C':0.85}[o]
    out['w'][o]=b64(norm(whistle(follow(thr*np.clip(t/1.2,0,1),tcu,0.18),o),-16))
bf=norm(bov(1.0,'A'),-13); bw=norm(bov(0.45,'A'),-13)*0.81
out['bov']={'full':b64(bf),'weak':b64(bw),'shift':b64(cut(bf,0.26)),'shiftw':b64(cut(bw,0.26))}
for g in ('G1','G2'):
    x=norm(grito(1.0,g),-13); xw=norm(grito(0.5,g),-13)*0.6
    out['gr'][g]={'full':b64(x),'weak':b64(xw),'shift':b64(cut(x,0.30,0.02,False))}
# demo 3 marchas
def demo(diesel):
    dur=8.2; n=int(SR*dur); t=np.arange(n)/SR
    shifts=[2.4,4.9] if not diesel else [2.8,5.6]; gap=0.26 if not diesel else 0.30
    lo,hi=(2600,6800) if not diesel else (1300,2700)
    rpm=np.zeros(n); thr=np.ones(n); seg=[0]+shifts+[7.0]
    for g in range(3):
        a,b=seg[g],seg[g+1]; m=(t>=a)&(t<b); start=lo if g==0 else lo+(hi-lo)*0.45
        rpm[m]=start+(hi-start)*((t[m]-a)/(b-a))**0.8
        if g<2: thr[(t>=b)&(t<b+gap)]=0.0
    m=t>=7.0; rpm[m]=hi*np.exp(-(t[m]-7.0)/0.6)+900; thr[m]=0
    rpm=follow(rpm,0.05,0.12)
    target=thr*np.clip((rpm-lo*0.9)/(hi-lo*0.9),0,1)**0.7
    eng=norm(engine(rpm,cyl=6 if diesel else 4,diesel=diesel),-18)*(0.45+0.55*follow(thr,0.08,0.15))
    res={'eng':b64(eng),'t':shifts+[7.0],'gap':gap}
    for o in 'ABC':
        tcu={'A':0.35,'B':0.5,'C':0.85}[o]*(1.4 if diesel else 1)
        boost=follow(target,tcu,0.12)
        res['w'+o]=b64(norm(whistle(boost,o),-19))
        res['p'+o]=[float(boost[int(s*SR)-1]) for s in shifts+[7.0]]
    return res
out['demo']['gas']=demo(False); out['demo']['die']=demo(True)
json.dump(out,open('aud2.json','w'))
print('ok', len(open('aud2.json').read())//1024,'KB')
