# Monster animation ids

Each monster `.NGP` keys its animations by an id (`head.id1`, 0..299). `Monster::initAfterDbLoad` fills `m_anims[id]`
(Monster +0x1CF0 + id×0x10) with `animationGetHandle(type, dupId, id)`, which takes the **first** animation with that id: a
second animation with the same id (Ultra-V's second 156 and 280, its extra id-0 ones) is never reached through a slot. A mod
that replaces a move replaces the animation with that id (`MODS.md`, "Own skeleton"; `tools/pose.py <NGP> --list` lists the
ids a monster has).

Sources: the decompiled code (`src/game/*.cpp`), the attack tables of the `.CHR` files (`tools/chr_dump.py`, key 0xFCC; swings
with a held object, key 0xFE3) and, for ids no code names, Ultra-V's animations seen with `build/monview.exe`.

Conventions:
- Monster axes (`cs`): x = side (right), y = forward, z = up. Hit directions are the push direction. "Face down/up" is the sign
  of the z of the forward axis (`m_cs->mat[1][2]` in `StateThrowBack`, Monster +0x3358 in `StateDeath`/`StateGetupAttack`);
  reading "≤ 0 = face down" is deduced (likely).
- Loop: `cycle` = the state repeats it (stride, idle); `loop` = `animationLoop(true)`; `one` = plays once; `hold` = plays and
  holds the pose while the state lasts; `layer` = an `AnimBlend` set by a percentage (does not run in time; the game adds it
  on top of the main animation); `add` = additive, once; `?` = no use in the code.
- Confidence: `certain` (the code says so), `likely` (code plus a deduction, e.g. the side of a hit), `guess` (visual only or
  nothing).
- Who has it: Con Congar (MONKEY), Rob Robo-47, Tog Togera (LIZARD), UV Ultra-V (SHOGUN), Pre Preytor (MANTIS), Kin
  Kineticlops (ENERGY), Mag Magmo (LAVA), Rap Raptros (DRAGON), Aga Agamo (ROCK), Zor Zorgulon (ALIEN), Gol Goliath Prime
  (ASSBOSS); costume 0 of each.

## Buttons and Ultra-V's attacks

`StatePunch` picks the attack type by the mapped action (`FORMATS.md`, "Character"): 5 = □ (quick), 0 = △ (strong),
3 = forward+□, 1 = forward+△, 4 = back+□, 2 = back+△. The position goes up with each chained attack; the type can change
inside the window. Other commands: ○ grabs/throws (actions 0xA/0xB), ✕ jumps, block and counter on the block button,
○+△ with the meter full = special (160-162), □+✕ with the meter = crowd control (163-165, in the air 192/193).

Ultra-V (`python tools/chr_dump.py disc/RST/SHOGUN.CHR`), animation per combo position:

| type | 1st | 2nd | 3rd | 4th | 5th | 6th |
|---|---|---|---|---|---|---|
| □ | 10 right jab | 11 left jab | 10 | 11 | 13 right kick | 15 left kick (knockdown) |
| △ | 14 left kick | 13 right kick | 15 (knockdown) | 14 | 17 two-foot kick (knockdown) | 10 |
| forward+□ | 11 | 11 | 13 | 11 | 11 | 17 |
| forward+△ | 16 heavy punch (stuns) | 16 | 16 | 16 | 16 | 10 |
| back+□ | 18 low kick (pushes back) | 18 | 18 | 18 | 18 | 10 |
| back+△ | 17 | 12 right kick (knockdown) | 15 | 17 | 15 | 17 |

Swings with an object in hand (`StateBatSwipe`): 220-226 and 228 (key 0xFE3 of `SHOGUN.CHR`).

Ultra-V's sword (crowd control) is a set of rigid parts on the right hand that only some animations scale up (`sc_a017b0`,
`sc_a010b0` in the kit export): 131, 163-165, 192, 195 and 206.

## Table

| id | hex | what it is | evidence | loop | Ultra-V (frames) | who has it | confidence |
|---|---|---|---|---|---|---|---|
| 0 | 0x00 | no animation: several anims carry id 0, the code uses slot 0 as 'none' | StateRun::init (anim = 0), pose.py | - | 0-256; 0-20; 0-60; 60-80; 80-140 | all | likely |
| 1 | 0x01 | walk forward (speed < walkMax 250); also what plays when only turning in place | StateRun::transitionInto/update/enterSubState | cycle | 0-40 | all but Zor,Gol | certain |
| 2 | 0x02 | run forward (required to run at all) | StateRun::transitionOK, enterSubState | cycle | 0-40 | all | certain |
| 3 | 0x03 | not referenced by code; Ultra-V: fists-up stance | visual | ? | 0-40 | Con,Rob,UV,Pre,Kin,Aga,Gol | guess |
| 5 | 0x05 | run direction layer: scrubbed by the heading of the motion (pct = heading/pi + 0.5, 0.5 = straight ahead); leans the run into strafes | StateRun::init/transitionInto/update (blend) | layer | 0-80 | all but Gol | certain (layer); direction mapping likely |
| 6 | 0x06 | landing run forward (after a jump with landing momentum >= 350) | StateRun::transitionInto | cycle | 0-40 | all but Gol | certain |
| 7 | 0x07 | walk backward (stick back; pair of 1, mirrored percent) | StateRun altAnim, SUB_2 = stick back | cycle | 0-40 | all but Zor,Gol | likely |
| 8 | 0x08 | run backward / backpedal (pair of 2) | StateRun altAnim | cycle | 0-40 | all | likely |
| 9 | 0x09 | landing run backward (pair of 6) | StateRun altAnim | cycle | 0-40 | all but Gol | likely |
| 10 | 0x0A | attack slot (CHR 0xFCC). Required by StatePunch::transitionOK. Ultra-V: right jab (Sq 1st/3rd) | StatePunch::transitionOK; chr_dump | one | 0-20 | all | certain |
| 11 | 0x0B | attack slot. Ultra-V: left jab (Sq 2nd/4th, Fwd+Sq) | chr_dump | one | 0-20 | all | certain |
| 12 | 0x0C | attack slot. Ultra-V: right kick, knockdown (Back+Tri 2nd) | chr_dump | one | 0-40 | all | certain |
| 13 | 0x0D | attack slot. Ultra-V: right kick (Sq 5th, Tri 2nd) | chr_dump | one | 0-50 | all but Zor,Gol | certain |
| 14 | 0x0E | attack slot. Ultra-V: left kick (Tri 1st/4th) | chr_dump | one | 0-50 | all but Zor,Gol | certain |
| 15 | 0x0F | attack slot. Ultra-V: left kick, knockdown (Tri 3rd, Sq 6th) | chr_dump | one | 0-60 | all but Gol | certain |
| 16 | 0x10 | attack slot. Ultra-V: heavy right punch that stuns (Fwd+Tri, stun 120) | chr_dump | one | 0-70 | all but Gol | certain |
| 17 | 0x11 | attack slot. Ultra-V: two-foot kick, knockdown (Back+Tri 1st, Tri 5th) | chr_dump | one | 0-50 | all but Gol | certain |
| 18 | 0x12 | attack slot. Ultra-V: right low kick/sweep with knockback (Back+Sq) | chr_dump | one | 0-50 | all but Gol | certain |
| 19 | 0x13 | attack slot (other monsters) | chr_dump | one | - | all but UV,Gol | certain |
| 20 | 0x14 | attack slot (other monsters) | chr_dump | one | - | Con,Rob,Tog,Pre,Kin,Mag,Aga,Zor | certain |
| 21 | 0x15 | attack slot (other monsters) | chr_dump | one | - | Con,Rob,Tog,Kin,Mag,Aga,Zor | certain |
| 22 | 0x16 | attack slot (other monsters) | chr_dump | one | - | Rob,Tog,Kin,Mag,Zor | certain |
| 23 | 0x17 | attack slot (other monsters) | chr_dump | one | - | Kin,Zor | certain |
| 26 | 0x1A | attack slot in FINAL.CHR; Raptros has a 120-frame anim here that its table does not use | chr_dump | ? | - | Rap | guess |
| 30 | 0x1E | jump crouch/take-off (stretched to the config's frames; required to jump) | StateJump/StateFly::enterSubState SUB_1, transitionOK | one | 0-20 | all | certain |
| 31 | 0x1F | jump take-off from a run, foot 0 (stride inside config window) | StateJump/StateFly::enterSubState SUB_1, StateRun::update footPref | one | 0-20 | all but Pre,Gol | certain (which foot: guess) |
| 32 | 0x20 | jump take-off from a run, foot 1 | same | one | 0-20 | all but Pre,Gol | certain (which foot: guess) |
| 33 | 0x21 | rising (weight follows vertical speed) | StateJump::enterSubState SUB_3 (blend9C) | layer | 0-40 | all | certain |
| 34 | 0x22 | falling (after the apex) | StateJump::enterSubState SUB_6 | one | 0-40 | all | certain |
| 35 | 0x23 | landing impact layer (percent pushed by landing momentum) | StateJump SUB_8/SUB_9 (blend104) | layer | 0-30 | all | certain |
| 36 | 0x24 | air lean layer: turn rate | StateJump::init/update lean[0] | layer | 0-40 | all but Pre | likely |
| 37 | 0x25 | air lean layer: velocity (one horizontal axis) | StateJump::update lean[1] | layer | 0-40 | all but Pre,Rap | likely |
| 38 | 0x26 | air lean layer: velocity (other horizontal axis) | StateJump::update lean[2] | layer | 0-40 | all but Pre,Rap | likely |
| 39 | 0x27 | landing recovery (stands up after the landing, then Idle) | StateJump::update SUB_9 | one | 0-30 | all | certain |
| 41 | 0x29 | pick up an object from the ground | StatePickUp::transitionFeasible/transitionInto | one | 0-20 | all but Gol | certain |
| 42 | 0x2A | pick up a two-handed (large) object | StatePickUp::transitionInto | one | 0-40 | all but Gol | certain |
| 48 | 0x30 | pick up an object high up (> 40 above the feet; falls back to 49) | StatePickUp::transitionInto | one | - | none | certain |
| 49 | 0x31 | catch an object thrown at you (also the high pickup fallback) | StateCatch, StatePickUp | one | 10-30 | all but Gol | certain |
| 50 | 0x32 | carry pose of a one-handed object (2-frame blend, ramped out on the throw) | StateThrow::transitionInto | layer | - | Con,Rob,Pre,Kin,Mag,Rap,Aga | likely |
| 52 | 0x34 | throw a one-handed object | StateThrow | one | 0-20 | all but Gol | certain |
| 54 | 0x36 | javelin throw (pickup type 7) | StateJavelin | one | 0-20 | all but Gol | certain |
| 55 | 0x37 | throw the held monster / two-handed object at the reticle target (action 0xA) | StateGrappleThrow, StateTwoHandedThrow | one | 0-30 | all | certain |
| 56 | 0x38 | throw the held monster / two-handed object straight ahead (action 0xB) | StateGrappleThrow1, StateTwoHandedThrow | one | 0-30 | all but Gol | certain |
| 58 | 0x3A | not referenced by code | - | ? | 0-40 | Con,UV,Pre,Kin | guess |
| 59 | 0x3B | not referenced by code | - | ? | 0-40 | Con,UV,Pre,Kin | guess |
| 60 | 0x3C | block, bare hands (required to block) | StateBlock::transitionOK/chooseBlock | hold | 0-40 | all | certain |
| 61 | 0x3D | block holding a one-handed object | StateBlock::chooseBlock | hold | 0-40 | all but Gol | certain |
| 62 | 0x3E | block holding a two-handed object or a monster | StateBlock::chooseBlock | hold | 0-40 | Con,Rob,Tog,UV,Mag,Rap,Aga,Zor | certain |
| 63 | 0x3F | block holding pickup type 0x10 | StateBlock::chooseBlock | hold | 0-80 | all but Gol | certain |
| 64 | 0x40 | counter (block-button counterattack) | StateCounter | one | 0-30 | all but Gol | certain |
| 65 | 0x41 | blocked hit while holding pickup type 0x10 | StateRecoil::getRecoilAnim | one | 0-12 | all but Gol | certain |
| 66 | 0x42 | blocked hit while holding a two-handed object, pickup type 6 or a monster | StateRecoil::getRecoilAnim | one | 0-30 | Con,UV,Pre,Mag,Rap,Aga,Zor | certain |
| 67 | 0x43 | hit reaction, side: pushed along +x (hit on the left side) | StateRecoil::getRecoilAnim (dot with cs x) | one | 0-20 | all | certain (side: likely) |
| 68 | 0x44 | hit reaction, side: pushed along -x (hit on the right side) | same | one | 0-20 | all | certain (side: likely) |
| 69 | 0x45 | hit reaction from behind (pushed forward) | same (dot with cs y >= 0) | one | 0-20 | all | likely |
| 70 | 0x46 | hit reaction from the front (pushed back); also the default recoil | same | one | 0-20 | all | certain (default); direction likely |
| 71 | 0x47 | blocked hit (normal) | StateRecoil::getRecoilAnim | one | 0-20 | all | certain |
| 74 | 0x4A | shocked (electrocuted) | StateShocked | loop | 0-40 | all but Gol | certain |
| 75 | 0x4B | big take-hit of type 0x1A0 (Goliath); no monster has it | StateBigTakeHit | one | - | none | certain |
| 76 | 0x4C | countered (stagger after the other side's counter) | StateCountered | one | 0-60 | all but Gol | certain |
| 77 | 0x4D | blocked big hit (pushed back while blocking) | StateRecoil::getRecoilAnim, StateBlock::transitionInto | one | 0-40 | all but Pre,Gol | certain |
| 78 | 0x4E | Preytor only; not referenced | - | ? | - | Pre | guess |
| 79 | 0x4F | Preytor only; not referenced | - | ? | - | Pre | guess |
| 80 | 0x50 | lying on the ground face up (after a knockdown) | StateThrowBack::enterSubState SUB_LYING | hold | 0-12 | all but Gol | certain (face: likely) |
| 81 | 0x51 | lying on the ground face down | same | hold | 0-12 | all but Gol | certain (face: likely) |
| 82 | 0x52 | air recovery: counter while flailing after a knockback/throw | StateThrowBack SUB_COUNTER | one | 0-20 | all but Gol | certain |
| 83 | 0x53 | not referenced; Ultra-V: quick get-up from the ground | visual | ? | 0-20 | Con,Rob,UV,Pre,Kin,Mag,Rap,Aga | guess |
| 84 | 0x54 | not referenced; Ultra-V: quick get-up from a fall | visual | ? | 0-20 | Con,Rob,UV,Pre,Kin,Mag,Rap,Aga | guess |
| 85 | 0x55 | knockdown landing pose, face down (2 frames) | StateThrowBack SUB_LANDED | one | 0-2 | all but Gol | certain (face: likely) |
| 86 | 0x56 | knockdown landing pose, face up (2 frames) | same | one | 0-2 | all but Gol | certain (face: likely) |
| 87 | 0x57 | get up from face down (required for knockdowns) | StateThrowBack SUB_GETUP, transitionOK | one | 0-60 | all | certain (face: likely) |
| 88 | 0x58 | get up from face up (required for knockdowns) | same | one | 0-60 | all | certain (face: likely) |
| 89 | 0x59 | not referenced by code | - | ? | 0-40 | Con,UV,Pre,Kin,Rap | guess |
| 90 | 0x5A | base idle | StateIdle::chooseBaseIdle, Monster::startCinema, AnimPappy/BoneSpring init | cycle | 0-420 | all | certain |
| 92 | 0x5C | taunt; also the rage trigger (95%) and the victory fallback | StateTaunt, Monster::updateBoostAndRage, StateVictory | one | 0-150 | all | certain |
| 93 | 0x5D | Goliath's second taunt (AI state 8) | StateTaunt | one | - | Gol | certain |
| 95 | 0x5F | idle with low health | StateIdle::chooseBaseIdle (m_health below the state's threshold) | cycle | 0-140 | all but Gol | certain |
| 96 | 0x60 | idle with the special meter full | StateIdle::chooseBaseIdle (isSpecialAvailable) | cycle | 0-40 | all but Gol | certain |
| 97 | 0x61 | fancy idle (random); no monster has it | StateIdle::chooseFancyIdle | one | - | none | certain |
| 98 | 0x62 | fancy idle (random); no monster has it | same | one | - | none | certain |
| 99 | 0x63 | fancy idle (random); no monster has it | same | one | - | none | certain |
| 100 | 0x64 | climb: latch onto the wall | StateClimb::enterSubState SUB_CLIMB | one | 0-20 | all but Gol | certain |
| 101 | 0x65 | climb up (played backwards to climb down when 102 is missing) | StateClimb::update | cycle | 0-30 | all but Gol | certain |
| 102 | 0x66 | climb down | StateClimb::update | cycle | - | none | certain |
| 103 | 0x67 | climb sideways, motion x < 0 (left; backwards for right when 104 is missing) | StateClimb::update | cycle | 0-30 | all but Gol | certain (side: likely) |
| 104 | 0x68 | climb sideways, motion x > 0 (right) | StateClimb::update | cycle | - | Con,Tog,Pre,Kin,Rap,Aga | certain (side: likely) |
| 105 | 0x69 | climb over the top ledge (backwards = climb down onto the wall) | StateClimb::enterSubState SUB_MOUNT/SUB_DISMOUNT | one | 0-20 | all but Gol | certain |
| 106 | 0x6A | hang at the top of the wall | StateClimb::enterSubState SUB_HANG | cycle | 0-80 | all but Gol | certain |
| 110 | 0x6E | glide (flyers) | StateFly SUB_7 | cycle | - | Pre,Rap | certain |
| 111 | 0x6F | glide additive layer (held at 50%) | StateFly::transitionInto | layer | - | Pre,Rap | certain |
| 113 | 0x71 | flap (flyers) | StateFly SUB_4 | loop | - | Pre,Rap | certain |
| 114 | 0x72 | flap with the stick back | StateFly SUB_4 | loop | - | Pre,Rap | certain |
| 120 | 0x78 | death (standing) | StateDeath::transitionInto | one | 0-210 | all | certain |
| 121 | 0x79 | death while lying face down | StateDeath::transitionInto (reaction state, Monster+0x3358 < 0) | one | 0-120 | all but Gol | likely |
| 122 | 0x7A | death while lying face up | same | one | 0-120 | all but Gol | likely |
| 130 | 0x82 | victory 1 (random of 130..132) | StateVictory::transitionInto | one | 0-200 | all but Gol | certain |
| 131 | 0x83 | victory 2 | same | one | 0-180 | all but Gol | certain |
| 132 | 0x84 | victory 3 | same | one | 0-180 | all but Gol | certain |
| 140 | 0x8C | additive recoil when firing the default ranged projectile | Monster::launchDefaultProjectile | add | 0-12 | all but Tog,Gol | certain |
| 142 | 0x8E | stunned/dizzy (stars); also stuck on an impaler | StateStunned, StateImpaled | loop | 0-60 | all but Gol | certain |
| 143 | 0x8F | required by StateImpaled but not played | StateImpaled::transitionOK | ? | 0-40 | Con,Rob,UV,Pre,Kin,Mag,Rap,Aga | certain |
| 144 | 0x90 | pull the impaler out | StateImpaled::update | one | 0-30 | all but Gol | certain |
| 145 | 0x91 | not referenced (Kineticlops, Agamo, Goliath) | - | ? | - | Kin,Aga,Gol | guess |
| 150 | 0x96 | additive flinch: pushed along +x | Monster::takeAdditiveRecoil | add | 0-12 | all but Gol | certain (side: likely) |
| 151 | 0x97 | additive flinch: pushed along -x | same | add | 0-12 | all but Gol | certain (side: likely) |
| 152 | 0x98 | additive flinch: pushed forward (hit from behind) | same | add | 0-12 | all but Gol | likely |
| 153 | 0x99 | additive flinch: pushed back (hit from the front) | same | add | 0-12 | all but Gol | likely |
| 154 | 0x9A | head look-at layer (first axis, yaw) | Monster::initAfterDbLoad, updateLookAt | layer | 0-40 | all but Con,Gol | likely |
| 155 | 0x9B | head look-at layer (second axis, pitch) | same | layer | 0-40 | all but Con,Gol | likely |
| 156 | 0x9C | air leg override (legs when off the ground outside the jump states) | Monster::updateAirLegOverride | layer | 0-2; 0-40 | all but Zor,Gol | certain |
| 160 | 0xA0 | special (circle+triangle with the meter): windup. Ultra-V: throws the grapple-hook chain | SpecialStates (all), move_list_especiais | one | 0-80 | all | certain |
| 161 | 0xA1 | special: active loop | same | loop | 70-106 | all | certain |
| 162 | 0xA2 | special: winddown (Ultra-V pulls the victim in) | same | one | 106-140 | all | certain |
| 163 | 0xA3 | crowd control (square+X with the meter): windup. Ultra-V draws the sword | StateCrowdControl | one | 0-40 | all | certain |
| 164 | 0xA4 | crowd control: loop on the ground (shock wave at 60%). Ultra-V spins with the sword | StateCrowdControl | loop | 40-110 | all | certain |
| 165 | 0xA5 | crowd control: winddown. Ultra-V puts the sword away | StateCrowdControl | one | 110-130 | all | certain |
| 166 | 0xA6 | special aim layer / aim pose | Robot/CannonHands enterSubState, LavaBlast/SonicRoar init | layer | - | Con,Rob,Tog,Mag,Rap,Gol | likely |
| 167 | 0xA7 | special aim layer (second axis) | LavaBlast/SonicRoar/CannonHands/RobotSpecial init | layer | - | Con,Rob,Mag,Gol | likely |
| 170 | 0xAA | grab: reach for the victim | StateGrapple | one | 0-30 | all | certain |
| 171 | 0xAB | lift the grabbed victim overhead | StateGrappleLift | one | 30-60 | all | certain |
| 172 | 0xAC | idle holding a monster or a two-handed object | StateIdle::chooseBaseIdle | cycle | 0-80 | all | certain |
| 173 | 0xAD | walk/run carrying (forward) | StateRun (heavy config) | cycle | 0-40 | all but Gol | certain |
| 174 | 0xAE | victim: held by the grabber | StateGrappled | hold | 0-40 | all but Gol | certain |
| 176 | 0xB0 | walk/run carrying (backward) | StateRun altAnim (heavy) | cycle | 0-40 | all but Gol | likely |
| 177 | 0xB1 | hit the victim held overhead | StateGrappleOHAttack/StateMonkeyOHAttack | one | 0-30 | all | certain |
| 178 | 0xB2 | second overhead hit (Congar) | StateMonkeyOHAttack | one | - | Con,Pre | certain |
| 180 | 0xB4 | heavy jump crouch (carrying) | StateJump/StateFly SUB_1 | one | 0-20 | all but Gol | certain |
| 181 | 0xB5 | heavy jump rising | StateJump SUB_3 | layer | 0-40 | all but Gol | certain |
| 182 | 0xB6 | heavy falling layer | StateJump SUB_6 | layer | 0-30 | all but Gol | certain |
| 183 | 0xB7 | heavy landing recovery | StateJump::update SUB_9 | one | 30-60 | all but Gol | certain |
| 184 | 0xB8 | not referenced; Ultra-V: squat, hop and land in place | visual | ? | 0-30 | UV,Pre,Aga | guess |
| 185 | 0xB9 | grab miss | StateGrapple::enterSubState | one | 0-20 | all | certain |
| 186 | 0xBA | overhead finisher (special: knocks the victim out of the hold) | StateGrappleOHAttack | one | 0-40 | all but Gol | certain |
| 189 | 0xBD | not referenced (Preytor, Kineticlops) | - | ? | - | Pre,Kin | guess |
| 191 | 0xBF | slot filled from another hierarchy (id1 = type + 5, anim 0), not a body animation | Monster::initAfterDbLoad | - | - | none | certain |
| 192 | 0xC0 | crowd control in the air (after the windup) | StateCrowdControl::update | one | 0-60 | all but Gol | certain |
| 193 | 0xC1 | crowd control started in the air (preferred over 192) | StateCrowdControl::transitionInto | one | 0-10 | all but Gol | certain |
| 194 | 0xC2 | not referenced by code; long (Ultra-V 490 frames): cinema | - | ? | 0-490 | all but Zor,Gol | guess |
| 195 | 0xC3 | not referenced by code; long (Ultra-V 230 frames, sword): cinema | - | ? | 0-230 | Con,Rob,UV,Pre,Kin,Rap | guess |
| 196 | 0xC4 | not referenced by code; long: cinema | - | ? | - | Con,Rob,Pre,Kin | guess |
| 197 | 0xC5 | not referenced by code; cinema | - | ? | - | Con,Pre,Kin | guess |
| 198 | 0xC6 | not referenced by code; cinema (Preytor) | - | ? | - | Pre | guess |
| 200 | 0xC8 | not referenced (Congar, Raptros, Agamo): cinema | - | ? | - | Con,Rap,Aga | guess |
| 201 | 0xC9 | not referenced (Congar, Raptros): cinema | - | ? | - | Con,Rap | guess |
| 202 | 0xCA | not referenced (Congar, Raptros): cinema | - | ? | - | Con,Rap | guess |
| 203 | 0xCB | not referenced (Raptros): cinema | - | ? | - | Rap | guess |
| 204 | 0xCC | not referenced (Congar, Raptros, Agamo): cinema | - | ? | - | Con,Rap,Aga | guess |
| 205 | 0xCD | not referenced (Congar): cinema | - | ? | - | Con | guess |
| 206 | 0xCE | cinema animation with its own sound (play206Sound) | MonsterSound::initMonsterSound/updateMonsterCinemaSound | one | 9800-10040 | all but Mag,Gol | certain |
| 207 | 0xCF | cinema roar (signature sound) | same | one | 0-60 | all but Gol | certain |
| 208 | 0xD0 | cinema roar 2 (signature sound) | same | one | 0-90 | all but Gol | certain |
| 209 | 0xD1 | victim: held with a struggle meter | StateGrappled SUB_1 | loop | - | Con,Pre,Kin,Aga | certain |
| 210 | 0xD2 | victim: held overhead while being hit | StateGrappled SUB_2 | loop | 0-20 | all but Gol | likely |
| 211 | 0xD3 | Robo-47 only; not referenced | - | ? | - | Rob | guess |
| 220 | 0xDC | swing with a held object (StateBatSwipe combo slot, from the .CHR swipe keys 0xFA4/0xFAC/0xFD7/0xFE3) | chr swipe keys | one | 0-30 | all but Gol | certain |
| 221 | 0xDD | swing with a held object (StateBatSwipe combo slot, from the .CHR swipe keys 0xFA4/0xFAC/0xFD7/0xFE3) | chr swipe keys | one | 0-40 | all but Gol | certain |
| 222 | 0xDE | swing with a held object (StateBatSwipe combo slot, from the .CHR swipe keys 0xFA4/0xFAC/0xFD7/0xFE3) | chr swipe keys | one | 0-50 | all but Gol | certain |
| 223 | 0xDF | swing with a held object (StateBatSwipe combo slot, from the .CHR swipe keys 0xFA4/0xFAC/0xFD7/0xFE3) | chr swipe keys | one | 0-30 | all but Gol | certain |
| 224 | 0xE0 | swing with a held object (StateBatSwipe combo slot, from the .CHR swipe keys 0xFA4/0xFAC/0xFD7/0xFE3) | chr swipe keys | one | 0-40 | all but Gol | certain |
| 225 | 0xE1 | swing with a held object (StateBatSwipe combo slot, from the .CHR swipe keys 0xFA4/0xFAC/0xFD7/0xFE3) | chr swipe keys | one | 40-110 | all but Gol | certain |
| 226 | 0xE2 | swing with a held object (bat swipe slot); also stomp with an object, 1st | chr swipe keys, StateStompAttack::startChain | one | 0-60 | all but Gol | certain |
| 227 | 0xE3 | swing with a held object (StateBatSwipe combo slot, from the .CHR swipe keys 0xFA4/0xFAC/0xFD7/0xFE3) | chr swipe keys | one | 0-30 | Con,Rob,UV,Pre,Kin,Mag,Rap,Aga | certain |
| 228 | 0xE4 | swing with a held object (StateBatSwipe combo slot, from the .CHR swipe keys 0xFA4/0xFAC/0xFD7/0xFE3) | chr swipe keys | one | 0-30 | all but Gol | certain |
| 229 | 0xE5 | swing with a held object (StateBatSwipe combo slot, from the .CHR swipe keys 0xFA4/0xFAC/0xFD7/0xFE3) | chr swipe keys | one | - | Con,Tog,Pre,Kin,Rap,Aga | certain |
| 230 | 0xE6 | swing with a held object (StateBatSwipe combo slot, from the .CHR swipe keys 0xFA4/0xFAC/0xFD7/0xFE3) | chr swipe keys | one | - | Tog | certain |
| 231 | 0xE7 | swing with a held object (StateBatSwipe combo slot, from the .CHR swipe keys 0xFA4/0xFAC/0xFD7/0xFE3) | chr swipe keys | one | - | Tog | certain |
| 239 | 0xEF | stomp a downed enemy with an object, 2nd (alternates with 226) | StateStompAttack::startChain | one | 0-60 | all but Gol | certain |
| 240 | 0xF0 | stomp a downed enemy, 1st foot (required) | StateStompAttack | one | 0-30 | all but Gol | certain |
| 241 | 0xF1 | stomp a downed enemy, other foot | StateStompAttack::startChain | one | 0-30 | all but Gol | certain |
| 242 | 0xF2 | get-up attack from face down | StateGetupAttack | one | 0-60 | all but Gol | certain (face: likely) |
| 243 | 0xF3 | get-up attack from face up | StateGetupAttack | one | 0-60 | all but Gol | certain (face: likely) |
| 244 | 0xF4 | fire a held weapon pickup (gun/tazer) | StateGunAttack, StateTazerAttack | one | 0-30 | all but Gol | certain |
| 245 | 0xF5 | bash with pickup type 0x10, 1st | StateShieldAttack | one | 0-20 | all but Gol | certain |
| 246 | 0xF6 | bash with pickup type 0x10, 2nd (alternates) | StateShieldAttack::update | one | 0-20 | all but Gol | certain |
| 250 | 0xFA | flail in the air (knocked back, thrown, slammed) | StateThrowBack SUB_FLAIL | cycle | 0-40 | all | certain |
| 251 | 0xFB | flail bend layer: upper body y | StateThrowBack::init adds[1] | layer | 0-40 | all | certain |
| 252 | 0xFC | flail bend layer: upper body x | adds[0] | layer | 0-40 | all | certain |
| 253 | 0xFD | flail bend layer: upper body z | adds[2] | layer | 0-40 | all | certain |
| 254 | 0xFE | flail bend layer: lower body y | adds[4] | layer | 0-40 | all | certain |
| 255 | 0xFF | flail bend layer: lower body x | adds[3] | layer | 0-40 | all | certain |
| 256 | 0x100 | flail bend layer: lower body z | adds[5] | layer | 0-40 | all | certain |
| 261 | 0x105 | butt slam (attack in the air): jump into the slam | StateButtSlam | one | 0-40 | all | certain |
| 262 | 0x106 | butt slam: the drop | StateButtSlam | one | 40-80 | all | certain |
| 263 | 0x107 | not referenced | - | ? | - | Pre,Kin,Mag,Rap,Aga | guess |
| 264 | 0x108 | dash start | StateDash SUB_START | one | 0-40 | all but Gol | certain |
| 265 | 0x109 | dash | StateDash SUB_DASH | cycle | 0-40 | all but Gol | certain |
| 266 | 0x10A | ram attack: charge | StateRamAttack SUB_CHARGE | one | 0-40 | all but Con,Zor | certain |
| 267 | 0x10B | ram attack: impact (required) | StateRamAttack SUB_IMPACT, transitionOK | one | 0-80 | all | certain |
| 268 | 0x10C | not referenced; Ultra-V: flip in the air | visual | ? | 0-20 | Con,Rob,Tog,UV,Kin,Mag,Rap,Aga | guess |
| 269 | 0x10D | not referenced; Ultra-V: flip and land | visual | ? | 20-40 | Con,Rob,Tog,UV,Kin,Mag,Rap,Aga | guess |
| 270 | 0x10E | not referenced (Kineticlops, Raptros, 2 frames) | - | ? | - | Kin,Rap | guess |
| 271 | 0x10F | impaled: struggle (random of 271/272) | StateImpaled::update | one | 0-20 | all but Gol | certain |
| 272 | 0x110 | impaled: struggle | StateImpaled::update | one | 0-20 | all but Gol | certain |
| 280 | 0x118 | victim: struggle in the hold (random of 280..284; 280 required) | StateGrappled::update/transitionOK | one | 0-20; 0-4 | all but Gol | certain |
| 281 | 0x119 | victim: struggle | StateGrappled::update | one | 0-4 | all but Gol | certain |
| 282 | 0x11A | victim: struggle | StateGrappled::update | one | 0-4 | all but Gol | certain |
| 283 | 0x11B | victim: struggle | StateGrappled::update | one | 0-6 | all but Gol | certain |
| 284 | 0x11C | victim: struggle (no monster has it) | StateGrappled::update | one | - | none | certain |
