"""CJ's body sounds from GTA SA, as <bank>_sNNN.wav in the mod's work folder; modkit pack builds files/SHOGUN_S.BNK and
files/GENAMB_S.BNK from the disc banks with them.
  python -I bodysnd.py [out dir]   (default: <runtime>/discroot/mods/_work/CJ/monsters/cj/files, from the repo's .wotm_recomp_dir)

Ultra-V's bank (SHOGUN_S; Sound.cpp, type 0x80):
- 11 = footstep (playFootstepSound): PLAY_CYCLE over two samples (left and right foot), so _a0 and _a1 get one GTA step each
- 15 = body impact (playBodyImpactSound): running into a wall or a monster while dashing, thrown into things
- 4 = punch whoosh, played together with 13 (playSwing)
Shared bank (GENAMB_S, BANK_AMBIENT; the port swaps in the custom's copy around its sounds):
- 90 = dash loop (updateDashSound: Ultra-V, Robo-47 and Zorgulon): silent, GTA's sprint is only faster steps
GTA SA (gta-reversed):
- steps: FEET bank 0 (SND_BANK_FEET_GENERIC) sounds 1..5 on concrete (HandleFootstepEvent picks one at random)
- wall: GENRL 39 (COLLISIONS) 34 = CONCRETE, mixed with 29, the ped crunch
- whoosh: GENRL 143 (WEAPONS) 87 = WEAPON_SWIPE_FIST
GTA's sounds are much quieter than a giant monster's, so each one gets a gain into a soft clip (tanh), like the landing (14).
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sa_extract  # noqa: E402
from rocketsnd import load, resample, save  # noqa: E402

GTA = r"C:\Games\GTA San Andreas 1.0\audio\SFX"
STEPS = (1, 3)       # FEET 0 sounds for the two feet
STEP_GAIN = 2.8
WALL_GAIN = 1.6
SWING_GAIN = 1.3


def get(cache, pak, bank, idx):
    p = os.path.join(cache, "%d_%03d.wav" % (bank, idx))
    if not os.path.exists(p):
        sa_extract.main([os.path.join(GTA, pak), str(bank), cache, str(idx)])
    return load(cache, bank, idx)


def clip(x, gain):
    return [32000.0 * math.tanh(gain * v / 32000.0) for v in x]


def default_out():
    rel = ("_work", "CJ", "monsters", "cj", "files")
    saved = os.path.join(HERE, "..", "..", ".wotm_recomp_dir")   # repo/mods/_cj_src -> repo/.wotm_recomp_dir (build_runtime.bat)
    if os.path.exists(saved):
        runtime = open(saved, encoding="utf-8", errors="replace").read().strip()
        if os.path.isdir(os.path.join(runtime, "discroot", "mods")):
            return os.path.join(runtime, "discroot", "mods", *rel)
    return os.path.join(HERE, "..", *rel)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else default_out()
    cache = os.path.join(os.environ.get("TEMP", HERE), "cj_gta_body")
    os.makedirs(cache, exist_ok=True)
    whole = os.path.join(out, "shogun_s_s011.wav")   # o som inteiro venceria as amostras
    if os.path.exists(whole):
        os.remove(whole)
    for k, idx in enumerate(STEPS):
        rate, x = get(cache, "FEET", 0, idx)
        save(os.path.join(out, "shogun_s_s011_a%d.wav" % k), rate, clip(x, STEP_GAIN))
    rate, wall = get(cache, "GENRL", 39, 34)
    r2, crunch = get(cache, "GENRL", 39, 29)
    crunch = resample(crunch, r2, rate)
    n = max(len(wall), len(crunch))
    mix = [(wall[i] if i < len(wall) else 0.0) + 0.7 * (crunch[i] if i < len(crunch) else 0.0) for i in range(n)]
    save(os.path.join(out, "shogun_s_s015.wav"), rate, clip(mix, WALL_GAIN))
    rate, sw = get(cache, "GENRL", 143, 87)
    save(os.path.join(out, "shogun_s_s004.wav"), rate, clip(sw, SWING_GAIN))
    save(os.path.join(out, "genamb_s_s090.wav"), 16000, [0.0] * 8000)
    print("bodysnd: SHOGUN_S 11 (_a0/_a1), 15, 4; GENAMB_S 90 -> %s" % os.path.abspath(out))


if __name__ == "__main__":
    main()
