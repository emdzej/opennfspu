# Extract the game

OpenNFSPU reads the **installed** game folder: `GameData/`, `FEData/` and `Porsche.exe` (version 3.5),
or an `.iso` image of that folder.
On Windows, install the game and apply the 3.5 update as usual. On macOS and Linux you don't need to run
the Windows setup: get the folder out of the installer instead.

## A repack disc image with a RAR installer

Some copies are a single disc image (for example `NFS_V.ISO`) whose `Setup/install.exe` is a RAR
self-extracting archive of the already installed and updated game (version 3.5). Extract it with
[`unar`](https://theunarchiver.com/command-line) (The Unarchiver's command line tool; `brew install unar`,
`apt install unar`):

```sh
# macOS: mount the image read-only
hdiutil attach -readonly -nobrowse -mountpoint /tmp/nfsiso NFS_V.ISO
unar -o ~/Games /tmp/nfsiso/Setup/install.exe
hdiutil detach /tmp/nfsiso
# -> ~/Games/Need For Speed - Porsche Unleashed/ (GameData, FEData, Porsche.exe, ...)
```

On Linux, mount it with `sudo mount -o loop,ro NFS_V.ISO /mnt` (or extract the ISO with `bsdtar`) and run
the same `unar` command. 7-Zip and p7zip list the archive but fail on it with "Unsupported Method": use
`unar`.

## A retail CD

The retail discs carry the game in the installer's own layout. Install it on Windows (or in Wine) and
apply the 3.5 update, then copy the installed folder; at least `GameData/`, `FEData/` and `Porsche.exe`
are needed.

## Check

```sh
ls "Need For Speed - Porsche Unleashed"
# FEData  GameData  Porsche.exe  ...
wc -c < "Need For Speed - Porsche Unleashed/Porsche.exe"     # 2129965 for version 3.5
```

For development, put (or link) the folder at `./game` in the checkout.

## Make an ISO of it (optional)

One file instead of a folder (the launchers and the browser player take either):

```sh
hdiutil makehybrid -iso -joliet -o NFS_PU.iso "Need For Speed - Porsche Unleashed"       # macOS
genisoimage -J -joliet-long -r -o NFS_PU.iso "Need For Speed - Porsche Unleashed"        # Linux
```

On Windows use any ISO 9660 + Joliet tool (ImgBurn, or `oscdimg -j1` from the Windows ADK). The image must contain the
*installed* game; see [Game data](/guide/game-data#or-an-iso-image-of-the-folder).
