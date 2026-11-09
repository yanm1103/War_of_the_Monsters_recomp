"""Test: the editor's Test options (editor.test_options, test_env, reload_game): the lists of monsters, custom monsters and skins, the
WOTM_MATCH / WOTM_TRAINING / WOTM_TRAINING_DUMMY / WOTM_SPAWN a choice makes, what is refused, and the reload file's start point.
Needs the game disc configured in the editor (a custom stage reads its base); writes only into a temporary mods folder.
Run: python tools/level_editor/test_testopts.py
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


def refused(fn, *a):
    try:
        fn(*a)
    except ValueError as e:
        return str(e)
    raise AssertionError("accepted %r" % (a,))


def main():
    cfg = dict(editor.load_config())
    tmp = tempfile.mkdtemp(prefix="testopts_")
    try:
        cfg["mods"] = tmp

        def put(rel, obj):
            p = os.path.join(tmp, *rel.split("/"))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "w", encoding="utf-8") as f:
                json.dump(obj, f)

        put("A/monsters/cj/monster.json", {"id": "cj", "name": "CJ", "base": "ultra-v", "ai": True})
        put("A/monsters/boss/monster.json", {"id": "boss", "name": "BOSS", "base": "jelly"})          # no "ai": a CPU cannot be it
        put("A/skins/g54/skin.json", {"monster": "lizard", "id": "g54", "name": "GODZILLA 54", "ai": True})
        put("A/skins/purple/skin.json", {"monster": "ultra-v", "id": "purple", "name": "PURPLE"})
        put("A/skins/mine/skin.json", {"monster": "cj", "id": "mine", "name": "CJ ALT", "ai": True})
        os.makedirs(os.path.join(tmp, "A", "skins", "broken"))
        with open(os.path.join(tmp, "A", "skins", "broken", "skin.json"), "w") as f:
            f.write("{not json")
        # a custom stage on CENTRAL
        os.makedirs(os.path.join(tmp, "A", "stages", "mycity"))
        put("A/stages/mycity/stage.json", {"id": "mycity", "name": "MY CITY", "base": "CENTRAL"})

        o = editor.test_options(cfg)
        assert [m["id"] for m in o["monsters"]] == ["1", "2", "3", "4", "5", "7", "8", "9", "10", "11"], o["monsters"]
        assert o["monsters"][2]["name"] == "Togera"
        assert {c["id"]: c["ai"] for c in o["customs"]} == {"cj": True, "boss": False}, o["customs"]
        sk = {s["id"]: (s["monster"], s["ai"]) for s in o["skins"]}
        assert sk == {"g54": ("3", True), "purple": ("4", False), "mine": ("cj", True)}, sk       # the file prefix and the lower name both map

        # no options: Togera alone in training, as before; a custom stage takes @id
        assert editor.test_env(cfg, "CENTRAL", True, None) == {"WOTM_MATCH": "3,1", "WOTM_TRAINING": "1"}
        assert editor.test_env(cfg, "CENTRAL", False, None) == {"WOTM_MATCH": "3,1"}
        assert editor.test_env(cfg, "VEGAS", True, {})["WOTM_MATCH"] == "3,2"
        assert editor.test_env(cfg, "MYCITY", True, {})["WOTM_MATCH"] == "3,@mycity"
        # a monster, a costume, CPUs with a skin, a custom, the dummy and a start point
        env = editor.test_env(cfg, "CENTRAL", True, {
            "monster": {"monster": "4", "skin": "2"}, "cpus": [{"monster": "1", "skin": ""}, {"monster": "3", "skin": "g54"}, {"monster": "cj"}],
            "dummy": 1, "here": [10.5, -20, 30, 90]})
        assert env == {"WOTM_MATCH": "4:2,1,3:g54,cj,1", "WOTM_TRAINING": "1", "WOTM_TRAINING_DUMMY": "1", "WOTM_SPAWN": "10.50,-20.00,30.00,90.00"}, env
        # a custom monster of yours; no training: no dummy, and the CPUs fight
        env = editor.test_env(cfg, "MYCITY", False, {"monster": {"monster": "cj", "skin": "mine"}, "cpus": [{"monster": "2"}], "dummy": 3})
        assert env == {"WOTM_MATCH": "cj:mine,2,@mycity"}, env
        # no CPUs in training: the dummy does not apply
        assert "WOTM_TRAINING_DUMMY" not in editor.test_env(cfg, "CENTRAL", True, {"monster": {"monster": "3"}, "cpus": [], "dummy": 2})
        # refused
        for bad in ({"monster": {"monster": "6"}}, {"monster": {"monster": "12"}}, {"monster": {"monster": "nobody"}},
                    {"monster": {"monster": "3", "skin": "../x"}}, {"monster": {"monster": "3", "skin": "100"}},
                    {"cpus": [{"monster": "3"}] * 8}, {"cpus": ["3"]}, {"monster": {"monster": "3", "skin": "A;B"}},
                    {"here": [1, 2, 3]}, {"here": [1, 2, 3, float("nan")]}, {"here": [1, 2, 3, "x"]},
                    {"cpus": [{"monster": "1"}], "dummy": 5}, {"cpus": [{"monster": "1"}], "dummy": True}):
            refused(editor.test_env, cfg, "CENTRAL", True, bad)

        # the reload file: "<STAGE> <counter>", with the start point when asked
        class Alive:
            def poll(self):
                return None
        old = (editor.GAME["proc"], editor.RELOAD_FILE, editor.GAME["count"])
        editor.GAME["proc"] = Alive()
        editor.RELOAD_FILE = os.path.join(tmp, "reload.txt")
        try:
            editor.reload_game("CENTRAL")
            a = open(editor.RELOAD_FILE).read().split()
            assert len(a) == 2 and a[0] == "CENTRAL" and a[1].isdigit(), a
            editor.reload_game("CENTRAL", [1.5, 2, 3, -45])
            b = open(editor.RELOAD_FILE).read().split()
            assert b[:1] == ["CENTRAL"] and int(b[1]) == int(a[1]) + 1 and b[2:] == ["1.50", "2.00", "3.00", "-45.00"], b
            refused(editor.reload_game, "CENTRAL", [1, 2])
        finally:
            editor.GAME["proc"], editor.RELOAD_FILE, editor.GAME["count"] = old
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("ok")


if __name__ == "__main__":
    main()
