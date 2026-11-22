import numpy as np, wave
exec(open('gen3.py').read().split("# R: o grito da gravacao")[0])   # funcoes ate H3
OUT='/home/claude/gta/out/GTMTurbo/'
def wr(name,x):
    pcm=np.clip(np.round(x*32767),-32768,32767).astype('<i2')
    w=wave.open(OUT+name,'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes()); w.close()
    print(name, len(x)/SR, 's', '%.1f dB'%(20*np.log10(np.sqrt(np.mean(x**2)))))
def loop_whistle(lo,tone,hiss,band):
    n=SR; t=np.arange(n)/SR
    ph=2*np.pi*(lo*t) + 40/(3.0)*(-np.cos(2*np.pi*3*t)+1)/ (2*np.pi)*2*np.pi  # vibrato 3 Hz, ciclos inteiros
    s=tone*(np.sin(ph)+0.18*np.sin(2*ph+0.7))
    w=rng3.standard_normal(n); nb=bp(np.tile(w,2),*band)[n:]          # ruido circular (laco sem emenda)
    return s+nb*hiss*1.6
# assobio no pico do estagio inicial (o mod muda a frequencia de tocar: f = lo .. hi)
wr('assobio_gasolina.wav', norm(loop_whistle(4200,1.0,0.35,(3000,9000)),-16))
wr('assobio_diesel.wav',   norm(loop_whistle(3000,0.75,0.7,(2200,7000)),-16))
bf=norm(bov(1.0,'A'),-13); bw=norm(bov(0.45,'A'),-13)*0.81
wr('espirro.wav',bf); wr('espirro_fraco.wav',bw)
m=int(0.04*SR); tt=np.arange(m)/SR
wr('tuc.wav', 0.6*np.sin(2*np.pi*140*tt)*np.exp(-tt/0.008))
wr('grito.wav', norm(H2(1.0),-13)); wr('grito_fraco.wav', norm(H2(0.5),-13)*0.6)
