import wave, numpy as np, os, sys, glob
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
R=sys.argv[1] if len(sys.argv)>1 else '/tmp/rc'   # dir of render_presets output

# The render is located by preset NAME, never by preset index.  This file used
# to hard-code indices and the pass-41 GtrStr removal shifted everything above
# 25 down by one, so it was pointing Ride at 32 (now RidBel), RidBel at 33 (now
# Bongo) and HHat-O at 27 (now Conga).  It survived four passes like that
# because samples/ is gitignored: with no reference WAVs the tool could not run
# at all, so nothing ever reported the breakage.  Keyed by name it survives the
# next renumbering — the same fix pass 41 applied to T25/T40a/stability_sweep.
pairs=[('Cymbal',  'cymbal-Crash16Inch.wav',           'Cymbal'),
       ('Gong',    'Chinese-Gong.wav',                 'Gong'),
       ('Ride',    'cymbal-Ride18Inch.wav',            'Ride'),
       ('RideBell','cymbal-RideBell20InchSabian.wav',  'RidBel'),
       ('HHatOpen','OpenHatBig.wav',                   'HHat-O'),
       ('Timpani', 'Orchestral-Timpani-C.wav',         'Timpani')]

def render_for(key):
    hits=sorted(glob.glob(os.path.join(R,'*_%s.wav'%key)))
    if len(hits)==1: return hits[0]
    if not hits:     return None
    print(f"  AMBIGUOUS: {len(hits)} renders match *_{key}.wav in {R}")
    return None

print(f"references: {S}/   renders: {R}/   (pass a directory as argv[1] to change)")
print(f"{'preset':9s} {'src':4s} | cent_early cent_late | flat_early flat_late |  T60   (flat~1=noise, ~0=tonal)")
for name,ref,key in pairs:
    ren=render_for(key)
    for tag,f in [('REF',os.path.join(S,ref)),('MINE',ren)]:
        if f is None:
            print(f"{name:9s} {tag}: MISSING — no *_{key}.wav in {R} (run render_presets first)"); continue
        if not os.path.exists(f):
            hint=" — samples/*.wav is gitignored; see samples/README.md" if tag=='REF' else ""
            print(f"{name:9s} {tag}: MISSING {f}{hint}"); continue
        d,sr=load(f); r=feats(d,sr)
        if r: print(f"{name:9s} {tag:4s} | {r[0]:8.0f} {r[1]:8.0f}  | {r[2]:9.3f} {r[3]:8.3f} | {r[4]:5.2f}s")
    print()
