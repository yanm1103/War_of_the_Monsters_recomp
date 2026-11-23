"""Efeitos de particula de Godzilla: Save the Earth (PS2) -> "breathFx" de um monstro custom (MODS.md).

Uso:
    python tools/ste/ste_fx.py <iso> list <bundle> [REGEX]          # efeitos (.edf) do bundle, decodificados
    python tools/ste/ste_fx.py <iso> breath <bundle> <edf> <pasta> [--hit <edf>]   # PNGs + breathfx.json com as camadas
    (ex.: breath Godzilla2k.cmp atomicbreath fx --hit AtomicBreath_hit)

<bundle> e um arquivo do GODZILLA.VOL (Godzilla2k.cmp, fx.bdp); as texturas sao procuradas nele e no fx.bdp.

Formato .edf (entrada de tipo 20 do Pipeworks Bundle): texto, uma linha por atributo, "CHAVE=VALOR". Cada par de letras
maiusculas e um indice em base 26 (AA = 0) na tabela de nomes do ELF (336 ponteiros para strings; achada pelo ponteiro para
"graph.gen_rate", que e DT = 97); texto entre '?' e literal (nomes de textura com prefixo <ctrl>000). Uma .edf de particula tem
int.emitter_count blocos "{ ... }", um por emissor. Valores de grafico: "const N" ou "poly G c0 c1 .. cG" (polinomio de grau G na
idade normalizada da particula, 0..1). Flags: +AdditiveAlpha (mistura aditiva), +EmitterRelativeParticles (as particulas andam com
o emissor). enum.particle_type Stretch = risco esticado na direcao da velocidade: cresce ate val.stretch_percent_time da vida e
encolhe a partir de val.destretch_percent_time. graph.gen_rate = particulas por segundo; graph.em_vel.x = velocidade no eixo do
emissor (o do sopro aponta para a frente da boca); p_lifetime em segundos; p_size = lado do quadrado.

Texturas (tipo 9): descritor {3, psm, u16 w, u16 h, n mips, tamanho, ...}; psm 0x13 = PSMT8 (bit 30 = embaralhada no GS, ver
ste_model.unswizzle8). u32 em +0x20 = 2: intensidade de 8 bits sem paleta (as das particulas); outro valor: paleta na entrada
"<nome>.pal". Os PNGs saem brancos com alfa = intensidade.
"""
import argparse, json, os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ste_model import Iso, Vol, Bundle, unswizzle8, write_png
from skeleton import entry_names

TYPE_EDF, TYPE_TEX = 20, 9


def clean(s):
    return re.sub(r'^[\x01-\x1f]\d{0,3}', '', s)


def elf_dictionary(iso):
    """Tabela de nomes das .edf: ponteiros do ELF para strings, comecando 97 antes do de 'graph.gen_rate'."""
    lba, ln = next((v for k, v in iso.root.items() if k.upper().startswith('SLUS_')), (None, None))
    d = iso.read(lba * 2048, min(ln, 0x600000))
    phoff, = struct.unpack_from('<I', d, 0x1c)
    phnum, = struct.unpack_from('<H', d, 0x2c)
    segs = [struct.unpack_from('<IIIIII', d, phoff + 32 * i) for i in range(phnum)]
    segs = [(off, va, fsz) for t, off, va, pa, fsz, msz in segs if t == 1 and fsz]

    def cstr(va):
        for off, v, s in segs:
            if v <= va < v + s:
                o = off + va - v
                e = d.find(b'\0', o, o + 128)
                b = d[o:e] if e > o else b''
                return b.decode('latin1') if b and all(32 <= c < 127 for c in b) else None
        return None

    o = d.find(b'\0graph.gen_rate\0') + 1
    va = next(v + o - off for off, v, s in segs if off <= o < off + s)
    p = d.find(struct.pack('<I', va))
    start = p - 97 * 4
    out = []
    i = start
    while True:
        s = cstr(struct.unpack_from('<I', d, i)[0])
        if s is None:
            break
        out.append(s)
        i += 4
    return out


def decode_line(s, names):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == '?':
            j = s.find('?', i + 1)
            j = len(s) if j < 0 else j
            out.append(('lit', clean(s[i + 1:j])))
            i = j + 1
        elif c.isupper() and i + 1 < len(s) and s[i + 1].isupper():
            k = (ord(c) - 65) * 26 + ord(s[i + 1]) - 65
            out.append(('name', names[k] if k < len(names) else s[i:i + 2]))
            i += 2
        else:
            i += 1
    return out


def parse_edf(text, names):
    """[(chave, [tokens])] por bloco: {'head': [...], 'emitters': [[...], ...]}."""
    res = {'head': [], 'emitters': []}
    cur = res['head']
    for line in text.split('\n'):
        if not line:
            continue
        toks = decode_line(line, names)
        if toks == [('lit', '{')]:
            cur = []
            res['emitters'].append(cur)
            continue
        if toks == [('lit', '}')]:
            cur = res['head']
            continue
        if '=' in line:
            k, v = line.split('=', 1)
            kt = decode_line(k, names)
            cur.append((kt[0][1] if kt else k, decode_line(v, names)))
        else:
            cur.append((' '.join(t[1] for t in toks), []))
    return res


def fmt(v):
    return ' '.join(x if t == 'name' else '"%s"' % x for t, x in v)


def graph(v):
    """('const', [x]) ou ('poly', [c0, c1, ...])."""
    kind = v[0][1] if v and v[0][0] == 'name' else 'const'
    nums = [float(x) for x in ' '.join(x for t, x in v if t == 'lit').split()] if v else []
    if kind in ('poly', 'polynomial') and nums:
        return 'poly', nums[1:1 + int(nums[0]) + 1]
    if kind == 'lininterp' and nums:   # N pares (t, valor)
        n = int(nums[0])
        return 'lin', [(nums[1 + 2 * i], nums[2 + 2 * i]) for i in range(n)]
    return 'const', nums[:1] or [0.0]


def graph_mean(v):
    """O valor medio de um grafico sobre a idade normalizada 0..1 (taxa, tamanho, velocidade que variam no tempo)."""
    kind, vals = graph(v)
    if kind == 'poly':
        return sum(c / (i + 1) for i, c in enumerate(vals))
    if kind == 'lin':
        t0, t1 = vals[0][0], vals[-1][0]
        area = sum((vals[i + 1][0] - vals[i][0]) * (vals[i + 1][1] + vals[i][1]) / 2 for i in range(len(vals) - 1))
        return area / (t1 - t0) if t1 > t0 else vals[0][1]
    return vals[0]


def layer_of(em, scale_tex):
    a = dict(em)
    g = lambda k, d=0.0: graph_mean(a[k]) if k in a else d
    gc = lambda k, d=1.0: graph(a[k])[1][0] if k in a and graph(a[k])[0] == 'const' else d   # cor: o polinomio vai em colorPoly
    poly = lambda k: graph(a[k])[1] if k in a and graph(a[k])[0] == 'poly' else None
    flags = ' '.join(x for t, x in a.get('flags.particle.def', []))
    tex = next((x for t, x in a.get('textures.name', []) if t == 'lit'), '')
    tex = clean(tex.split()[0]) if tex.split() else ''   # varias texturas (quadros): fica a primeira
    eflags = ' '.join(x for t, x in a.get('flags.emitter.def', []))
    L = {
        'texture': scale_tex(tex),
        'mode': 'stretch' if fmt(a.get('enum.particle_type', [])) == 'Stretch' else 'sprite',
        'rate': g('graph.gen_rate'),
        'speed': g('graph.em_vel.x'),
        'speedRandom': g('graph.em_vel_r.x'),
        'radial': g('graph.vel_from_start'),
        'up': g('graph.em_vel.z'),
        'once': '+OneTimeEmission' in eflags,
        'rotate': '+RandomStartRotation' in flags,
        'life': [g('graph.p_lifetime.min'), g('graph.p_lifetime.max')],
        'size': [g('graph.p_size.min'), g('graph.p_size.max')],
        'spread': g('graph.start_area.x'),
        'color': [gc('graph.vertex_color.' + c) for c in 'rgba'],
        'additive': '+AdditiveAlpha' in flags,
    }
    for c in 'rgba':
        p = poly('graph.vertex_color.' + c)
        if p:
            L.setdefault('colorPoly', {})[c] = p
    p = poly('graph.size_mul')
    if p:
        L['sizePoly'] = p
    if L['mode'] == 'stretch':
        L['stretch'] = [float(fmt(a.get('val.stretch_percent_time', [('lit', '0')])).strip('"') or 0),
                        float(fmt(a.get('val.destretch_percent_time', [('lit', '1')])).strip('"') or 1)]
    return L


def textures(bundles):
    """{nome minusculo: (bundle, entrada, nomes)} de todas as texturas dos bundles."""
    out = {}
    for b in bundles:
        names = entry_names(b)
        for e in b.entries:
            t, nm = names.get(e['id'], (None, ''))
            if t == TYPE_TEX:
                out.setdefault(clean(nm).lower(), (b, e, names))
    return out


def write_texture(path, b, e, names):
    c = b.chunk(e)
    psm, = struct.unpack_from('<I', c, 4)
    w, h = struct.unpack_from('<HH', c, 8)
    pal_kind, = struct.unpack_from('<I', c, 0x20)
    if psm == 0:   # descritor {1, 0, ...}: PSMCT32 cru (RGBA, alfa 0..128)
        raw = b.tail_bytes(e)[:w * h * 4]
        write_png(path, w, h, b''.join(bytes((raw[i], raw[i + 1], raw[i + 2], min(255, raw[i + 3] * 2))) for i in range(0, w * h * 4, 4)))
        return
    assert psm & 0xff == 0x13, 'textura psm %x' % psm
    raw = b.tail_bytes(e)[:w * h]
    lin = unswizzle8(raw, w, h) if psm & 0x40000000 else raw
    if pal_kind == 2:
        rgba = b''.join(bytes((255, 255, 255, v)) for v in lin)
    else:
        nm = clean(names[e['id']][1]).lower() + '.pal'
        pe = next(x for x in b.entries if clean(names.get(x['id'], (0, ''))[1]).lower() == nm)
        pr = b.tail_bytes(pe)
        pal = [pr[i * 4:i * 4 + 4] for i in range(256)]
        p2 = list(pal)
        for gq in range(0, 256, 32):
            for k in range(8):
                p2[gq + 8 + k], p2[gq + 16 + k] = pal[gq + 16 + k], pal[gq + 8 + k]
        rgba = b''.join(bytes((p2[v][0], p2[v][1], p2[v][2], min(255, p2[v][3] * 2))) for v in lin)
    write_png(path, w, h, rgba)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('iso')
    ap.add_argument('cmd', choices=['list', 'breath', 'shot'])
    ap.add_argument('bundle')
    ap.add_argument('edf', nargs='?', default='')
    ap.add_argument('out', nargs='?')
    ap.add_argument('--hit', default='', help='efeito do acerto (camadas "at": "hit"), ex. AtomicBreath_hit')
    ap.add_argument('--impact', default='', help='shot: efeitos da explosao, separados por virgula (composite vira as partes)')
    ap.add_argument('--launch', default='', help='shot: efeito da saida do tiro, ex. fireball_release')
    a = ap.parse_args()
    iso = Iso(a.iso)
    vol = Vol(iso)
    names = elf_dictionary(iso)
    b = Bundle(vol.get(a.bundle))
    en = entry_names(b)
    edfs = {clean(nm): b.chunk(e).decode('latin1') for e in b.entries
            for t, nm in [en.get(e['id'], (None, ''))] if t == TYPE_EDF}
    if a.cmd == 'list':
        for nm, text in edfs.items():
            if a.edf and not re.search(a.edf, nm, re.I):
                continue
            p = parse_edf(text, names)
            print('==== %s' % nm)
            for k, v in p['head']:
                print('  %-30s %s' % (k, fmt(v)))
            for i, em in enumerate(p['emitters']):
                print('  -- emissor %d' % i)
                for k, v in em:
                    print('    %-30s %s' % (k, fmt(v)))
        return
    if a.bundle.lower() != 'fx.bdp':   # efeitos comuns (explosoes, fumaca) vem do fx.bdp quando o bundle do monstro nao os tem
        fb = Bundle(vol.get('fx.bdp'))
        fen = entry_names(fb)
        for e in fb.entries:
            t, nm = fen.get(e['id'], (None, ''))
            if t == TYPE_EDF:
                edfs.setdefault(clean(nm), fb.chunk(e).decode('latin1'))
    key = next((k for k in edfs if k.lower() == a.edf.lower()), None)
    if not key or not a.out:
        sys.exit('efeito %r nao achado (use list)' % a.edf)
    p = parse_edf(edfs[key], names)
    os.makedirs(a.out, exist_ok=True)
    texs = textures([b] + ([Bundle(vol.get('fx.bdp'))] if a.bundle.lower() != 'fx.bdp' else []))
    used = {}

    def tex_file(nm):
        if not nm:
            return ''
        if nm.lower() not in used:
            hit = texs.get(nm.lower())
            if not hit:
                print('textura %s nao achada' % nm)
                return ''
            fn = nm.lower() + '.png'
            write_texture(os.path.join(a.out, fn), *hit)
            used[nm.lower()] = fn
            print('textura %s -> %s' % (nm, fn))
        return 'fx/' + used[nm.lower()]

    def emitters(name, seen=()):
        """Emissores de particula de um efeito; composite vira os das partes (as que nao sao de particula ficam de fora)."""
        k = next((x for x in edfs if x.lower() == name.lower().replace('.edf', '')), None)
        if not k or k in seen:
            print('efeito %s nao achado ou repetido' % name)
            return []
        q = parse_edf(edfs[k], names)
        head = dict(q['head'])
        kind = fmt(head.get('type', [])).strip('"')
        if kind == 'composite':
            n = int(fmt(head.get('int.num_edfs', [('lit', '0')])).strip('"') or 0)
            out = []
            for i in range(n):
                part = fmt(head.get('edf.name.%d' % i, [])).strip('"')
                out += emitters(part, seen + (k,))
            return out
        if kind != 'particle':
            print('efeito %s: tipo %s, fica de fora' % (k, kind))
            return []
        return [em for em in q['emitters'] if '+NoRender' not in ' '.join(x for t, x in dict(em).get('flags.particle.def', []))]

    if a.cmd == 'shot':   # tiro proprio ("shotFx" do monster.json): voo, explosao e saida
        layers = []
        for em in emitters(key):
            L = layer_of(em, tex_file)
            L['at'] = 'shot'
            L['relative'] = '+EmitterRelativeParticles' in ' '.join(x for t, x in dict(em).get('flags.emitter.def', []))
            layers.append(L)
        for site, lst in (('impact', a.impact), ('launch', a.launch)):
            for nm in [x for x in lst.split(',') if x]:
                for em in emitters(nm):
                    L = layer_of(em, tex_file)
                    L['at'] = site
                    layers.append(L)
        with open(os.path.join(a.out, 'shotfx.json'), 'w') as f:
            json.dump({'layers': layers}, f, indent=2)
        print('%d camadas -> %s' % (len(layers), os.path.join(a.out, 'shotfx.json')))
        return
    layers = [layer_of(em, tex_file) for em in p['emitters']]
    if a.hit:   # efeito do acerto: camadas que nascem onde o sopro bate
        hk = next((k for k in edfs if k.lower() == a.hit.lower()), None)
        if not hk:
            sys.exit('efeito %r nao achado (use list)' % a.hit)
        for em in parse_edf(edfs[hk], names)['emitters']:
            L = layer_of(em, tex_file)
            L['at'] = 'hit'
            layers.append(L)
    with open(os.path.join(a.out, 'breathfx.json'), 'w') as f:
        json.dump({'layers': layers}, f, indent=2)
    print('%d camadas -> %s' % (len(layers), os.path.join(a.out, 'breathfx.json')))


if __name__ == '__main__':
    main()
