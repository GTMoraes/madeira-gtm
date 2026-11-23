# Suspiro do wastegate / valvula de recirculacao do turbo de fabrica (GTMTouchSA 3.81)
# 22050 Hz mono, 16 bits. Cada candidato sai normalizado com o pico de volume (RMS em janela de 100 ms) em -16 dBFS,
# o mesmo RMS do laco do assobio: no jogo o volume fica relativo ao assobio cheio (VolumeSuspiro dB acima dele)
import numpy as np, wave, sys
from scipy.signal import butter, sosfilt
SR=22050
OUT=sys.argv[1] if len(sys.argv)>1 else '.'
def bp(x,lo,hi,o=2): return sosfilt(butter(o,[lo,hi],'bandpass',fs=SR,output='sos'),x)
def lp(x,f,o=2): return sosfilt(butter(o,f,'lowpass',fs=SR,output='sos'),x)
def hp(x,f,o=2): return sosfilt(butter(o,f,'highpass',fs=SR,output='sos'),x)
def reson(x,f,q):
    y=np.zeros_like(x); y1=y2=0.0
    for i in range(len(x)):
        w=2*np.pi*f[i]/SR; r=np.exp(-w/(2*q))
        a1=-2*r*np.cos(w); a2=r*r
        v=(1-r)*x[i]-a1*y1-a2*y2; y2=y1; y1=v; y[i]=v
    return y
def env(t,ta,td,t0=0.0):
    u=t-t0
    a=np.where(u<0,0.0,np.where(u<ta,0.5-0.5*np.cos(np.pi*np.clip(u/ta,0,1)),1.0))
    return a*np.exp(-np.clip(u-ta,0,None)/td)
def tvlp(x,fc):                       # passa-baixa de 1 polo com corte variando no tempo (o ar "fecha" no fim)
    y=np.zeros_like(x); v=0.0
    for i in range(len(x)):
        a=1-np.exp(-2*np.pi*fc[i]/SR); v+=a*(x[i]-v); y[i]=v
    return y
def finish(x,dur):
    n=int(SR*dur); x=x[:n]
    f=int(0.03*SR); x[-f:]*=np.linspace(1,0,f)
    x=hp(x,180)
    w=int(0.1*SR); r=np.sqrt(np.convolve(x*x,np.ones(w)/w,'same')).max()
    x=x*10**(-16/20)/r
    pk=np.abs(x).max()
    if pk>0.89: x*=0.89/pk
    return x
def mk(seed,dur): r=np.random.default_rng(seed); n=int(SR*dur); return r.standard_normal(n),np.arange(n)/SR
C={}
# A: "psss" curto e macio: ar fino saindo, some rapido
w,t=mk(11,0.6)
s=bp(w,1800,7000)*env(t,0.012,0.11)
C['A']=finish(tvlp(s,7500*np.exp(-t/0.35)+1800)*1.5,0.6)
# B: "fsss" medio: sopro mais longo e mais agudo, fechando devagar
w,t=mk(12,0.9)
s=bp(w,2500,9000)*env(t,0.025,0.24)+bp(w,1200,2600)*env(t,0.02,0.12)*0.35
C['B']=finish(tvlp(s,9000*np.exp(-t/0.5)+2200),0.9)
# C: "uff" grave: um respiro, mais corpo e menos chiado
w,t=mk(13,0.9)
s=bp(w,450,2400)*env(t,0.03,0.26)+bp(w,2400,5200)*env(t,0.02,0.12)*0.3
C['C']=finish(s,0.9)
# D: "psh-psh": a valvula bate duas vezes de leve e solta o resto
w,t=mk(14,0.8)
s=(bp(w,1500,6500)*(env(t,0.008,0.06)+0.55*env(t,0.008,0.06,0.11))+bp(w,1800,6000)*env(t,0.04,0.2,0.05)*0.3)
C['D']=finish(s,0.8)
# E: turbina girando solta: sopro com o resto do assobio descendo (6,4 para 3,8 kHz)
w,t=mk(15,1.0)
f=3800+2600*np.exp(-t/0.28)
ph=2*np.pi*np.cumsum(f)/SR
tone=(np.sin(ph)+0.15*np.sin(2*ph+0.7))*env(t,0.01,0.3)*0.22
s=bp(w,3000,8500)*env(t,0.02,0.22)+tone
C['E']=finish(s,1.0)
# F: "whoo" de recirculacao: ar num tubo, ressonancia larga descendo de 2,2 para 1 kHz
w,t=mk(16,0.9)
fr=1000+1200*np.exp(-t/0.3)
s=reson(w,fr,5.0)*6*env(t,0.03,0.3)+bp(w,2000,6000)*env(t,0.02,0.15)*0.25
C['F']=finish(s,0.9)
def wr(name,x):
    pcm=np.clip(np.round(x*32767),-32768,32767).astype('<i2')
    wv=wave.open(OUT+'/'+name,'wb'); wv.setnchannels(1); wv.setsampwidth(2); wv.setframerate(SR); wv.writeframes(pcm.tobytes()); wv.close()
    w=int(0.1*SR); r=np.sqrt(np.convolve(x*x,np.ones(w)/w,'same')).max()
    print(name,'%.2f s'%(len(x)/SR),'pico RMS %.1f dBFS'%(20*np.log10(r)),'pico %.1f dBFS'%(20*np.log10(np.abs(x).max())))
for k,x in C.items(): wr('suspiro_%s.wav'%k,x)
