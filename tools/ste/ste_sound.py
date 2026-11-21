"""Sons de Godzilla: Save the Earth (PS2): os bancos `.uber` + `.samp` do GODZILLA.VOL viram WAVs com o nome do som.

  python tools/ste/ste_sound.py <iso> <banco> <pasta> [--list] [--match REGEX]

`banco` e o nome sem extensao (`godzilla_sound`, `godzilla_voice_e`, `shell_sound`, `rail_fpm`). Cada amostra sai como
`<nn>_<nome do som>.wav` (16 bits, mono, na taxa da amostra); uma amostra que varios sons usam leva o nome do primeiro.

Formato (Pipeworks, little endian):
- `.uber`: `UBER`, u16 versao, u16 ?, depois 6 u32 com os offsets das secoes: PROJ, POOL, SDIR, nomes, e dois de fim.
  - `PROJ`: `PROJ`, u32 versao, u32 n, u32 ?, n entradas de 12 bytes {f32 volume, u32 grupo, u16 roteiro no POOL, u16 ?}.
  - `POOL`: `POOL`, u32 versao, u32 n, u32 ?, n u32 com o offset (a partir do `POOL`) do roteiro de cada som. O roteiro e uma
    lista de instrucoes de 8 bytes (u8 opcode, ...); a 0x10 toca a amostra dos bytes +1,+2 (big endian, indice no SDIR), a
    0x06 toca o som dos bytes +2,+3 (big endian) e a 0x13 sorteia entre as que vem depois.
  - `SDIR`: `SDIR`, u32 versao, u32 tamanho do `.samp`, u32 n, n entradas de 16 bytes {u32 offset, u32 tamanho, u32 flags,
    u32 taxa}; a entrada 0 e vazia (offset 0xffffffff). Flags bit 0 = amostra em laco.
  - a 4a secao: u32 n, u32 ?, u32 offset do nome do projeto (que vem logo depois); a 6a: u32 n e n u32 com o offset
    absoluto do nome de cada som (ASCII NUL).
- `.samp`: as amostras em ADPCM do SPU2 (blocos de 16 bytes: u8 shift|filtro<<4, u8 flags, 14 bytes = 28 amostras).
"""
import os
import re
import struct
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ste_model as S

ADPCM_K = [(0.0, 0.0), (60 / 64.0, 0.0), (115 / 64.0, -52 / 64.0), (98 / 64.0, -55 / 64.0), (122 / 64.0, -60 / 64.0)]


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def parse_uber(ub):
    if ub[:4] != b'UBER':
        raise ValueError('nao e um .uber')
    secs = [u32(ub, 8 + 4 * i) for i in range(6)]
    pool, sdir, names_at = secs[1], secs[2], secs[5]
    # amostras
    if ub[sdir:sdir + 4] != b'SDIR':
        raise ValueError('SDIR fora do lugar')
    n = u32(ub, sdir + 12)
    samples = []
    for i in range(n):
        o = sdir + 16 + 16 * i
        off, size, flags, rate = struct.unpack_from('<4I', ub, o)
        samples.append({'off': off, 'size': size, 'flags': flags, 'rate': rate})
    # nomes dos sons
    nn = u32(ub, names_at)
    names = []
    for i in range(nn):
        p = u32(ub, names_at + 4 + 4 * i)
        names.append(ub[p:ub.index(b'\0', p)].decode('latin-1'))
    # som -> roteiro (PROJ) -> amostras que ele toca, direto (0x10) ou pelos sons que ele chama (0x06)
    proj = secs[0]
    pidx = [struct.unpack_from('<H', ub, proj + 16 + 12 * k + 8)[0] for k in range(u32(ub, proj + 8))]
    np_ = u32(ub, pool + 8)
    offs = [u32(ub, pool + 16 + 4 * i) for i in range(np_)] + [sdir - pool]
    direct, calls = [], []
    for i in range(np_):
        a, b = pool + offs[i], pool + offs[i + 1]
        smp, sub = [], []
        for o in range(a, b - 7, 8):
            if ub[o] == 0x10:
                smp.append(ub[o + 1] << 8 | ub[o + 2])
            elif ub[o] == 0x06:
                sub.append(ub[o + 2] << 8 | ub[o + 3])
        direct.append(smp)
        calls.append(sub)

    def resolve(k, depth=0):
        if k >= len(pidx) or depth > 8:
            return []
        p = pidx[k]
        out = list(direct[p])
        for s in calls[p]:
            out += [x for x in resolve(s, depth + 1) if x not in out]
        return out
    uses = [resolve(k) for k in range(len(names))]
    return samples, names, uses


def decode_adpcm(data):
    out = []
    s1 = s2 = 0.0
    for o in range(0, len(data) - 15, 16):
        sf, flags = data[o], data[o + 1]
        shift, filt = sf & 0x0F, (sf >> 4) & 0x0F
        k0, k1 = ADPCM_K[filt] if filt < 5 else (0.0, 0.0)
        for i in range(28):
            byte = data[o + 2 + i // 2]
            nib = (byte >> 4) if i & 1 else (byte & 0x0F)
            if nib >= 8:
                nib -= 16
            v = (nib << 12) >> shift
            v = v + s1 * k0 + s2 * k1
            s2, s1 = s1, v
            out.append(max(-32768, min(32767, int(round(v)))))
        if flags & 1 and o > 0 and not (flags & 2):   # fim da amostra (sem laco)
            break
    return out


def write_wav(path, pcm, rate):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack('<%dh' % len(pcm), *pcm))


def safe(nm):
    return re.sub(r'[^A-Za-z0-9_.-]+', '_', nm).strip('_') or 'som'


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    iso, bank, out = argv[0], argv[1], argv[2]
    vol = S.Vol(S.Iso(iso))
    ub = vol.get(bank + '.uber')
    samples, names, uses = parse_uber(ub)
    owner = {}
    for si, lst in enumerate(uses):
        for smp in lst:
            owner.setdefault(smp, []).append(si)
    pat = re.compile(argv[argv.index('--match') + 1], re.I) if '--match' in argv else None
    if '--list' in argv:
        for si, nm in enumerate(names):
            if pat and not pat.search(nm):
                continue
            print('som %4d  %-40s amostras %s' % (si, nm, ' '.join(str(x) for x in uses[si]) if si < len(uses) else '-'))
        return 0
    sp = vol.get(bank + '.samp')
    os.makedirs(out, exist_ok=True)
    nw = 0
    for i, s in enumerate(samples):
        if s['off'] == 0xFFFFFFFF or not s['size']:
            continue
        nms = [names[k] for k in owner.get(i, []) if k < len(names)]
        if pat and not any(pat.search(x) for x in nms):
            continue
        pcm = decode_adpcm(sp[s['off']:s['off'] + s['size']])
        fn = '%04d_%s.wav' % (i, safe(nms[0]) if nms else 'sem_nome')
        write_wav(os.path.join(out, fn), pcm, s['rate'] or 22050)
        nw += 1
    print('%s: %d sons, %d amostras, %d WAVs em %s' % (bank, len(names), len(samples) - 1, nw, out))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
