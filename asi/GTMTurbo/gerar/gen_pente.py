# Piado do pente na turbina (diesel com turbo GTM), GTMTouchSA 3.83
# Laco sem emenda de 2 s a 22050 Hz, tom base 1000 Hz: o plugin toca mais rapido ou mais devagar conforme a rotacao
# da turbina (1,3 kHz x rotacao) e muda o volume. Timbre do grito H2 escolhido na 3.69 (ar num tubo estreito: ruido num
# ressonador de Q 28 + 35% de tom puro + 2o harmonico), com a tremida irregular de 8,5 e 13,5 Hz da gravacao da F-250,
# agora periodica no laco. RMS -11,3 dBFS, o mesmo pico de volume do grito.wav antigo
import numpy as np, wave, sys
SR=22050; N=2*SR; F0=1000.0
rng=np.random.default_rng(28)
t=np.arange(N)/SR
def reson_circ(x,f,q):
    xx=np.tile(x,3); y=np.zeros_like(xx); y1=y2=0.0
    w=2*np.pi*f/SR; r=np.exp(-w/(2*q)); a1=-2*r*np.cos(w); a2=r*r
    for i in range(len(xx)):
        v=(1-r)*xx[i]-a1*y1-a2*y2; y2=y1; y1=v; y[i]=v
    return y[N:2*N]
w=rng.standard_normal(N)
air=reson_circ(w,F0,28.0); air/=np.sqrt(np.mean(air**2))
air2=reson_circ(rng.standard_normal(N),2*F0,20.0); air2/=np.sqrt(np.mean(air2**2))
ph=2*np.pi*F0*t
tone=np.sin(ph)+0.25*np.sin(2*ph+0.4)
s=air*0.70+air2*0.18+tone*0.35/np.sqrt(0.5)
# tremida: soma de senos com periodo que fecha em 2 s (multiplos de 0,5 Hz), forte em 8,5 e 13,5 Hz
def trem():
    m=np.zeros(N)
    for fr,a in ((8.5,1.0),(13.5,0.7),(8.0,0.35),(9.0,0.35),(13.0,0.25),(14.0,0.25),(4.5,0.2),(17.0,0.15)):
        m+=a*np.sin(2*np.pi*fr*t+rng.uniform(0,6.283))
    m/=np.max(np.abs(m))
    return 1-0.5*(0.5+0.5*m)
s=s*trem()
s=s*10**(-11.3/20)/np.sqrt(np.mean(s**2))
pk=np.max(np.abs(s))
if pk>0.89: s*=0.89/pk
pcm=np.clip(np.round(s*32767),-32768,32767).astype('<i2')
out=sys.argv[1] if len(sys.argv)>1 else 'pente.wav'
wv=wave.open(out,'wb'); wv.setnchannels(1); wv.setsampwidth(2); wv.setframerate(SR); wv.writeframes(pcm.tobytes()); wv.close()
print(out,'%.1f dBFS RMS, pico %.1f dBFS, emenda %d -> %d'%(20*np.log10(np.sqrt(np.mean(s**2))),20*np.log10(pk if pk<0.89 else 0.89),pcm[-1],pcm[0]))
