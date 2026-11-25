#!/usr/bin/env python3
"""Every check of the project in one command, showing only what is new.

    python tools/check.py                 # the static checks; lists the findings that are not in the baseline
    python tools/check.py --all           # the known (accepted) findings too
    python tools/check.py --accept        # the findings of this run become the baseline (tools/check_baseline.json)
    python tools/check.py --only moves,anims
    python tools/check.py --smoke         # also plays a short AI fight with every monster of mods/ (tools/smoke.ps1; needs
                                          # the runtime, about a minute and a half per monster)
    python tools/check.py --smoke-json <smoke.json>   # reads a smoke that already ran

Checks (name: what it runs):
    docs    tools/doclint.py (the lines the commit adds and TODO.md) and tools/wotm_env.py (the WOTM_* table of NATIVE.md)
    fp      tools/fplint.py: float tests in native/ that /fp:fast folds away
    json    every mods/*/monsters/*/monster.json: keys the runtime does not read (the runtime ignores them silently)
    mods    tools/modkit.py check on mods/: files of a mod the game would not load
    moves   tools/move_check.py on every mod monster with its own .CHR and model: errors and warnings
    anims   tools/anim_check.py on every mod monster with its own model: errors and warnings
    smoke   the [check] lines of the runtime, crashes and freezes of the smoke fights (only with --smoke or --smoke-json)

Findings of severity info (what a check could not cover, what is only worth a look) are listed as notes: they never fail the
run and never enter the baseline; moves and anims only produce them with --all.
A finding has a stable key (check, subject, code, detail without numbers that drift). The baseline holds the keys someone
looked at and accepted, per check; a finding whose key is there is "known" and stays out of the default listing. A key of the
baseline that no longer shows up is reported as fixed (--accept drops it). The baseline of a check that did not run is kept.
Exit code 1 when there is a new finding.
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
BASELINE = os.path.join(TOOLS, "check_baseline.json")
MODS = os.path.join(ROOT, "mods")
CHECKS = ("docs", "fp", "json", "mods", "moves", "anims", "smoke")
sys.path.insert(0, TOOLS)


def finding(check, subject, code, title, text="", detail="", sev="warn"):
    """detail: what tells two findings of the same code apart (a move, a file, a key); it goes in the key, so no measured numbers."""
    return {"check": check, "subject": subject, "code": code, "sev": sev, "title": title, "text": text,
            "key": " | ".join(x for x in (subject, code, detail) if x)}


def run_tool(args):
    r = subprocess.run([sys.executable] + args, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.returncode, (r.stdout + r.stderr).splitlines()


def mod_dirs():
    return sorted(d for d in glob.glob(os.path.join(MODS, "*")) if os.path.isdir(d) and os.path.basename(d)[0] not in "_.")


# ---------------------------------------------------------------- the checks

def check_docs():
    out = []
    rc, lines = run_tool(["tools/doclint.py"])
    if rc:
        for l in lines:
            if l.strip():
                out.append(finding("docs", "doclint", "doclint", l.strip(), detail=re.sub(r":\d+", "", l.strip())[:160], sev="error"))
    rc, lines = run_tool(["tools/wotm_env.py"])
    if rc:
        for l in lines:
            if "WOTM_" in l:
                out.append(finding("docs", "wotm_env", "env_table", l.strip(), "The WOTM_* table of docs/guides/NATIVE.md and the code disagree "
                                   "(python tools/wotm_env.py --write adds the missing rows).", detail=l.strip()[:160], sev="error"))
    return out


def check_fp():
    import fplint
    return [finding("fp", f["file"], "self_compare", "%s:%d: %s" % (f["file"], f["line"], f["message"]), f["text"], detail=f["text"][:160], sev="error")
            for f in fplint.findings()]


def runtime_keys():
    """Every key the runtime asks a JSON object for in native/wotm_mods.hpp: get("key")."""
    with open(os.path.join(ROOT, "native", "wotm_mods.hpp"), encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    # get("key"), and the helpers that take the object and the key: color(v, "fireColor", ...)
    return set(re.findall(r'get\("([A-Za-z_]\w*)"\)', text)) | set(re.findall(r'\b\w+\(\*?\w+, "([A-Za-z_]\w*)"', text))


def check_json():
    out, known = [], runtime_keys()
    for path in sorted(glob.glob(os.path.join(MODS, "*", "monsters", "*", "monster.json"))):
        rel = os.path.relpath(path, ROOT).replace("\\", "/")
        if os.path.basename(os.path.dirname(os.path.dirname(os.path.dirname(path))))[0] in "_.":
            continue
        try:
            with open(path, encoding="utf-8") as fh:
                data = json.load(fh)
        except (OSError, ValueError) as e:
            out.append(finding("json", rel, "bad_json", "%s does not parse" % rel, str(e), sev="error"))
            continue
        unknown = set()

        def walk(v, depth=0):
            if isinstance(v, dict):
                for k, x in v.items():
                    if k not in known and not k.startswith("_"):
                        unknown.add(k)
                    walk(x, depth + 1)
            elif isinstance(v, list):
                for x in v:
                    walk(x, depth + 1)

        walk(data)
        for k in sorted(unknown):
            out.append(finding("json", rel, "unknown_key", '%s: the runtime does not read "%s"' % (rel, k),
                               "A key the runtime never asks for is ignored without a message: a typo here leaves the default in place. "
                               'Keys that start with "_" are notes and are not checked.', detail=k))
    return out


def check_mods():
    out, mod = [], ""
    _rc, lines = run_tool(["tools/modkit.py", "check", "--mods", MODS])
    for l in lines:
        if l and not l.startswith(" ") and "error(s)" not in l and "conflicts" not in l:
            mod = l.strip()
        m = re.match(r"\s+(\S+)\s+ERROR: (.*)$", l)
        if m:
            out.append(finding("mods", mod, "modkit_check", "%s: %s: %s" % (mod, m.group(1), m.group(2)), detail=m.group(1) + ": " + m.group(2), sev="error"))
        m = re.match(r"\s+(.+?)\s{2,}(.+ -> .+ wins)$", l)
        if m:
            out.append(finding("mods", "conflict", "mod_conflict", "two mods replace %s: %s" % (m.group(1), m.group(2)), detail=m.group(1)))
    return out


def check_moves(every=False):
    import chr_view as cv
    import move_check as mc
    out = []
    for mod in mod_dirs():
        for cid, c, n in mc.mod_monsters(mod):
            try:
                a = mc.check(cv.moves_of(c, n, check=False), n)
            except Exception as e:  # noqa: BLE001 - a monster that cannot be read is a finding, not a crash of the whole run
                out.append(finding("moves", cid, "unreadable", "%s: the moves cannot be read" % cid, repr(e), sev="error"))
                continue
            for f in a["findings"]:
                if f["sev"] == "info" and not every:
                    continue
                out.append(finding("moves", cid, f["code"], "%s: %s: %s" % (cid, f["where"], f["title"]), f["text"] + "\nFix: " + f["fix"],
                                   detail=f["where"], sev=f["sev"]))
    return out


def check_anims(every=False):
    try:
        import anim_check as ac
    except ImportError:
        return []
    out = []
    for mod in mod_dirs():
        for res in ac.check_mod(mod):
            for f in res["findings"]:
                sev = f.get("severity") or f.get("sev") or "warn"
                if sev == "info" and not every:
                    continue
                anims = ",".join(str(x) for x in f.get("anims", []))
                out.append(finding("anims", res["monster"], f["code"], "%s: %s" % (res["monster"], f["title"]),
                                   f.get("text", "") + ("\nFix: " + f["hint"] if f.get("hint") else ""), detail=anims, sev=sev))
    return out


def check_smoke(path):
    with open(path, encoding="utf-8-sig") as fh:
        data = json.load(fh)
    found = data.get("findings") or []
    if isinstance(found, dict):   # ConvertTo-Json writes a single element without the list
        found = [found]
    out = []
    for f in found:
        code, _, detail = str(f["code"]).partition(":")
        sev = "error" if code in ("crash", "frozen", "exited", "no_fight") else "info" if code == "breath_not_seen" else "warn"
        out.append(finding("smoke", f["monster"], code, "%s: %s (x%s)" % (f["monster"], f["code"], f.get("count", 1)), f.get("text", ""), detail=detail, sev=sev))
    return out


def run_smoke(secs):
    out_dir = os.path.join(os.environ.get("TEMP", ROOT), "wotm_smoke_check")
    cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", os.path.join(TOOLS, "smoke.ps1"), "-Out", out_dir, "-Secs", str(secs)]
    subprocess.run(cmd, cwd=ROOT, check=True)
    return os.path.join(out_dir, "smoke.json")


# ---------------------------------------------------------------- baseline and report

def load_baseline():
    try:
        with open(BASELINE, encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return {}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", help="comma separated checks: " + ", ".join(CHECKS))
    ap.add_argument("--all", action="store_true", help="list the known findings and the info ones too")
    ap.add_argument("--accept", action="store_true", help="write the findings of this run as the baseline")
    ap.add_argument("--smoke", action="store_true", help="run tools/smoke.ps1 and read its findings")
    ap.add_argument("--smoke-json", help="read the findings of a smoke that already ran")
    ap.add_argument("--secs", type=int, default=70, help="seconds of each smoke fight")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    only = [x.strip() for x in a.only.split(",")] if a.only else [c for c in CHECKS if c != "smoke"]
    bad = [c for c in only if c not in CHECKS]
    if bad:
        ap.error("unknown check: %s" % ", ".join(bad))
    if (a.smoke or a.smoke_json) and "smoke" not in only:
        only.append("smoke")
    runners = {"docs": check_docs, "fp": check_fp, "json": check_json, "mods": check_mods, "moves": lambda: check_moves(a.all),
               "anims": lambda: check_anims(a.all)}
    found, ran = [], []
    for c in CHECKS:
        if c not in only:
            continue
        if c == "smoke":
            path = a.smoke_json or (run_smoke(a.secs) if a.smoke else None)
            if not path:
                continue
            found += check_smoke(path)
        else:
            if not a.json:
                print("... %s" % c, file=sys.stderr)
            found += runners[c]()
        ran.append(c)

    base = load_baseline()
    notes = [f for f in found if f["sev"] == "info"]   # worth reading, never a failure and never in the baseline
    found = [f for f in found if f["sev"] != "info"]
    new = [f for f in found if f["key"] not in base.get(f["check"], {})]
    known = [f for f in found if f["key"] in base.get(f["check"], {})]
    keys = {c: {f["key"] for f in found if f["check"] == c} for c in ran}
    fixed = [(c, k) for c in ran for k in sorted(base.get(c, {})) if k not in keys[c]]

    if a.accept:
        for c in ran:
            base[c] = {f["key"]: f["title"] for f in sorted(found, key=lambda f: f["key"]) if f["check"] == c}
        with open(BASELINE, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(base, fh, indent=1, sort_keys=True, ensure_ascii=False)
            fh.write("\n")
        print("baseline: %d finding(s) accepted in %s" % (sum(len(base[c]) for c in ran), os.path.relpath(BASELINE, ROOT)))
        return 0
    if a.json:
        print(json.dumps({"ran": ran, "new": new, "known": known, "notes": notes, "fixed": [{"check": c, "key": k} for c, k in fixed]}, indent=1))
        return 1 if new else 0

    def show(fs, full):
        for f in sorted(fs, key=lambda f: (CHECKS.index(f["check"]), {"error": 0, "warn": 1, "info": 2}.get(f["sev"], 1), f["key"])):
            print("  [%s] %-5s %s" % (f["check"], f["sev"], f["title"]))
            if full and f["text"]:
                for l in f["text"].split("\n"):
                    print("          %s" % l)

    print("checks: %s" % ", ".join(ran))
    if new:
        print("NEW (%d):" % len(new))
        show(new, True)
    if a.all and known:
        print("KNOWN (%d, in the baseline):" % len(known))
        show(known, False)
    if notes:
        print("NOTES (%d, not failures):" % len(notes))
        show(notes, a.all)
    if fixed:
        print("FIXED (%d, in the baseline but gone; --accept drops them):" % len(fixed))
        for c, k in fixed:
            print("  [%s] %s" % (c, k))
    print("%d new, %d known, %d fixed" % (len(new), len(known), len(fixed)))
    return 1 if new else 0


if __name__ == "__main__":
    sys.exit(main())
