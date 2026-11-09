"""Convert a source video into the engine's portable MPEG-1 stream.

Usage: python Tools/ImportVideo.py input.mp4 Project/Assets/Video/Intro.mpg
Requires ffmpeg on PATH, next to this script, or the imageio-ffmpeg Python package.
The original file is never modified. Optional audio is exported as a separate WAV.
"""
import argparse, shutil, subprocess
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('source',type=Path)
parser.add_argument('output',type=Path)
parser.add_argument('--audio',action='store_true')
args=parser.parse_args()
encoder=shutil.which('ffmpeg')
if not encoder:
    try:
        import imageio_ffmpeg
        encoder=imageio_ffmpeg.get_ffmpeg_exe()
    except ImportError:
        raise SystemExit('Install ffmpeg or imageio-ffmpeg to import video.')
args.output.parent.mkdir(parents=True,exist_ok=True)
subprocess.run([encoder,'-n','-i',str(args.source),'-an','-c:v','mpeg1video','-q:v','3','-r','30','-pix_fmt','yuv420p','-f','mpeg',str(args.output)],check=True)
if args.audio:
    subprocess.run([encoder,'-n','-i',str(args.source),'-vn','-c:a','pcm_s16le',str(args.output.with_suffix('.wav'))],check=True)
