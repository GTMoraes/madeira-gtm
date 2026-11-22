import numpy as np, wave, io, base64, json
from scipy.signal import butter, sosfilt, lfilter
SR=22050
rng=np.random.default_rng(7)
def bp(x,lo,hi,o=2): return sosfilt(butter(o,[lo,hi],'bandpass',fs=SR,output='sos'),x)
def lp(x,f,o=2): return sosfilt(butter(o,f,'lowpass',fs=SR,output='sos'),x)
def hp(x,f,o=2): return sosfilt(butter(o,f,'highpass',fs=SR,output='sos'),x)
def reson(x,f,q):            # ressonador variavel no tempo (2 polos)
    y=np.zeros_like(x); y1=y2=0.0
    for i in range(len(x)):
        w=2*np.pi*f[i]/SR; r=np.exp(-w/(2*q[i] if hasattr(q,'__len__') else 2*q))
        a1=-2*r*np.cos(w); a2=r*r
        v=(1-r)*x[i]-a1*y1-a2*y2; y2=y1; y1=v; y[i]=v
    return y
def follow(target,tc_up,tc_dn):
    b=np.zeros_like(target); v=0.0
    au=1-np.exp(-1/(tc_up*SR)); ad=1-np.exp(-1/(tc_dn*SR))
    for i,t in enumerate(target):
        v+= (t-v)*(au if t>v else ad); b[i]=v
    return b
def whistle(boost, opt):
    # boost 0..1 (pressao), a turbina: frequencia sobe com a pressao e satura (fica "soprando")
    lo,hi,tone,hiss,band={'A':(4200,7600,1.0,0.35,(3000,9000)),'B':(3000,5600,0.75,0.7,(2200,7000)),'C':(1700,3600,0.55,1.0,(1200,5000))}[opt]
    sat=1-np.exp(-3.2*boost)                                   # satura perto do topo
    f=lo+(hi-lo)*sat + 40*np.sin(2*np.pi*3.1*np.arange(len(boost))/SR)*sat
    ph=2*np.pi*np.cumsum(f)/SR
    t=tone*(np.sin(ph)+0.18*np.sin(2*ph+0.7))
    n=bp(rng.standard_normal(len(boost)),*band)*hiss*1.6
    return (t+n)*(boost**1.4)
def bov(intensity, opt):
    n=int(SR*1.3); t=np.arange(n)/SR
    w=rng.standard_normal(n)
    if opt=='A':      # SQV: estalo agudo + "tchuuu" tonal descendo
        e1=np.exp(-t/0.05)*(t<0.18); e2=np.clip((t-0.04)/0.03,0,1)*np.exp(-np.clip(t-0.07,0,None)/0.32)
        s1=bp(w,3500,9000)*e1*1.4
        f2=2600*np.exp(-t/0.9)+1100
        s2=reson(w*0.6,f2,np.full(n,14.0))*e2*9+bp(w,1500,5000)*e2*0.5
    elif opt=='B':    # curto e seco
        e1=np.exp(-t/0.035); e2=np.clip((t-0.02)/0.02,0,1)*np.exp(-np.clip(t-0.04,0,None)/0.14)
        s1=bp(w,4000,9500)*e1*1.5
        s2=reson(w*0.6,2200*np.exp(-t/0.5)+1300,np.full(n,10.0))*e2*8
    else:             # longo, mais chiado
        e1=np.exp(-t/0.06)*(t<0.25); e2=np.clip((t-0.05)/0.05,0,1)*np.exp(-np.clip(t-0.1,0,None)/0.55)
        s1=bp(w,3000,9000)*e1*1.2
        s2=reson(w*0.5,2000*np.exp(-t/1.2)+900,np.full(n,8.0))*e2*7+bp(w,1800,7000)*e2*0.9
    # dois estagios: com pouca pressao so o primeiro, fraco
    k=np.clip((intensity-0.35)/0.5,0,1)
    return (s1*(0.45+0.55*intensity)+s2*k)*intensity
def flutter(intensity, opt):
    n=int(SR*1.3); t=np.arange(n)/SR
    rate,band,dec={'A':(24,(500,1800),0.45),'B':(14,(300,1200),0.6),'C':(19,(400,2600),0.5)}[opt]
    rate_t=rate*(1+0.35*np.exp(-t/0.3))                    # comeca mais rapido e desacelera
    ph=np.cumsum(rate_t)/SR
    puls=np.clip(np.sin(2*np.pi*ph),0,None)**3
    env=np.exp(-t/dec)*np.clip(t/0.01,0,1)
    w=rng.standard_normal(n)
    s=bp(w,*band)*puls*env*2.2
    if opt=='C': s+=bp(w,2500,6000)*env*np.exp(-t/0.15)*0.6
    return s*intensity
def engine(rpm, cyl=4, diesel=False):
    f=rpm/60*cyl/2
    ph=2*np.pi*np.cumsum(f)/SR
    x=sum((1/h)*np.sin(h*ph+h*0.3) for h in range(1,14))
    x+= 0.25*np.sin(0.5*ph)
    x=np.tanh(1.5*x)
    x=lp(x,1800 if diesel else 3200)
    x+=bp(rng.standard_normal(len(x)),200,1400 if diesel else 2500)*(0.25 if diesel else 0.12)
    return x
def norm(x,db=-14):
    x=x*10**(db/20)/np.sqrt(np.mean(x**2)+1e-12); pk=np.max(np.abs(x))
    return x*(0.89/pk) if pk>0.89 else x
def b64(x):
    pcm=np.clip(np.round(x*32767),-32768,32767).astype('<i2')
    b=io.BytesIO(); w=wave.open(b,'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes()); w.close()
    return 'data:audio/wav;base64,'+base64.b64encode(b.getvalue()).decode()
out={'w':{},'b':{},'f':{},'demo':{}}
# 1) assobio: puxada de 3,5 s e tirada de pe
n=int(SR*4.5); t=np.arange(n)/SR
thr=(t<3.5).astype(float)
for o in 'ABC':
    tcu={'A':0.35,'B':0.5,'C':0.85}[o]
    boost=follow(thr*np.clip(t/1.2,0,1),tcu,0.18)
    out['w'][o]=b64(norm(whistle(boost,o),-16))
# 2) espirro e 3) pentada: forte e fraco
for o in 'ABC':
    out['b'][o]={'forte':b64(norm(bov(1.0,o),-13)),'fraco':b64(norm(bov(0.45,o),-13)*0.45/0.89*1.6)}
    out['f'][o]={'forte':b64(norm(flutter(1.0,o),-13)),'fraco':b64(norm(flutter(0.45,o),-13)*0.6)}
# 4) demo: 3 marchas com motor de referencia
def demo(diesel):
    dur=8.0; n=int(SR*dur); t=np.arange(n)/SR
    shifts=[2.4,4.9] if not diesel else [2.8,5.6]
    lo,hi=(2600,6800) if not diesel else (1300,2700)
    rpm=np.zeros(n); thr=np.ones(n)
    seg=[0]+shifts+[7.2]
    for g in range(3):
        a,b=seg[g],seg[g+1]; m=(t>=a)&(t<b)
        start=lo if g==0 else lo+ (hi-lo)*0.45
        rpm[m]=start+(hi-start)*((t[m]-a)/(b-a))**0.8
        thr[(t>=b-0.0)&(t<b+0.28)]=0.0               # pe fora na troca
    m=t>=7.2; rpm[m]=hi*np.exp(-(t[m]-7.2)/0.6)+900; thr[m]=0
    rpm=follow(rpm,0.05,0.12)
    target=thr*np.clip((rpm-lo*0.9)/(hi-lo*0.9),0,1)**0.7
    eng=norm(engine(rpm,cyl=6 if diesel else 4,diesel=diesel),-18)*(0.45+0.55*follow(thr,0.08,0.15))
    res={'eng':b64(eng),'t':[s for s in shifts]+[7.2]}
    for o in 'ABC':
        tcu={'A':0.35,'B':0.5,'C':0.85}[o]*(1.4 if diesel else 1)
        boost=follow(target,tcu,0.12)
        res['w'+o]=b64(norm(whistle(boost,o),-19))
        # pressao no momento de cada troca (para a forca do espirro)
        res['p'+o]=[float(boost[int(s*SR)-1]) for s in shifts+[7.2]]
    return res
out['demo']['gas']=demo(False); out['demo']['die']=demo(True)
json.dump(out,open('aud.json','w'))
print('ok', len(open('aud.json').read())//1024,'KB')
