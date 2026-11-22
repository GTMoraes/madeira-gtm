import numpy as np, json
from scipy.signal import stft, istft
exec(open('gen2.py').read().split("out={'w':{},'bov'")[0])   # funcoes de gen.py + grito/cut
rng3=np.random.default_rng(5)
def amjit(n,rates,dep,seed):
    r=np.random.default_rng(seed); t=np.arange(n)/SR
    jit=lp(r.standard_normal(n),18)*6
    p=r.uniform(0,6.28,2)
    return 1-dep*(0.5+0.5*np.sin(2*np.pi*rates[0]*t+p[0]+jit))*(0.6+0.4*np.sin(2*np.pi*rates[1]*t+p[1]))
def base(intensity,dur=1.4):
    n=int(SR*dur); t=np.arange(n)/SR
    f=np.maximum(760+520*intensity-480*t+40*t*t,420)
    env=np.clip(t/0.025,0,1)*np.exp(-t/0.62)*(0.55+0.45*np.exp(-t/0.12))
    return n,t,f,env
def H1(i):    # dois tons, como o Piercer D: segundo 1,8% acima, tremida propria
    n,t,f,env=base(i)
    s=0
    for k,(det,a,seed) in enumerate([(1.0,1.0,1),(1.018,0.8,2)]):
        ph=2*np.pi*np.cumsum(f*det)/SR
        s=s+a*(np.sin(ph)+0.11*np.sin(2*ph+0.4))*amjit(n,(8.5+2*k,13.5-k),0.5,seed)
    s+=bp(rng3.standard_normal(n),500,2600)*0.3*np.exp(-t/0.25)
    return s*env*i
def H2(i):    # ar: ruido passando por um ressonador estreito que segue o tom (som de tubo), com um pouco de tom
    n,t,f,env=base(i)
    w=rng3.standard_normal(n)
    r=reson(w,f,np.full(n,28.0))
    r=r/np.max(np.abs(r))
    ph=2*np.pi*np.cumsum(f)/SR
    s=(1.0*r+0.35*np.sin(ph))*amjit(n,(8.5,13.5),0.5,3)
    s+=bp(rng3.standard_normal(n),500,2600)*0.25*np.exp(-t/0.25)
    return s*env*i
def H3(i):    # sujo: tom com tremido no tom (vibrato aleatorio), saturacao leve, chiado e eco curto de tubo
    n,t,f,env=base(i)
    vib=1+0.008*lp(rng3.standard_normal(n),35)*8
    ph=2*np.pi*np.cumsum(f*vib)/SR
    s=np.tanh(1.8*(np.sin(ph)+0.15*np.sin(2*ph+0.4)))*amjit(n,(8.5,13.5),0.5,4)
    s+=bp(rng3.standard_normal(n),600,3000)*0.35*np.exp(-t/0.3)
    d1,d2=int(0.0045*SR),int(0.011*SR)
    y=s.copy(); y[d1:]+=0.35*s[:-d1]; y[d2:]+=0.2*s[:-d2]
    return y*env*i
# R: o grito da gravacao (6,62 a 8,0 s), isolado por mascara que segue o tom e o 2o harmonico
import wave
w=wave.open('ref.wav'); x=np.frombuffer(w.readframes(w.getnframes()),'<i2')/32768
seg=x[int(6.60*SR):int(8.05*SR)]
f_,t_,Z=stft(seg,SR,nperseg=1024,noverlap=1024-128)
A=np.abs(Z); m=(f_>500)&(f_<1600)
tr=np.array([f_[m][np.argmax(A[m,j])] for j in range(A.shape[1])])
tr=np.convolve(tr,np.ones(5)/5,'same')
M=np.zeros_like(A)
for j in range(A.shape[1]):
    for h,wd in ((1,90),(2,140),(3,180)):
        M[:,j]+=np.exp(-0.5*((f_-h*tr[j])/wd)**2)*(1 if h==1 else 0.8)
M=np.clip(np.maximum(M,0.06),0,1)                  # um pouco do ar em volta
_,y=istft(Z*M,SR,nperseg=1024,noverlap=1024-128)
y=y[:len(seg)]
fade=np.ones(len(y)); k=int(0.02*SR); fade[:k]=np.linspace(0,1,k); fade[-int(0.25*SR):]*=np.linspace(1,0,int(0.25*SR))
R=hp(y*fade,350)
# a gravacao comeca a 1,28 kHz (pressao cheia); pouca pressao = tocar mais devagar e mais baixo
def Rw(): 
    idx=np.arange(0,len(R)-1,0.75); return np.interp(idx,np.arange(len(R)),R)
out={}
for name,fn in (('H1',H1),('H2',H2),('H3',H3)):
    a=norm(fn(1.0),-13); b=norm(fn(0.5),-13)*0.6
    out[name]={'full':b64(a),'weak':b64(b),'shift':b64(cut(a,0.30,0.02,False))}
a=norm(R,-13); b=norm(Rw(),-13)*0.6
out['R']={'full':b64(a),'weak':b64(b),'shift':b64(cut(a,0.30,0.02,False))}
json.dump(out,open('aud3.json','w'))
# conferencia rapida: estabilidade do tom (desvio do tom em relacao a uma reta) e tonalidade
for name,s in (('G1',grito(1.0,'G1')),('H1',H1(1.0)),('H2',H2(1.0)),('H3',H3(1.0)),('R',R),('gravacao',seg)):
    F,T,Zz=stft(s,SR,nperseg=2048,noverlap=2048-221); Aa=np.abs(Zz); mm=(F>500)&(F<1600)
    js=range(5,min(90,Aa.shape[1]))
    fr=np.array([F[mm][np.argmax(Aa[mm,j])] for j in js]); pk=np.array([Aa[mm,j].max()/np.median(Aa[mm,j]) for j in js])
    print('%-9s tonalidade %4.1f dB  largura do pico %s'%(name,20*np.log10(np.median(pk)),
      '%.0f Hz'%np.median([ (np.sum(Aa[mm,j]>Aa[mm,j].max()*0.5))*F[1] for j in js])))
