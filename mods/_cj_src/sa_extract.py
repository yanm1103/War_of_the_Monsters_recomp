"""Dump one bank of a GTA San Andreas (PC) SFX pak to WAV files. Standard library only.

usage: python -I extract.py <pak> <bank> <outdir> [--lut PedSpeechAudioEntityLUTs.inc.h] [idx ...]

  <pak>   pak name (FEET, GENRL, PAIN_A, SCRIPT, SPC_EA, SPC_FA, SPC_GA, SPC_NA, SPC_PA)
          resolved under <GTA>/audio/SFX, or a full path to the pak file
          (CONFIG is taken from <pak dir>/../CONFIG).
  <bank>  global BankLkup index (== eSoundBank, e.g. 700 = SND_BANK_SPC_PA_CR),
          or a bank index relative to the pak (e.g. 10 within SPC_PA).
  idx     optional sound indices to dump (default: all).
  --lut   gta-reversed PedSpeechAudioEntityLUTs.inc.h: names the files of SPC_PA
          (player speech) and PAIN_A banks after their speech context.

Format (verified on 1.0 PC data):
  PakFiles.dat : N x 52-byte NUL-padded pak names; BankLkup's pak index points here.
  BankLkup.dat : 710 x 12 bytes {u8 pak, u8 pad[3], u32 offset, u32 size}.
                 offset = bank header position in the pak, size = PCM data bytes after the header.
  bank header  : u16 numSounds, u16 pad, then 400 x 12 bytes
                 {u32 offset (relative to PCM start), i32 loopOffset (-1 = none),
                  u16 sampleRate, i16 headroom}; header is 4 + 400*12 = 4804 bytes.
  PCM          : signed 16-bit little-endian mono; sound i runs to sound i+1 offset
                 (last one to the bank size).
"""
import os, re, struct, sys, wave

DEFAULT_GTA = r"C:\Games\Grand Theft Auto San Andreas"
HDR = 4 + 400 * 12

PLY_VOICES = ["AG", "AG2", "AR", "AR2", "CD", "CD2", "CF", "CF2", "CG", "CG2",
              "CR", "CR2", "PG", "PG2", "PR", "PR2", "WG", "WG2", "WR", "WR2"]


def load_labels(lut_path, bank):
    """Map sound index -> context name for SPC_PA (690..709) or PAIN_A (144..146)."""
    src = open(lut_path, encoding="utf-8").read()
    ctxh = os.path.join(os.path.dirname(lut_path), "PedSpeechContexts.h")
    lines = src.splitlines()

    def table(marker):
        i = next(k for k, l in enumerate(lines) if marker in l)
        rows = []
        for l in lines[i + 1:]:
            if l.startswith("}};"):
                break
            if "{{{" in l:
                cells = re.findall(r"\{(\d+), (\d+)\}|\{\}", l)
                rows.append([(int(a), int(b)) if a else None for a, b in cells])
        return rows

    def names(prefix):
        if not os.path.exists(ctxh):
            return {}
        t = open(ctxh, encoding="utf-8").read()
        return {int(v): k[len(prefix):] for k, v in re.findall(r"(%s\w+)\s*=\s*(\d+)" % prefix, t)}

    if 690 <= bank <= 709:
        rows, col, nm = table("gPlySpeechLookup{{"), bank - 690, names("CTX_PLY_")
    elif 144 <= bank <= 146:
        # pain voice v (column v) -> bank 144 + v (gPainVoiceToBankLookup); column 1 tops out
        # at 130 and bank 145 holds 131 sounds, column 2 at 100 and bank 146 holds 101.
        rows, col, nm = table("gPainSpeechLookup{{"), bank - 144, names("CTX_PAIN_")
    else:
        return {}
    out = {}
    for ci, r in enumerate(rows):
        c = r[col]
        if c:
            for s in range(c[0], c[1] + 1):
                out[s] = nm.get(ci, "ctx%d" % ci)
    return out


def main(argv):
    lut = None
    if "--lut" in argv:
        k = argv.index("--lut")
        lut = argv[k + 1]
        del argv[k:k + 2]
    if len(argv) < 3:
        print(__doc__)
        return 2
    pak_arg, bank_arg, outdir = argv[0], int(argv[1]), argv[2]
    only = [int(x) for x in argv[3:]]

    if os.path.isfile(pak_arg):
        pak_path = pak_arg
    else:
        pak_path = os.path.join(DEFAULT_GTA, "audio", "SFX", pak_arg.upper())
    cfg = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(pak_path))), "CONFIG")
    pak_name = os.path.basename(pak_path).upper()

    pf = open(os.path.join(cfg, "PakFiles.dat"), "rb").read()
    paks = [pf[i:i + 52].split(b"\0")[0].decode().upper() for i in range(0, len(pf), 52)]
    pak_idx = paks.index(pak_name)
    bl = open(os.path.join(cfg, "BankLkup.dat"), "rb").read()
    lk = [struct.unpack_from("<B3xII", bl, i) for i in range(0, len(bl), 12)]
    in_pak = [b for b, e in enumerate(lk) if e[0] == pak_idx]
    if bank_arg in in_pak:
        bank = bank_arg
    elif 0 <= bank_arg < len(in_pak):
        bank = in_pak[bank_arg]
    else:
        print("bank %d is not in %s (global %d..%d, or relative 0..%d)" % (bank_arg, pak_name, in_pak[0], in_pak[-1], len(in_pak) - 1))
        return 1
    _, off, size = lk[bank]

    labels = load_labels(lut, bank) if lut else {}
    voice = PLY_VOICES[bank - 690] if 690 <= bank <= 709 else None

    os.makedirs(outdir, exist_ok=True)
    with open(pak_path, "rb") as f:
        f.seek(off)
        hdr = f.read(HDR)
        n = struct.unpack_from("<H", hdr, 0)[0]
        ents = [struct.unpack_from("<IiHh", hdr, 4 + 12 * k) for k in range(n)]
        f.seek(off + HDR)
        data = f.read(size)

    index = ["idx,file,offset,bytes,samples,rate,seconds,loop,headroom,context"]
    rates = set()
    for k, (soff, loop, rate, head) in enumerate(ents):
        if only and k not in only:
            continue
        end = ents[k + 1][0] if k + 1 < n else size
        pcm = data[soff:end]
        if len(pcm) & 1:
            pcm = pcm[:-1]
        ctx = labels.get(k, "")
        tag = "%d" % bank + ("_" + voice if voice else "") + "_%03d" % k + ("_" + ctx if ctx else "")
        fn = os.path.join(outdir, tag + ".wav")
        w = wave.open(fn, "wb")
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)
        w.close()
        rates.add(rate)
        index.append("%d,%s,%d,%d,%d,%d,%.3f,%d,%d,%s" % (k, os.path.basename(fn), soff, len(pcm), len(pcm) // 2,
                                                          rate, len(pcm) / 2 / rate, loop, head, ctx))
    with open(os.path.join(outdir, "index_%d.csv" % bank), "w", encoding="utf-8") as f:
        f.write("\n".join(index) + "\n")
    print("%s bank %d (rel %d): %d sounds in bank, wrote %d WAV, rates %s -> %s"
          % (pak_name, bank, in_pak.index(bank), n, len(index) - 1, sorted(rates), os.path.abspath(outdir)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
