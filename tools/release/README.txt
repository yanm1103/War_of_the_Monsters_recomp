War of the Monsters Recompiled!
==============================

A native Windows port of War of the Monsters (PlayStation 2, 2003), built from the recompiled game code with a new PC renderer, sound and movie player.
This package contains no game data. You need your own copy of the game.

Requirements
------------
- Windows 10 or 11, 64-bit
- A graphics card with OpenGL 3.3
- An ISO of War of the Monsters NTSC-U (SCUS-97197). Other regions are not supported.
  The port checks the game executable SCUS_971.97 inside the ISO (sha1 1ed7f8457436a9170d8f85386e267369c7fcdd4f).
  A known good ISO dump has sha1 111a4702dcf9523ad79bdbf89fb30dc068c3f151.

Install
-------
1. Unzip
2. Open wotm.exe
3. Pick your ISO file.

Windows may show "Windows protected your PC" (the program is not signed):
click "More info", then "Run anyway".


Save location
-------------
Next to wotm.exe, or in %LOCALAPPDATA%\WotM if the game folder is read-only (Program Files):
- save.json and mc0\     save file
- settings.ini           the options of the "Recompiled options" panel
- screenshots\           photo mode pictures (F9 / L3+R3)
- wotm.log               log of the last session (wotm.prev.log: the one before)




Default controls
----------------
All keys and buttons can be remapped ingame in RECOMPILED OPTIONS > CONTROLS.

Gamepad layout:
bottom button = Cross
right button = Circle
left button = Square
top button = Triangle
bumpers = L1/R1
triggers = L2/R2

Default keyboard and mouse:
  WASD                move            Mouse              turn the camera
  Left click          light attack    Right click        heavy attack
  Space               jump            F or mouse 4       grab
  Shift               block           Q or middle click  lock-on
  Tab/R/mwheel        change target   Z/X                strafe
  1/2                 specials        Alt                hold to walk
  Arrows              D-pad           Ctrl               taunt
  Enter / Esc         pause

Photo mode: F9 or L3+R3.
F11 switches between window and fullscreen


Mods
----
Mods go in the mods\ folder. This package comes with four:
- Goliath Prime, Cerebulon, Mecha: playable custom monsters
- recompiled_logo: the title screen logo
How to make your own (textures as PNG, custom monsters):
https://github.com/yanm1103/War_of_the_Monsters_recomp/blob/master/docs/guides/MODS.md


Reporting a bug
---------------
Open an issue at https://github.com/yanm1103/War_of_the_Monsters_recomp/issues and attach wotm.log (from the folder in "Save location").


License
-------
The port's code is free software under the GNU GPL 3.0: source code at https://github.com/yanm1103/War_of_the_Monsters_recomp (this version: the tag or commit in the first line of wotm.log). Third-party components and their licenses are listed in
THIRD_PARTY.txt, with the texts in licenses\.

War of the Monsters is (c) 2003 Sony Computer Entertainment America. This project is not affiliated with or endorsed by Sony or Incognito Entertainment.
