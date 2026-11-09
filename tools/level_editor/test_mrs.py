"""Test: the editor's Light dialog on the server side (lvl.mrs_values / mrs_edit and editor.stage_mrs / save_mrs / stage_env) for a game
stage and for a custom stage. Needs the game disc configured in the editor; writes only into a temporary mods folder.
Run: python tools/level_editor/test_mrs.py
"""
import json
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE)))
sys.path.insert(0, HERE)
import editor   # noqa: E402
import lvl      # noqa: E402


def main():
    cfg = dict(editor.load_config())
    tmp = tempfile.mkdtemp(prefix="wotm_mrstest_")
    cfg["mods"] = tmp
    try:
        disc = os.path.join(editor.stage_root(cfg["disc"], "CENTRAL"), "RST", "CENTRAL.MRS")
        data = open(disc, "rb").read()
        v = lvl.mrs_values(data)
        assert v["fog"] and v["bg"] and len(v["lights"]) == 3, v
        assert v["fog"]["minR"] == 1200 and v["fog"]["maxR"] == 16380 and v["fog"]["rgb"] == [37, 20, 13], v["fog"]
        assert [round(x) for x in v["lights"][0]["rgb"]] == [190, 140, 110] and v["lights"][0]["h"] == 120.0
        # putting back what it read changes nothing, byte for byte
        same = lvl.mrs_edit(data, v["fog"], v["bg"][:3], v["lights"])
        assert same == data, "round trip"
        # a change touches only its own entries
        new = lvl.mrs_edit(data, fog=dict(v["fog"], rgb=[1, 2, 3], minR=500.0), bg=[1, 2, 3])
        assert len(new) == len(data)
        diff = [i for i in range(len(data)) if new[i] != data[i]]
        ents = {k: (off + 8, off + size) for off, k, size in lvl.mrs_entries(data)}
        for i in diff:
            assert any(a <= i < b for k, (a, b) in ents.items() if k in (2, 4)), "byte %d outside the fog and background" % i
        w = lvl.mrs_values(new)
        assert w["fog"]["rgb"] == [1, 2, 3] and w["fog"]["minR"] == 500.0 and w["bg"][:3] == [1, 2, 3] and w["bg"][3] == v["bg"][3]
        assert w["lights"] == v["lights"]
        # refused: out of range, not numbers, a part the file has no entry for
        for bad in (dict(fog=dict(v["fog"], rgb=[0, 0, 256])), dict(fog=dict(v["fog"], farClip=0)), dict(bg=[0, 0]),
                    dict(lights=v["lights"][:2]), dict(lights=[dict(l, p=91) for l in v["lights"]]),
                    dict(fog=dict(v["fog"], minR=float("nan")))):
            try:
                lvl.mrs_edit(data, **bad)
            except ValueError:
                continue
            raise AssertionError("accepted %r" % (bad,))
        try:
            lvl.mrs_edit(b"\0" * 12 + (1).to_bytes(4, "little") + (8).to_bytes(4, "little"), bg=[1, 2, 3])
        except ValueError:
            pass
        else:
            raise AssertionError("edited a file with no background entry")

        # a game stage: nothing in the mod -> the disc's; save -> the mod's file; the env follows; reset -> the disc's again
        os.makedirs(os.path.join(tmp, "T"))
        r = editor.stage_mrs(cfg, "T", "CENTRAL")
        assert r["source"] == "disc" and r["fog"]["rgb"] == [37, 20, 13], r
        r = editor.save_mrs(cfg, {"mod": "T", "name": "CENTRAL", "fog": dict(v["fog"], rgb=[200, 100, 50], maxR=1000.0),
                                  "bg": [200, 100, 50], "lights": v["lights"]})
        mine = os.path.join(tmp, "T", "RST", "CENTRAL.MRS")
        assert os.path.isfile(mine) and r["source"] == "mod" and r["fog"]["rgb"] == [200, 100, 50], r
        assert r["warnings"] and "ends before" in r["warnings"][0], r["warnings"]      # maxR 1000 < minR 1200
        env = editor.stage_env(cfg, cfg["disc"], "CENTRAL", "T")
        assert env["bg"] == [200, 100, 50] and env["file"] == mine and "fa" not in env["fog"], env["fog"]
        assert editor.stage_env(cfg, cfg["disc"], "CENTRAL", "")["file"] == disc
        r = editor.save_mrs(cfg, {"mod": "T", "name": "CENTRAL", "fog": dict(v["fog"], rgb=[9, 9, 9], maxR=9000.0)})
        assert not r["warnings"] and open(mine, "rb").read()[:12] == data[:12] and len(open(mine, "rb").read()) == len(data)
        r = editor.save_mrs(cfg, {"mod": "T", "name": "CENTRAL", "reset": True})
        assert r["source"] == "disc" and not os.path.exists(os.path.join(tmp, "T", "RST")), r
        try:
            editor.save_mrs(cfg, {"mod": "../x", "name": "CENTRAL", "bg": [0, 0, 0]})
        except ValueError:
            pass
        else:
            raise AssertionError("accepted a bad mod name")

        # a custom stage keeps it in its folder, and only that stage takes it
        sdir = os.path.join(tmp, "T", "stages", "mycity")
        os.makedirs(sdir)
        json.dump({"id": "mycity", "name": "MY CITY", "base": "CENTRAL"}, open(os.path.join(sdir, "stage.json"), "w"))
        assert editor.custom_stages(cfg).get("MYCITY"), "custom stage not seen"
        editor.save_mrs(cfg, {"mod": "T", "name": "MYCITY", "bg": [5, 6, 7], "fog": dict(v["fog"], rgb=[5, 6, 7])})
        assert os.path.isfile(os.path.join(sdir, "MYCITY.MRS")) and not os.path.exists(os.path.join(tmp, "T", "RST"))
        assert editor.stage_mrs(cfg, "T", "MYCITY")["fog"]["rgb"] == [5, 6, 7]
        assert editor.stage_mrs(cfg, "T", "CENTRAL")["source"] == "disc"
        assert editor.stage_env(cfg, cfg["disc"], "MYCITY", "T")["bg"] == [5, 6, 7]
        editor.save_mrs(cfg, {"mod": "T", "name": "MYCITY", "reset": True})
        assert not os.path.exists(os.path.join(sdir, "MYCITY.MRS")) and os.path.isdir(sdir)
        print("ok")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
