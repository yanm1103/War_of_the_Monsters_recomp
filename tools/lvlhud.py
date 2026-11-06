#!/usr/bin/env python3
"""The fight HUD a stage carries (level editor, HUD panel): each LVL/<STAGE>.NGP holds the HUD nodes (ids 0x1C20..0x2133, read by
Hud::parseHudElements) and their textures sit in the same stage's .TEX/.RTX, so a mod replaces them with PNGs named after the stage
(`<stage>_t<id>.png`, `<stage>_rtx_t<id>.png`, MODS.md "Textures as PNG"). The texIds differ between stages: find them by the HUD
node ids, never by a fixed table or by the root's index.

  python tools/lvlhud.py <STAGE> [--disc <folder>]     # the stage's HUD textures: role, file, texId, size, users
  python tools/lvlhud.py --all [--disc <folder>]       # texId of each role in every stage, and the version (A or B)

Version B (CANYON1, TOKYODM): another bar atlas, special icon, health icon, font and meshes; its boss bar has no "BOSS" label.
"""
import io
import contextlib
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lvl   # noqa: E402
import ngp   # noqa: E402
import tex   # noqa: E402

HUD_IDS = (0x1C20, 0x2134)          # id1 range dbProcInteractive hands to Hud::parseHudElements (not opcodes 0x1F/0x27)
FONT_ID = 18                        # fontSetTexId: the fight font (an OBJECT, not a HUD node)
BUTTONS = (0x1F51, 0x1F52, 0x1F53, 0x1F54)

# role -> (label, HUD ids whose textures it is). "same": the same picture in every stage (A and B).
ROLES = [
    ("atlas", "HUD frame and bars (atlas)", (0x1F48,), False),
    ("special", "Special move icon (R)", (0x1F4D,), False),
    ("boss", "BOSS label of the boss bar", (0x1F55,), False),
    ("healthicon", "Player number over the enemy", (0x1F46,), False),
    ("font", "Fight text font", (), False),
    ("ring", "Ring around a pickup", (0x1F42,), True),
    ("textbox", "Hint box frame", (0x1F43,), True),
    ("letterbox", "Cinema bars", (0x1F44,), True),
    ("buttons", "Button prompts", BUTTONS, True),
]
ROLE = {r[0]: r for r in ROLES}
NOTES = {"textbox": "also drawn by the stage's end point (node 0x262A)"}


def _walk(img):
    """Every node the load walk reaches (dbsTraverse from every root; ANIM_MGR leaves are not followed)."""
    seen, out = set(), []
    for r in img.roots():
        stack = [r & lvl.MASK]
        while stack:
            n = stack.pop()
            if n in seen or not img.ok(n):
                continue
            seen.add(n)
            out.append(n)
            if img.u32(n) & 63 == 7:
                continue
            stack.extend(lvl.children(img, n))
    return out


def _texids(img, n):
    """texIds of the OBJECTs under node n (slots at +0x50, count at +0x5A)."""
    out, seen, stack = set(), set(), [n]
    while stack:
        m = stack.pop()
        if m in seen:
            continue
        seen.add(m)
        if img.u32(m) & 63 == 0 and img.ok(m, 0xA0):
            for s in range(min(img.u16(m + 0x5A), 5)):
                out.add(img.u16(m + 0x50 + 2 * s))
        stack.extend(lvl.children(img, m))
    return out


def _packet_ids(path):
    if not os.path.isfile(path):
        return set()
    data, _ = tex.read_file(path)
    return {p.texId for p in tex.parse(data)["packets"] if not p.is_palette()}


def stage_hud(root, stage):
    """The stage's HUD: {"stage", "version": "A"|"B", "roles": {role: [{"tex", "file", "w", "h", "psm"}]}, "users": {tex: [ids]}}.
    root holds LVL/<STAGE>.NGP/.TEX/.RTX."""
    base = os.path.join(root, "LVL", stage.upper())
    img = ngp.Image.load(base + ".NGP")
    by_id, users, font = {}, {}, set()
    for n in _walk(img):
        w = img.u32(n)
        op, id1 = w & 63, w >> 18
        if id1 == FONT_ID and op == 0 and img.ok(n, 0xA0):
            font.add(img.u16(n + 0x50))
        if HUD_IDS[0] <= id1 < HUD_IDS[1] and op not in (0x1F, 0x27):
            t = _texids(img, n)
            by_id.setdefault(id1, set()).update(t)
            for k in t:
                users.setdefault(k, set()).add(id1)
    for k in font:
        users.setdefault(k, set()).add(FONT_ID)
    atlas = by_id.get(0x1F48, set())
    sets = {}
    for key, _label, ids, _same in ROLES:
        s = set().union(*(by_id.get(i, set()) for i in ids)) if ids else set(font)
        if key == "boss":
            s -= atlas
        sets[key] = s
    rtx_ids, tex_ids = _packet_ids(base + ".RTX"), _packet_ids(base + ".TEX")
    sizes = {}
    for path in (base + ".TEX", base + ".RTX"):
        if os.path.isfile(path):
            data, _ = tex.read_file(path)
            for p in tex.parse(data)["packets"]:
                if not p.is_palette():
                    sizes.setdefault(p.texId, (p.w, p.h, tex.PSM_NAMES.get(p.psm, hex(p.psm))))
    roles = {}
    for key, s in sets.items():
        roles[key] = [{"tex": t, "file": "rtx" if t in rtx_ids and t not in tex_ids else "tex",
                       "w": sizes.get(t, (0, 0, "?"))[0], "h": sizes.get(t, (0, 0, "?"))[1], "psm": sizes.get(t, (0, 0, "?"))[2]}
                      for t in sorted(s)]
    return {"stage": stage.upper(), "version": "A" if roles["boss"] else "B", "roles": roles,
            "users": {t: sorted(v) for t, v in users.items()}}


def png_name(stage, entry):
    """The PNG name the native drawing looks for (native/wotm_texmods.hpp), as tools/tex.py export writes it."""
    return "%s%s_t%04d.png" % (stage.lower(), "_rtx" if entry["file"] == "rtx" else "", entry["tex"])


def export_stage(root, stage, outdir):
    """The stage's .TEX and .RTX as PNGs (tools/tex.py export) in outdir, once; returns outdir."""
    base = os.path.join(root, "LVL", stage.upper())
    stamp = os.path.join(outdir, ".done")
    if not os.path.isfile(stamp):
        os.makedirs(outdir, exist_ok=True)
        with contextlib.redirect_stdout(io.StringIO()):
            tex.cmd_export(base + ".TEX", outdir)
            if os.path.isfile(base + ".RTX"):
                tex.cmd_export(base + ".RTX", outdir)
        open(stamp, "w").close()
    return outdir


def mod_pngs(mod_dir, stage):
    """{png name (lower case): path} of the stage's texture PNGs anywhere in the mod (the file name is what counts)."""
    pat = re.compile(r"^%s(_rtx)?_t\d{4}\.png$" % re.escape(stage.lower()))
    out = {}
    for d, _dirs, files in os.walk(mod_dir):
        for f in files:
            if pat.match(f.lower()):
                out[f.lower()] = os.path.join(d, f)
    return out


def panel(info, mod_dir, exported):
    """What the editor's HUD panel shows for one stage: one row per role texture, with the disc PNG (in `exported`, from
    export_stage) and the mod's PNG if it has one (anywhere in mod_dir)."""
    stage = info["stage"]
    have = mod_pngs(mod_dir, stage) if mod_dir and os.path.isdir(mod_dir) else {}
    rows = []
    for key, label, _ids, same in ROLES:
        for e in info["roles"][key]:
            name = png_name(stage, e)
            rows.append({"role": key, "label": label, "same": same, "note": NOTES.get(key, ""), "png": name, "tex": e["tex"],
                         "file": e["file"], "w": e["w"], "h": e["h"], "psm": e["psm"],
                         "disc": os.path.join(exported, name) if os.path.isfile(os.path.join(exported, name)) else None,
                         "mod": have.get(name.lower())})
    return {"stage": stage, "version": info["version"], "rows": rows}


def targets(infos, stage, role, every):
    """Stages and PNG names a replacement of `role` in `stage` goes to: only that stage, or (every) each stage whose picture of
    that role is the same kind: all stages for the roles that are the same in A and B, else the stages of the same version.
    infos: {stage: stage_hud(...)}. [(stage, png name)]."""
    if not every:
        return [(stage, png_name(stage, e)) for e in infos[stage]["roles"][role]]
    same = ROLE[role][3]
    version = infos[stage]["version"]
    out = []
    for st, info in sorted(infos.items()):
        if same or info["version"] == version:
            out += [(st, png_name(st, e)) for e in info["roles"][role]]
    return out


def write_mod_png(mod_dir, name, data):
    """Write a PNG (bytes) as <mod>/LVL/<name>, removing the same name elsewhere in the mod (the game takes any folder)."""
    for old in [p for n, p in mod_pngs(mod_dir, name.split("_")[0]).items() if n == name.lower()]:
        os.remove(old)
    out = os.path.join(mod_dir, "LVL", name)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    tmp = out + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, out)
    return out


def remove_mod_png(mod_dir, name):
    """Remove the mod's PNG of that name, wherever it is in the mod. The removed paths."""
    gone = [p for n, p in mod_pngs(mod_dir, name.split("_")[0]).items() if n == name.lower()]
    for p in gone:
        os.remove(p)
    return gone


def describe(info):
    lines = ["%s: HUD version %s" % (info["stage"], info["version"])]
    for key, label, _ids, same in ROLES:
        for e in info["roles"][key]:
            users = ", ".join("0x%X" % i if i != FONT_ID else "font" for i in info["users"].get(e["tex"], []))
            lines.append("  %-10s %-34s %-22s %4dx%-4d %-5s %s%s" % (
                key, label, png_name(info["stage"], e), e["w"], e["h"], e["psm"], users,
                "  (" + NOTES[key] + ")" if key in NOTES else ""))
        if not info["roles"][key]:
            lines.append("  %-10s %-34s (none)" % (key, label))
    return lines


def main(argv):
    disc = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "disc")
    if "--disc" in argv:
        i = argv.index("--disc")
        disc = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    if not argv:
        print(__doc__)
        return 1
    if argv[0] == "--all":
        stages = sorted(os.path.splitext(f)[0] for f in os.listdir(os.path.join(disc, "LVL")) if f.upper().endswith(".NGP"))
        print("| stage | version | " + " | ".join(r[0] for r in ROLES) + " |")
        for st in stages:
            info = stage_hud(disc, st)
            cells = [",".join(("rtx " if e["file"] == "rtx" else "") + str(e["tex"]) for e in info["roles"][r[0]]) or "-"
                     for r in ROLES]
            print("| %s | %s | %s |" % (st, info["version"], " | ".join(cells)))
        return 0
    for line in describe(stage_hud(disc, argv[0])):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
