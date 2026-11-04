"""Encode the two short 0.8 reviews, using actual capture timestamps."""
import argparse,json,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--ffmpeg',required=True);args=p.parse_args()
root=Path(__file__).resolve().parents[1];out=root/'Artifacts/ValidationVideos';out.mkdir(exist_ok=True)
captures=json.loads((root/'Artifacts/Validation/phase8-captures.json').read_text())['captures']
parts=[]
for i,capture in enumerate(captures):
    target=out/('npc-pressure-08.mp4' if capture['kind']=='combat' else f'npc-crouch-08-{i}.mp4')
    subprocess.run([sys.executable,'-B',str(root/'Scripts/EncodeGameplayCapture.py'),capture['path'],str(target),'--ffmpeg',args.ffmpeg],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    if capture['kind']=='stance':parts.append(target)
playlist=out/'npc-crouch-08.ffconcat'
playlist.write_text('ffconcat version 1.0\n'+'\n'.join("file '"+part.as_posix()+"'" for part in parts))
subprocess.run([args.ffmpeg,'-y','-f','concat','-safe','0','-i',str(playlist),'-c','copy','-movflags','+faststart',str(out/'npc-crouch-08.mp4')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
print('Encoded npc-pressure-08.mp4 and npc-crouch-08.mp4')
