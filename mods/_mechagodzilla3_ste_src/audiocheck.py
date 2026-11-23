"""Confere os bancos .BNK do mod: decodifica cada som trocado do banco que o jogo carrega e compara com o WAV do STE (fonte)."""
import glob, os, re, subprocess, sys, wave
import numpy as np

REPO = 'C:/Users/TwistZero/WoTM'
MOD = REPO + '/mods/Mechagodzilla 3/monsters/mechagodzilla3_ste/files'
SRC = REPO + '/mods/_mechagodzilla3_ste_src/sounds'
TMP = sys.argv[1]
os.makedirs(TMP, exist_ok=True)


def read(p):
    w = wave.open(p, 'rb')
    n, ch, sw, rate = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
    raw = w.readframes(n)
    a = np.frombuffer(raw, dtype='<i2' if sw == 2 else np.uint8).astype(float)
    if sw == 1:
        a = (a - 128) * 256
    if ch > 1:
        a = a.reshape(-1, ch).mean(1)
    return a / 32768.0, rate


def env(a, rate, n=400):
    # envelope RMS em n janelas (compara o formato, indiferente a taxa)
    k = max(1, len(a) // n)
    m = len(a) // k * k
    return np.sqrt((a[:m].reshape(-1, k) ** 2).mean(1))


print('banco  som  fonte do STE                       dur fonte  dur banco  pico   rms   corr.envelope')
for bank, prefix in (('LIZARD_S', 'lizard_s'), ('GENAMB_S', 'genamb_s')):
    for f in sorted(glob.glob('%s/%s_*__*.wav' % (SRC, prefix))):
        num = int(re.findall(r'_(\d{3})__', os.path.basename(f))[0])
        out = '%s/%s_%03d.wav' % (TMP, prefix, num)
        subprocess.run([sys.executable, REPO + '/tools/snd989.py', 'wav', '%s/%s.BNK' % (MOD, bank), str(num), out],
                       capture_output=True, check=True)
        a, ra = read(f)
        b, rb = read(out)
        ea, eb = env(a, ra), env(b, rb)
        k = min(len(ea), len(eb))
        corr = np.corrcoef(ea[:k], eb[:k])[0, 1] if k > 3 else float('nan')
        name = os.path.basename(f).split('__', 1)[1][:-4]
        print('%-8s %3d  %-32s %7.2fs  %7.2fs  %5.2f  %5.3f  %6.3f' % (bank, num, name, len(a) / ra, len(b) / rb, np.abs(b).max(), np.sqrt((b ** 2).mean()), corr))
