"""The level editor's pedestrians on the server side (web/peds.js): the disc's pedestrian groups and net for /api/stage. The
patch's "peds" is applied and reported by tools/lvl.py (LevelPatch.apply_peds, ped_report) in editor.py's check."""
import lvl


def stage_peds(st, root):
    """{"groups": [{"handle", "pos", "count"}], "net": [{"pos", "links"}], "hasNet"} of a lvl.Stage; root holds PTS/<STAGE>.PED.
    No .PED: the game builds a hidden default net, and the editor treats the stage as having none (lvl.py apply_peds refuses
    new groups there)."""
    net = lvl.net_from_points(lvl.stage_points(root, st.name).get("PED", []))
    return {"groups": [{"handle": "0x%X" % g["node"], "pos": [round(x, 2) for x in g["pos"]], "count": g["count"]}
                       for g in lvl.ped_groups(st)],
            "net": [{"pos": [round(x, 2) for x in n["pos"]], "links": [list(l[:3]) for l in n["links"]]} for n in net],
            "hasNet": bool(net)}
