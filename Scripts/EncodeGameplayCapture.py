"""Encode diagnostic frames at their recorded times, with the game mixer audio."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('output',type=Path);p.add_argument('--ffmpeg',required=True);args=p.parse_args()
meta=json.loads((args.capture/'frames.json').read_text());requested=meta['times'];frames=sorted(args.capture.glob('Frame_*.png'))
# A simultaneous named screenshot can replace one capture request. Retain the
# actual frame indices and timestamps so that a gap holds the preceding frame
# instead of shifting every later frame relative to the recorded game audio.
indices=[int(frame.stem.split('_')[1]) for frame in frames]
assert len(frames)>1 and all(0<=index<len(requested) for index in indices)
times=[requested[index] for index in indices]
print(f'Encoding {len(frames)} of {len(requested)} requested frames at recorded timestamps.',flush=True)
lines=['ffconcat version 1.0']
for index,frame in enumerate(frames):
    lines.append("file '"+frame.resolve().as_posix().replace("'","'\\''")+"'")
    duration=(times[index+1] if index+1<len(times) else meta['duration'])-times[index]
    lines.append(f'duration {max(.001,duration):.6f}')
lines.append("file '"+frames[-1].resolve().as_posix()+"'")
playlist=args.capture/'frames.ffconcat';playlist.write_text('\n'.join(lines))
args.output.parent.mkdir(parents=True,exist_ok=True)
command=[args.ffmpeg,'-y','-f','concat','-safe','0','-i',str(playlist)]
if (args.capture/'Audio.wav').exists():command+=['-i',str(args.capture/'Audio.wav')]
command+=['-c:v','libx264','-preset','fast','-crf','20','-pix_fmt','yuv420p','-r','30','-c:a','aac','-b:a','160k','-t',str(meta['duration']),'-movflags','+faststart',str(args.output)]
subprocess.run(command,check=True)
