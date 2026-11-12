"""Assemble a narrated tutorial from raw OBS takes, one scene per narration line, plus a soft .srt.

    python tools/tutorial/montage.py tools/tutorial/skin --takes <takes dir> --audio <audio dir> --out <video.mp4>
    python tools/tutorial/montage.py tools/tutorial/skin ... --placeholder     # Windows voice for missing lines

Inputs, in the tutorial folder:
  script.json  scenes with "id" and "narration" (what the TTS reads).
  edit.json    how each scene is cut: "crops" (named rectangles of the monitor, with optional blur boxes in
               crop coordinates) and, per scene id, "pieces": [{take, from, to, crop, speed, squeeze, audio}].
               "squeeze" drops the still stretches of a UI take (ffmpeg freezedetect), keeping 0.25 s of each.
               Scene keys: "lead" (seconds before the voice starts), "fit" (max speed-up so the pieces end with
               the voice), "fade_in", "fade_out".
Audio: <audio dir>/<scene id>.mp3|.wav. With --placeholder a missing line is spoken by the Windows voice (for
timing only; the real voice is generated from the approved script).

A scene lasts max(pieces, lead + voice + tail); short pieces hold their last frame. Pieces are encoded to
1920x1080 60 fps (NVENC) in <work>, scenes are joined without re-encoding the video.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

W, H, FPS = 1920, 1080, 60
TAIL = 0.6        # silence after the voice before the next scene
KEEP = 0.35       # what squeeze keeps of each still stretch
GAME_VOL = 0.35   # take audio under the voice


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='replace')
    if r.returncode:
        sys.exit('failed: %s\n%s' % (' '.join(cmd), r.stderr[-3000:]))
    return r


def duration(path):
    out = run(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', path]).stdout
    return float(out.strip())


def freezes(take, rect, work):
    """Still intervals of a take inside rect, cached; neighbours closer than 0.1 s are merged (keyframes
    break a long still into pieces)."""
    key = hashlib.md5(('%s %s' % (take, rect)).encode()).hexdigest()[:10]
    cache = os.path.join(work, 'freeze_%s.json' % key)
    if os.path.exists(cache):
        with open(cache) as f:
            return json.load(f)
    vf = 'crop=%d:%d:%d:%d,' % (rect[2], rect[3], rect[0], rect[1]) if rect else ''
    vf += 'freezedetect=n=0.0001:d=0.8'
    err = run(['ffmpeg', '-hide_banner', '-i', take, '-vf', vf, '-an', '-f', 'null', '-']).stderr
    starts = [float(x) for x in re.findall(r'freeze_start: ([0-9.]+)', err)]
    ends = [float(x) for x in re.findall(r'freeze_end: ([0-9.]+)', err)]
    ends += [duration(take)] * (len(starts) - len(ends))
    iv = []
    for s, e in zip(starts, ends):
        if iv and s - iv[-1][1] < 0.1:
            iv[-1][1] = e
        else:
            iv.append([s, e])
    with open(cache, 'w') as f:
        json.dump(iv, f)
    return iv


def keep_ranges(a, b, frz):
    """[a, b] minus the middle of each still stretch."""
    out, t = [], a
    for s, e in frz:
        s, e = max(s, a), min(e, b)
        if e - s <= 2 * KEEP + 0.3:
            continue
        if s + KEEP > t:
            out.append([t, s + KEEP])
        t = e - KEEP
    if b > t:
        out.append([t, b])
    return [r for r in out if r[1] - r[0] > 0.02]


def piece_ranges(p, crops, takes, work):
    take = os.path.join(takes, p['take'] + '.mp4')
    rect = crops.get(p.get('crop', 'full'), {}).get('rect')
    if p.get('squeeze'):
        return take, keep_ranges(p['from'], p['to'], freezes(take, rect, work))
    return take, [[p['from'], p['to']]]


def blur_chain(boxes, src):
    """Filter text that pixelates each box of the (already cropped) frame (10 px mosaic: unreadable)."""
    parts, cur = [], src
    for i, (x, y, w, h) in enumerate(boxes):
        parts.append('[%s]split[m%d][c%d];[c%d]crop=%d:%d:%d:%d,scale=%d:%d,scale=%d:%d:flags=neighbor[b%d];'
                     '[m%d][b%d]overlay=%d:%d[o%d]'
                     % (cur, i, i, i, w, h, x, y, max(1, w // 10), max(1, h // 10), w, h, i, i, i, x, y, i))
        cur = 'o%d' % i
    return ';'.join(parts), cur


def build_piece(p, take, ranges, speed, crops, work, name):
    out = os.path.join(work, name + '.mkv')
    a = ranges[0][0]
    b = ranges[-1][1]
    crop = crops.get(p.get('crop', 'full'), {})
    rect, boxes = crop.get('rect'), crop.get('blur', [])
    fl = []
    for i, (s, e) in enumerate(ranges):
        fl.append('[0:v]trim=%.3f:%.3f,setpts=PTS-STARTPTS[v%d]' % (s - a, e - a, i))
    fl.append(''.join('[v%d]' % i for i in range(len(ranges))) + 'concat=n=%d:v=1:a=0[cat]' % len(ranges))
    cur = 'cat'
    if rect:
        fl.append('[cat]crop=%d:%d:%d:%d[cr]' % (rect[2], rect[3], rect[0], rect[1]))
        cur = 'cr'
    if boxes:
        chain, cur2 = blur_chain(boxes, cur)
        fl.append(chain)
        cur = cur2
    fl.append('[%s]scale=%d:%d:flags=lanczos,setsar=1,setpts=PTS/%.4f,fps=%d,format=yuv420p[vo]'
              % (cur, W, H, speed, FPS))
    length = sum(e - s for s, e in ranges) / speed
    if p.get('audio') and len(ranges) == 1:
        tempo = []
        sp = speed
        while sp > 2.0:
            tempo.append('atempo=2.0')
            sp /= 2.0
        tempo.append('atempo=%.4f' % sp)
        fl.append('[0:a:0]atrim=0:%.3f,asetpts=PTS-STARTPTS,%s,volume=%.2f,aresample=48000[ao]'
                  % (b - a, ','.join(tempo), GAME_VOL))
    else:
        fl.append('anullsrc=r=48000:cl=stereo,atrim=0:%.3f[ao]' % length)
    run(['ffmpeg', '-y', '-v', 'error', '-ss', '%.3f' % a, '-to', '%.3f' % b, '-i', take,
         '-filter_complex', ';'.join(fl), '-map', '[vo]', '-map', '[ao]',
         '-c:v', 'h264_nvenc', '-preset', 'p5', '-rc', 'vbr', '-cq', '19', '-b:v', '0',
         '-c:a', 'pcm_s16le', '-ar', '48000', '-ac', '2', out])
    return out, length


def placeholder_tts(text, out):
    txt = out + '.txt'
    with open(txt, 'w', encoding='utf-8') as f:
        f.write(text)
    ps = ("Add-Type -AssemblyName System.Speech; $s = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
          "$s.SelectVoice('Microsoft David Desktop'); $s.Rate = 1; $s.SetOutputToWaveFile('%s'); "
          "$s.Speak([IO.File]::ReadAllText('%s')); $s.Dispose()" % (out, txt))
    run(['powershell', '-NoProfile', '-Command', ps])


def find_audio(sid, audio_dir):
    for ext in ('.mp3', '.wav', '.m4a', '.ogg'):
        p = os.path.join(audio_dir, sid + ext)
        if os.path.exists(p):
            return p
    return None


def build_scene(sc, ed, voice, crops, takes, work):
    sid = sc['id']
    lead = ed.get('lead', 0.3)
    vlen = duration(voice)
    plan = [piece_ranges(p, crops, takes, work) + (p,) for p in ed['pieces']]
    raw = sum(sum(e - s for s, e in r) / p.get('speed', 1.0) for _, r, p in plan)
    want = lead + vlen + TAIL
    fit = 1.0
    if raw > want and ed.get('fit', 1.0) > 1.0:
        fit = min(ed['fit'], raw / want)
    clips = []
    for i, (take, ranges, p) in enumerate(plan):
        clips.append(build_piece(p, take, ranges, p.get('speed', 1.0) * fit, crops, work, '%s_p%d' % (sid, i)))
    vid = sum(c[1] for c in clips)
    total = max(vid, want)
    out = os.path.join(work, sid + '.mkv')
    cmd = ['ffmpeg', '-y', '-v', 'error']
    for c, _ in clips:
        cmd += ['-i', c]
    cmd += ['-i', voice]
    n = len(clips)
    fl = [''.join('[%d:v][%d:a]' % (i, i) for i in range(n)) + 'concat=n=%d:v=1:a=1[cv][ca]' % n]
    fl.append('[cv]tpad=stop_mode=clone:stop_duration=%.3f,trim=0:%.3f' % (max(0, total - vid) + 0.1, total)
              + (',fade=in:st=0:d=%.2f' % ed['fade_in'] if ed.get('fade_in') else '')
              + (',fade=out:st=%.3f:d=%.2f' % (total - ed['fade_out'], ed['fade_out']) if ed.get('fade_out') else '')
              + '[v]')
    fl.append('[%d:a]aresample=48000,aformat=channel_layouts=stereo,adelay=%d:all=1[vo]' % (n, int(lead * 1000)))
    fl.append('[ca]apad[cp];[cp][vo]amix=inputs=2:duration=longest:normalize=0,atrim=0:%.3f' % total
              + (',afade=out:st=%.3f:d=%.2f' % (total - ed['fade_out'], ed['fade_out']) if ed.get('fade_out') else '')
              + '[a]')
    cmd += ['-filter_complex', ';'.join(fl), '-map', '[v]', '-map', '[a]',
            '-c:v', 'h264_nvenc', '-preset', 'p5', '-rc', 'vbr', '-cq', '19', '-b:v', '0', '-r', str(FPS),
            '-c:a', 'pcm_s16le', '-ar', '48000', out]
    run(cmd)
    return out, duration(out), lead, vlen


def cues(text):
    """Subtitle chunks: sentences, long ones split at commas, at most ~84 characters."""
    out = []
    for s in re.split(r'(?<=[.!?])\s+', text.strip()):
        while len(s) > 84:
            cut = s.rfind(', ', 0, 84)
            if cut < 20:
                cut = s.rfind(' ', 0, 84)
            out.append(s[:cut + 1].strip())
            s = s[cut + 1:].strip()
        if out and len(out[-1]) + len(s) < 40:
            out[-1] += ' ' + s
        else:
            out.append(s)
    return out


def ts(t):
    ms = int(round(t * 1000))
    return '%02d:%02d:%02d,%03d' % (ms // 3600000, ms // 60000 % 60, ms // 1000 % 60, ms % 1000)


def wrap(s, n=42):
    if len(s) <= n:
        return s
    mid = len(s) // 2
    cut = min((i for i in range(len(s)) if s[i] == ' '), key=lambda i: abs(i - mid))
    return s[:cut] + '\n' + s[cut + 1:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder')
    ap.add_argument('--takes', required=True)
    ap.add_argument('--audio', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--work')
    ap.add_argument('--placeholder', action='store_true')
    ap.add_argument('--only', help='comma-separated scene ids (for a quick look at a few scenes)')
    a = ap.parse_args()
    with open(os.path.join(a.folder, 'script.json'), encoding='utf-8') as f:
        script = json.load(f)
    with open(os.path.join(a.folder, 'edit.json'), encoding='utf-8') as f:
        edit = json.load(f)
    work = a.work or os.path.splitext(a.out)[0] + '_work'
    os.makedirs(work, exist_ok=True)
    os.makedirs(a.audio, exist_ok=True)
    only = set(a.only.split(',')) if a.only else None
    scenes, srt, t, n = [], [], 0.0, 0
    for sc in script['scenes']:
        if only and sc['id'] not in only:
            continue
        voice = find_audio(sc['id'], a.audio)
        if not voice:
            if not a.placeholder:
                sys.exit('no audio for %s in %s (use --placeholder for a timing draft)' % (sc['id'], a.audio))
            voice = os.path.join(work, 'tts_' + sc['id'] + '.wav')
            if not os.path.exists(voice):
                placeholder_tts(sc['narration'], voice)
        path, length, lead, vlen = build_scene(sc, edit['scenes'][sc['id']], voice, edit['crops'], a.takes, work)
        print('%-12s %5.1f s (voice %4.1f s)' % (sc['id'], length, vlen), flush=True)
        chunks = cues(sc['narration'])
        total = sum(len(c) for c in chunks)
        c0 = t + lead
        for c in chunks:
            d = vlen * len(c) / total
            n += 1
            srt.append('%d\n%s --> %s\n%s\n' % (n, ts(c0), ts(c0 + d - 0.05), wrap(c)))
            c0 += d
        scenes.append(path)
        t += length
    lst = os.path.join(work, 'concat.txt')
    with open(lst, 'w', encoding='utf-8') as f:
        f.writelines("file '%s'\n" % s.replace('\\', '/') for s in scenes)
    run(['ffmpeg', '-y', '-v', 'error', '-f', 'concat', '-safe', '0', '-i', lst, '-c:v', 'copy',
         '-af', 'loudnorm=I=-16:TP=-1.5:LRA=11', '-ar', '48000',
         '-c:a', 'aac', '-b:a', '192k', '-movflags', '+faststart', a.out])
    with open(os.path.splitext(a.out)[0] + '.srt', 'w', encoding='utf-8') as f:
        f.write('\n'.join(srt))
    print('%s: %.1f s, %d subtitles' % (a.out, t, n))


if __name__ == '__main__':
    main()
