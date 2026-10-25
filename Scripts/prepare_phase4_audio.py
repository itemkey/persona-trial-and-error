"""Prepare CC0 shotgun recordings plus original procedural hub/impact sounds.
Downloaded originals live in .work/AudioCC0; generated source WAVs are versioned.
Requires numpy, soundfile and imageio-ffmpeg (local .work/python_audio is supported).
"""
from pathlib import Path
import sys,subprocess,json,hashlib
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'.work/python_audio'))
import numpy as np, soundfile as sf, imageio_ffmpeg
OUT=ROOT/'SourceArt/Audio';OUT.mkdir(parents=True,exist_ok=True)
FF=imageio_ffmpeg.get_ffmpeg_exe();SR=48000;records={}
def convert(source,name,maximum=None,speed=1):
    tmp=OUT/(name+'_raw.wav')
    subprocess.run([FF,'-y','-i',str(source),'-ar',str(SR),'-ac','1',str(tmp)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    x,_=sf.read(tmp);tmp.unlink();nz=np.where(np.abs(x)>.015)[0]
    if len(nz):x=x[max(0,nz[0]-int(.008*SR)):]
    if maximum:x=x[:int(maximum*SR)]
    if speed!=1:x=np.interp(np.arange(0,len(x),speed),np.arange(len(x)),x)
    save(name,x,.85 if name=='ShotgunFire' else .45)
    records[name]={'source':str(source.relative_to(ROOT/'.work/AudioCC0')),'sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'processing':'mono 48kHz; silence trim; gain; length/speed adjustment'}
def save(name,x,peak=.5):
    x=np.asarray(x);x=x/(np.max(np.abs(x))+1e-10)*peak
    n=min(240,len(x)//10);x[:n]*=np.linspace(0,1,n);x[-n:]*=np.linspace(1,0,n)
    sf.write(OUT/(name+'.wav'),x,SR,subtype='PCM_16')
src=ROOT/'.work/AudioCC0'
convert(src/'firearms/Prepared SFX Library/Mossberg/N_26P.wav','ShotgunFire',3.2)
convert(src/'reload/ShotgunSounds/Rack.mp3','Pump',1.0,1.45)
convert(src/'reload/ShotgunSounds/Subsequent Shells.mp3','ShellLoad',.65)
convert(src/'reload/ShotgunSounds/Shell in Chamber.mp3','ShellDrop',.3)
convert(src/'reload/ShotgunSounds/Rack.mp3','Click',.075)
rng=np.random.default_rng(4024)
for name,duration in [('HitBody',.19),('HitWall',.23),('Water',4),('Motor',4)]:
 t=np.arange(int(duration*SR))/SR;n=rng.normal(size=len(t))
 if name=='HitBody':x=(np.sin(2*np.pi*85*t)*.8+np.convolve(n,np.ones(20)/20,'same'))*np.exp(-t*28)
 elif name=='HitWall':x=(n*.6+np.sin(2*np.pi*1200*t)*.1)*np.exp(-t*40)
 elif name=='Water':x=np.convolve(n,np.ones(6)/6,'same')*(.8+.08*np.sin(2*np.pi*3*t))
 else:x=.7*np.sin(2*np.pi*60*t)+.2*np.sin(2*np.pi*120*t)+np.convolve(n,np.ones(70)/70,'same')*.2
 save(name,x,.3 if name.startswith('Hit') else .14);records[name]={'source':'Original procedural synthesis, Scripts/prepare_phase4_audio.py','license':'Project original'}
(OUT/'provenance.json').write_text(json.dumps({'licenses':{'firearms':'CC0 — Ben Jaszczak, Brian Nelson, Kevin Heras, Matthew Nanney — https://opengameart.org/content/the-free-firearm-sound-library','reload':'CC0 — zer0_sol — https://opengameart.org/content/shotgun-reload-sound-effects'},'assets':records},indent=2,ensure_ascii=False),encoding='utf-8')
print('PERSONA_AUDIO_PREPARED',len(records))
