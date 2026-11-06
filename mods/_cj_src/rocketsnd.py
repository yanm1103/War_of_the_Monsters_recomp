"""CJ's special sounds (Robo-47's homing missile, ROBOT_S.BNK) from GTA SA's rocket launcher, as robot_s_sNNN.wav in the mod's
work folder; modkit pack builds files/ROBOT_S.BNK from the disc bank with them.
  python -I rocketsnd.py [out dir]   (default: ../_work/CJ/monsters/cj/files)

GTA SA (gta-reversed):
- launch: GENRL 143 (WEAPONS) sound 68, ROCKET_LAUNCH (CAEGlobalWeaponAudioEntity::ProjectileFire)
- flight: GENRL 138 (VEHICLE_GEN) sound 26, THRUST, played at speed 1.25 while the rocket exists
- explosion: GENRL 52 (EXPLOSIONS) sounds 4 NEAR, 3 MAIN, 2 LOWBEE and 1 DISTANT, played together
- aim: none (the heat-seeker locks on in silence), so sound 29 is a silent loop
- weapon pickup: GENRL 59 sound 27 (with 28, its right channel)
Robo-47 (ROBOT_S): 2 = special start (head opens), 29 = aim (sample 0: beeps speeding up; sample 1: the held lock tone, a
loop), 6 = launch, 7 = flight loop, 5 = explosion.
"""
import array
import math
import os
import sys
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sa_extract  # noqa: E402

FLIGHT_GAIN = 2.5   # thrust loop gain before the soft clip


def load(d, bank, idx):
    p = os.path.join(d, "%d_%03d.wav" % (bank, idx))
    if not os.path.exists(p):
        sa_extract.main(["GENRL", str(bank), d, str(idx)])
    w = wave.open(p)
    rate = w.getframerate()
    return rate, [float(x) for x in array.array("h", w.readframes(w.getnframes()))]


def resample(x, src, dst):
    if src == dst:
        return list(x)
    n = int(len(x) * dst / src)
    out = []
    for i in range(n):
        t = i * src / dst
        k = int(t)
        f = t - k
        a = x[k] if k < len(x) else 0.0
        b = x[k + 1] if k + 1 < len(x) else 0.0
        out.append(a + (b - a) * f)
    return out


def normalize(x, peak=30000.0):
    m = max(abs(v) for v in x) or 1.0
    return [v * peak / m for v in x]


def save(path, rate, x):
    w = wave.open(path, "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(int(rate))
    w.writeframes(array.array("h", [max(-32768, min(32767, int(round(v)))) for v in x]).tobytes())
    w.close()
    print("%-22s %6d Hz %.3f s" % (os.path.basename(path), rate, len(x) / rate))


def trim(x, rate, secs, fade):
    n, f = int(rate * secs), int(rate * fade)
    x = list(x[:n])
    for i in range(max(0, len(x) - f), len(x)):
        x[i] *= (len(x) - i) / float(f)
    return x


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "_work", "CJ", "monsters", "cj", "files")
    os.makedirs(out, exist_ok=True)
    tmp = os.path.join(tempfile.gettempdir(), "cj_gta_rocket")
    os.makedirs(tmp, exist_ok=True)
    # 2: special start = weapon pickup clack
    r, x = load(tmp, 59, 27)
    save(os.path.join(out, "robot_s_s002.wav"), r, x)
    # 6: launch
    r, x = load(tmp, 143, 68)
    save(os.path.join(out, "robot_s_s006.wav"), r, x)
    # 7: flight loop, 1.25x as GTA plays it (same samples, faster rate); louder, soft-clipped: the game fades the missile with
    # distance (350 to 1000) and GTA's thrust is a hiss that got lost there
    r, x = load(tmp, 138, 26)
    x = [32000.0 * math.tanh(FLIGHT_GAIN * v / 32000.0) for v in x]
    save(os.path.join(out, "robot_s_s007.wav"), r * 1.25, x)
    # 5: explosion, the four layers mixed
    rate = 27000
    mix = []
    for idx in (4, 3, 2, 1):
        r, x = load(tmp, 52, idx)
        x = resample(x, r, rate)
        if len(mix) < len(x):
            mix += [0.0] * (len(x) - len(mix))
        for i, v in enumerate(x):
            mix[i] += v
    save(os.path.join(out, "robot_s_s005.wav"), rate, normalize(mix))
    # 29: aim. GTA's heat-seeker locks on in silence (the owner checked in the game): a silent loop
    for k in (0, 1):   # the per-sample beeps of an earlier version
        p = os.path.join(out, "robot_s_s029_a%d.wav" % k)
        if os.path.exists(p):
            os.remove(p)
    save(os.path.join(out, "robot_s_s029.wav"), 22050, [0.0] * 2205)


if __name__ == "__main__":
    main()
