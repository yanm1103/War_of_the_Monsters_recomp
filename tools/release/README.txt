War of the Monsters Recompiled
==============================

A native Windows port of War of the Monsters (PlayStation 2, 2003), built from the
recompiled game code with a new PC renderer, sound and movie player.

This package contains no game data. You need your own copy of the game:
the North American release (NTSC-U, SCUS-97197) as a disc image (.iso).


Requirements
------------
- Windows 10 or 11, 64-bit
- A graphics card with OpenGL 3.3
- Your own War of the Monsters NTSC-U disc image (.iso)


First run
---------
1. Unzip this folder anywhere you like (for example, Documents\WotM).
2. Open wotm.exe.
3. The game asks for your disc image: pick your .iso file.
   It checks that it is the NTSC-U release and copies the game program
   (SCUS_971.97) from it. The rest is read from the .iso every time you play,
   so keep it where it is. If you move it, the game asks for it again.

Windows may show "Windows protected your PC" (the program is not signed):
click "More info", then "Run anyway".


Where things are saved
----------------------
Next to wotm.exe, or in %LOCALAPPDATA%\WotM if the game folder is read-only
(for example under Program Files):
- save.json and mc0\     your progress (the game saves on its own)
- settings.ini           the options of the "Recompiled options" panel
- screenshots\           photo mode pictures
- wotm.log               the log of the last session (wotm.prev.log: the one before)


Options
-------
Main menu > RECOMPILED OPTIONS (also in the pause menu): video (resolution,
fullscreen, refresh rate, anti-aliasing, texture filter), features (lock-on
toggle, enemy health bars, damage numbers, narrator), camera (modern or classic)
and controls (key and button mapping, button icons, vibration).
F11 switches between window and fullscreen at any time.


Controls
--------
Gamepad: the PlayStation layout on your controller (bottom button = Cross,
right = Circle, left = Square, top = Triangle; LB/RB = L1/R1, LT/RT = L2/R2).
Up to 4 gamepads (XInput) for 2 to 4 players.

Keyboard and mouse (default):
  WASD                move            Mouse              turn the camera
  Left click          light attack    Right click        heavy attack
  Space               jump            F or side button   grab
  Shift               block           Q or middle click  lock on
  Tab / R / wheel     change target   Z / X              strafe
  1 / 2               specials        Alt                walk
  Arrows              D-pad           Ctrl               taunt
  Enter / Esc         pause

Photo mode (in a fight): F9 or L3+R3.
All keys and buttons can be changed in RECOMPILED OPTIONS > CONTROLS.


Mods
----
Mods go in the mods\ folder. This package comes with four:
- Goliath Prime, Cerebulon and Jelly: playable custom monsters
- Mecha: the robot of the final boss, playable
- recompiled_logo: the title screen logo
How to make your own (textures as PNG, custom monsters):
https://github.com/yanm1103/War_of_the_Monsters_recomp/blob/master/docs/passos/MODS.md


Reporting a bug
---------------
Open an issue at https://github.com/yanm1103/War_of_the_Monsters_recomp/issues and attach wotm.log
(from the folder in "Where things are saved"). Its first line says which version
you have. If the game crashed, the log already says where; wotm.pdb (included)
lets anyone read it.


License
-------
The port's code is free software under the GNU GPL 3.0: source code at
https://github.com/yanm1103/War_of_the_Monsters_recomp (this version: the tag or commit in the first
line of wotm.log). Third-party components and their licenses are listed in
THIRD_PARTY.txt, with the texts in licenses\.

War of the Monsters is (c) 2003 Sony Computer Entertainment America. This project
is not affiliated with or endorsed by Sony or Incognito Entertainment.
