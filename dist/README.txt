========================================================
  dota2PyScriptPlus  v1.0.0
  coded by Mamad
========================================================

WHAT IS THIS
------------
A Windows tool that changes Dota 2 cosmetics (hero skins,
items, wards, terrain, HUD) and camera distance.
It does NOT inject into the game, does NOT touch game
memory, and does NOT run while Dota 2 is closed.

You run it, you pick what you want, you press APPLY,
then you start Dota 2. Nothing else.


REQUIREMENTS
------------
- Windows 10 or 11, 64-bit
- Dota 2 installed through Steam
- No installer, no Visual C++ Redistributable, no .NET.
  Everything is inside this one .exe.


HOW TO RUN
----------
1. Unzip this folder anywhere. For example:
       C:\dota2PyScriptPlus\

2. Double-click  dota2PyScriptPlus.exe

   If Windows shows "Windows protected your PC":
       click "More info"  ->  "Run anyway"
   That is normal for any unsigned exe. The file is not
   signed because code-signing certificates cost money.

3. If the app says it cannot find Dota 2, or APPLY fails
   with a permission error, right-click the exe and choose
   "Run as administrator". This is only needed when Dota 2
   is installed inside C:\Program Files.


FIRST RUN - IT MAY TAKE A FEW SECONDS
-------------------------------------
The first time you open it, the tool reads Dota 2's own
files to build a list of every skin in the game. This can
take 10-30 seconds on a slow disk. The window may look
empty for a moment. Wait. It is not frozen.

After the first run it starts instantly, because the list
is cached in:
       %APPDATA%\dota2PyScriptPlus\
Do not delete that folder unless you want a rebuild.


CAMERA ZOOM - IMPORTANT
-----------------------
Dota 2 itself limits how far you can zoom out, and that
limit is different in every mode:

   Ranked / Turbo ......... capped (usually around 1134-1200)
   Demo Hero / bot lobby .. NOT capped (goes to 1400)

So if F8 (1400) does nothing in a real match, that is the
game refusing it, not the tool failing.

Keys:
   F5 = 1200     F6 = 1250     F7 = 1300
   F8 = 1400     F9 = reset to default

To find the highest value your mode allows, press F9 first,
then F5, and walk up to F8. The one that stops changing
the view is one step past your limit.

Note: the Camera tab is not finished yet. An update is
coming soon.


WHAT GETS MODIFIED
------------------
The tool writes its files into your Dota 2 folder, next to
the game. It keeps backups of anything it replaces.

To undo everything, open the tool and press REMOVE /
RESTORE on the tab you used. If you ever want a fully
clean game, in Steam:
   right-click Dota 2 -> Properties -> Installed Files
   -> Verify integrity of game files


TROUBLESHOOTING
---------------
Nothing happens when I press a key
    Dota 2 must be the active window. Click into the game
    first. Also make sure the tool is still running in the
    background.

"Failed to load items_game.txt"
    The tool could not read Dota 2's files. Run the tool as
    administrator, or check that Dota 2 is fully installed
    and not mid-update.

The console does not open in game
    Add  -console  to Dota 2 launch options:
    Steam -> right-click Dota 2 -> Properties -> Launch
    Options -> type:  -console
    Then press the  ~  key (below Esc) inside the game.

Windows Defender / SmartScreen complains
    Unsigned exe, expected. Choose "Run anyway".


UPDATES
-------
The tool checks for updates on its own. If a new version
exists you will see a small notice in the corner. If the
update is required, the tool will ask you to download it
before you can continue. You never need to re-download
manually.


DISCLAIMER
----------
This software changes local game files only. Use it at
your own risk. The author is not responsible for any
account action taken by Valve, or for any damage caused
by misuse. If you do not accept this, do not run the exe.

========================================================
