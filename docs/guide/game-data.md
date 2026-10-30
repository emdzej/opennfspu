# Game data

OpenNFSPU needs your own copy of **Need for Speed: Porsche Unleashed** for Windows, **installed and
updated to version 3.5**. It reads either the installed folder or a disc image of it (below):

| What | Needed for |
|---|---|
| `GameData/` | cars, tracks, simulation tables, sounds, speech, music |
| `FEData/` | the frontend: art, layouts, text, showroom models, movies |
| `Porsche.exe` | the original program: OpenNFSPU reads its tables at run time. It must be version 3.5: 2,129,965 bytes, CRC-32 `fd2f5573` |
| `Drivers/dx7z.dll`, `SaveData/` | used when present |

The default install folder is something like
`C:\Program Files (x86)\EA GAMES\Need For Speed - Porsche Unleashed`. On macOS and Linux you can copy
that folder from a Windows machine, or extract the installer without running it: see
[Extract the game](/howto/extract-game).

Names are matched case-insensitively, as on Windows.

## Or an ISO image of the folder

The game also runs from a single `.iso` file (ISO 9660 with Joliet long names) of the installed folder, or a
raw `.bin` of one: the whole game as one file. Give the image wherever a folder is asked for (the launchers' "Choose disc image", the
player's **Choose a disc image**, or `gasm-run opennfspu.wasm --asset cd=NFS_PU.iso`).

It must be an image *of the installed game*: `GameData`, `FEData` and `Porsche.exe` at its top (or one
folder down). The original setup disc won't do, nor a repack whose installer is an archive; the game
then stops with "game data not found". Make one from the installed folder:

```sh
# macOS
hdiutil makehybrid -iso -joliet -o NFS_PU.iso "Need For Speed - Porsche Unleashed"
# Linux (genisoimage; mkisofs and xorriso -as mkisofs take the same options)
genisoimage -J -joliet-long -r -o NFS_PU.iso "Need For Speed - Porsche Unleashed"
```

On Windows, any tool that writes ISO 9660 with Joliet works (for example ImgBurn's "Create image file
from files/folders", or `oscdimg -j1 <folder> NFS_PU.iso` from the Windows ADK). A `.cue` sheet only names an image: use the `.bin` next to
it.

## Version 3.5

The European and North American releases were updated by EA's patches; the last one is 3.5. Check the
size of `Porsche.exe`: if it is not 2,129,965 bytes, update the game first. The launchers, the browser
player and the game itself all check it and say so.

## What stays on your device

Nothing from the game is in OpenNFSPU, on its website or in its bundles. The browser player reads your
folder in the browser (or copies it into the site's private browser storage, if you import it); nothing
is uploaded.
