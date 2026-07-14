import wave, numpy as np, os
def load(p):
    w=wave.open(p); sr=w.getframerate(); n=w.getnframes(); ch=w.getnchannels()
    raw=np.frombuffer(w.readframes(n),dtype=np.int16).astype(np.float32)/32768.
    if ch>1: raw=raw[::ch]
    return raw, sr
def onset(d): 
    e=np.abs(d); thr=e.max()*0.1; i=np.argmax(e>thr); return max(0,i)
def feats(d,sr):
    d=d[onset(d):]
    if len(d)<int(0.1*sr): return None
    def cent(a,b):
        s=d[int(a*sr):int(b*sr)]
        if len(s)<256: return 0,0
        S=np.abs(np.fft.rfft(s*np.hanning(len(s)))); fr=np.fft.rfftfreq(len(s),1/sr)
        c=(S*fr).sum()/(S.sum()+1e-12)
        P=S**2+1e-12; flat=np.exp(np.mean(np.log(P)))/np.mean(P)
        return c, flat
    ce,fe=cent(0.0,0.10); cm,fm=cent(0.3,0.7)
    env=np.convolve(np.abs(d),np.ones(480)/480,mode='same'); pk=env.max()
    idx=np.where(env>pk*0.03)[0]; t60=idx[-1]/sr if len(idx) else 0
    return ce,cm,fe,fm,t60
S='samples'
pairs=[('Cymbal','cymbal-Crash16Inch.wav','/tmp/rc/13_Cymbal.wav'),
       ('Gong','Chinese-Gong.wav','/tmp/rc/14_Gong.wav'),
       ('Ride','cymbal-Ride18Inch.wav','/tmp/rc/32_Ride.wav'),
       ('RideBell','cymbal-RideBell20InchSabian.wav','/tmp/rc/33_RidBel.wav'),
       ('HHatOpen','OpenHatBig.wav','/tmp/rc/27_HHat-O.wav'),
       ('Timpani','Orchestral-Timpani-C.wav','/tmp/rc/05_Timpani.wav')]
print(f"{'preset':9s} {'src':4s} | cent_early cent_late | flat_early flat_late |  T60   (flat~1=noise, ~0=tonal)")
for name,ref,ren in pairs:
    for tag,f in [('REF',os.path.join(S,ref)),('MINE',ren)]:
        if not os.path.exists(f): print(f"{name:9s} {tag}: MISSING {f}"); continue
        d,sr=load(f); r=feats(d,sr)
        if r: print(f"{name:9s} {tag:4s} | {r[0]:8.0f} {r[1]:8.0f}  | {r[2]:9.3f} {r[3]:8.3f} | {r[4]:5.2f}s")
    print()
