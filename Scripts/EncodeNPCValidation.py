"""Build normal-speed and half-speed review videos from timestamped game captures."""
import argparse,json,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('group',choices=['poses','motion','deaths']);p.add_argument('--ffmpeg',required=True);args=p.parse_args()
root=Path(__file__).resolve().parents[1];out=root/'Artifacts/ValidationVideos';out.mkdir(exist_ok=True)
def run(*command):subprocess.run([args.ffmpeg,'-y',*map(str,command)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
def encode(capture,path):
    subprocess.run([sys.executable,'-B',str(root/'Scripts/EncodeGameplayCapture.py'),capture,str(path),'--ffmpeg',args.ffmpeg],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
name={'poses':'npc-crouch-after','motion':'npc-motion-after','deaths':'npc-death-views-after'}[args.group]
target=out/(name+'.mp4')
if args.group=='motion':
    data=json.loads((root/'Artifacts/Validation/phase7-motion-final.json').read_text());encode(data['capture'],target)
else:
    report='phase7-npc-poses' if args.group=='poses' else 'phase7-death-views'
    data=json.loads((root/'Artifacts/Validation'/(report+'.json')).read_text());parts=[]
    for i,c in enumerate(data['captures']):
        part=out/(name+f'-{i}.mp4');encode(c['path'],part);parts.append(part)
    playlist=out/(name+'.ffconcat');playlist.write_text('ffconcat version 1.0\n'+'\n'.join("file '"+p.as_posix()+"'" for p in parts))
    run('-f','concat','-safe','0','-i',playlist,'-c','copy','-movflags','+faststart',target)
run('-i',target,'-vf','setpts=2*PTS','-an','-c:v','libx264','-preset','fast','-crf','20','-movflags','+faststart',out/(name+'-slow.mp4'))
if args.group=='motion':
    comparison=out/'npc-motion-comparison.mp4'
    filters="[0:v]scale=960:540,drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='BEFORE':fontcolor=white:fontsize=24:x=12:y=12[a];[1:v]scale=960:540,drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='AFTER':fontcolor=white:fontsize=24:x=12:y=12[b];[a][b]hstack=shortest=1[v]"
    run('-i',out/'npc-motion-before.mp4','-i',target,'-filter_complex',filters,'-map','[v]','-an','-c:v','libx264','-preset','fast','-crf','20','-movflags','+faststart',comparison)
print(target,flush=True)
