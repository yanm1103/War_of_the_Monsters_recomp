"""Test: the editor names the sky OBJECTs a model replaces in each stage (editor.py Stages.sky, /api/stage "sky"), the same ones
lvl.py's sky_plan and sky_clouds_plan return, so the view hides what the game replaces and no more.

Needs the disc configured in the editor (Settings). Nothing is written.
Run: python tools/level_editor/test_sky_caps.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import editor  # noqa: E402

# stages with no dome / no cloud layer the game can replace (docs/guides/MODS.md, "Sky")
NO_DOME = {"BIGSHOT", "CANYON1", "CANYON2", "MINISF", "TOKYODM", "TRAINING"}
NO_CLOUDS = {"BIGSHOT", "CANYON2", "TOKYODM", "TRAINING", "UFO"}


def main():
    cfg = editor.load_config()
    disc = cfg["disc"]
    names = [s["name"] for s in editor.STAGES.list(disc)]
    assert len(names) >= 19, names
    for n in names:
        sky = editor.STAGES.sky(disc, n)
        assert set(sky) == {"dome", "clouds"}, (n, sky)
        assert bool(sky["dome"]) == (n not in NO_DOME), (n, "dome", sky["dome"])
        assert bool(sky["clouds"]) == (n not in NO_CLOUDS), (n, "clouds", sky["clouds"])
        assert all(a == a.lower() and int(a, 16) > 0 for a in sky["dome"] + sky["clouds"]), (n, sky)
        assert not set(sky["dome"]) & set(sky["clouds"]), (n, "a part is both the dome and the clouds")
    assert len(editor.STAGES.sky(disc, "UFO")["dome"]) == 7
    assert len(editor.STAGES.sky(disc, "CAPITOL")["clouds"]) == 1       # CAPITOL's other cloud layers are separate objects and stay
    print("ok")


if __name__ == "__main__":
    main()
