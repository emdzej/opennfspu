# Inventory of Porsche.exe

Address ranges of `Porsche.exe` (v3.5) mapped to modules, with the C++ classes found through RTTI. The
linker kept object-file order, so each module is one contiguous span. The boundaries come from the
vtable methods of each class, from source-file and error strings, and from imports. They are accurate
to a few functions at the edges.

Counts are after `tools/ghidra/FillGaps.java`, which raised Ghidra's 6,006 functions to 8,013. Most of
the added ones are vtable methods and callbacks that nothing called directly. 7,780 functions are in
`.text`/`PACODE` and decompile; the rest are thunks. "Named" counts understood names in
`tools/ghidra/names.tsv` (confidence high/med/low). "Class slots" counts functions named `Class_vN` after
their vtable slot. See [architecture.md](architecture.md) for how the pieces fit together.

## Modules

| Range | Module | Functions | Code | Named | Class slots | Contents |
|---|---|---:|---:|---:|---:|---|
| 401000-410000 | ai | 251 | 58 KB | 7 | 21 | AI. `AIHigh_*` decides who drives (player, human, opponent, cop, perp, traffic, none); `AIState_*` is the driving state (idle, normal, racing, chase, offroad, purgatory, goto-slice). Also the AI data files (`.cop`, `.lsp`, `trafcfg.dat`, `prsonal.bin`/`scripts.bin`/`hhglue.bin`, `tuning.bin`) |
| 410000-418000 | game | 102 | 31 KB | 22 | 0 | Race core: car/parts records, the input packet queue, replay load/save, `RaceLoop` 0x415300, `Sim_Step` 0x415010, task lists, race end |
| 418000-437700 | hud | 565 | 114 KB | 14 | 113 | HUD: `H*` visuals (tach, speedometer, gear, track map, positions, timers, pause, replay, summaries, congrats), `.hud` layouts, `hud.loc` |
| 437700-458000 | render | 507 | 125 KB | 21 | 83 | 3D render: `R3DCar`/`R3DObject` (+LOD) loaded from `.crp`/`.tpg`/`.env`/`.map`, material layers (`RCustomLayers::*`, `Headlights::*`, specular/sky/shadow layers), `RDraw*Bundle`, billboards, particle systems, texture conversion |
| 458000-460000 | sky | 140 | 30 KB | 3 | 22 | Sky: `SkyLevel_*`, `SkyHorizon`, `SkyMapSource_*`, cloud scrollers (`Sky\*.bin/.fsh/.txt`) |
| 460000-464000 | camera | 52 | 15 KB | 1 | 0 | Cameras and the replay/spectate step (`Camera_Update` 0x463af0) |
| 464000-469000 | rendersys | 105 | 18 KB | 12 | 0 | `NFSRegistryManager`, the render system (`Render_Init`, `Render_SetVideoMode`, `Render_Frame`, views) |
| 469000-471000 | scene | 132 | 30 KB | 9 | 31 | Scene elements from `.scn` (camera, geometry, particle, trigger, audio), start grids, skill tests |
| 471000-474000 | track | 53 | 11 KB | 3 | 8 | `Track_Load`/`BWorld_DeInit`, animation types (`QuatAnim`, `CamAnim`, `SmackAnim`) |
| 474000-488000 | world | 329 | 77 KB | 8 | 56 | World: `W*Bundle` (static, instance, mid-ground, scene, line, anim), the `WTreeElem_*` collision tree, `P*` collidables, `Tr<nn>.bin` specs, object definitions, `.crp`/`.jnc`/`.edg` track files |
| 488000-494000 | net | 215 | 46 KB | 13 | 3 | Network game: online lobby (`fsNet`, earacing.com), `CDataPacket`, client/server (`CConHost`/`CConClient`), HTML results. **Stubbed in the port** |
| 494000-4a4000 | sim | 100 | 62 KB | 7 | 0 | Simulation: car physics (the densest float code in the exe), parts (`nfs5.prt`), `.sim`/`.ais` car data, engine model, per-track settings |
| 4a4000-4b0000 | gamesys | 172 | 46 KB | 23 | 7 | Loading screens, `udff` file handles, updater/registration (mrbupd), and the game side of audio (banks, speech, engine audio, music, track sounds) |
| 4b0000-4b8000 | options | 64 | 30 KB | 16 | 3 | Option records (`fe.txt` / `fe_out.txt`), `WinMain` 0x4b6500, `GameMain` 0x4b6840 |
| 4b8000-524000 | frontend | 2426 | 412 KB | 39 | 711 | Front end: `FE*` controls and accessors, data areas (`FEda*`), screens and callback lists, 3D rooms, career (eras, tournaments, chronicle, factory driver), dealership/garage/parts, saves (`.sav`, `player.lst`, `nfs5.per`), comms screens |
| 524000-52c000 | fert | 165 | 29 KB | 5 | 29 | FERT front-end runtime: `Window`, `Control`, `Part`s, `Attribute`s, `EventPump`/events; `.lay`/`.ctr`/`.scr` parsing |
| 52c000-534000 | real-mem | 146 | 28 KB | 8 | 0 | Memory (FastAlloc, EA block heap `MEM_purge`), an expression parser, `INPUT_init` |
| 534000-53fab0 | real-gfx | 330 | 41 KB | 30 | 4 | EA real: shapes and CLUTs (`depthconv.c`, `shpclut.c`), windows (`createw.c`, `startup.c`), timers (`lowtimer.c`), fonts (`fontpc.c`, `fontattr.c`, `fontdraw.c`), EAGL2 scene/view/matrix API |
| 53fab0-554000 | eagl | 62 | 79 KB | 0 | 1 | EAGL2 geometry kernels: transform, clip and light routines specialised per vertex format (5-10 KB each), selected through function pointers |
| 554000-575e00 | real-sys | 688 | 95 KB | 59 | 0 | EA real: dynamic textures (`dyntexture.c`), CPU and Glide detection, ICMP, TAPI modem (`tapipkt.c`), key (`key.c`), joystick, timer (`inittmr.c`), file system (`nfile.c`, `hlsfile.c`, `locatbig.c`), streams (`stream.c`), packs (`loadpack.c`), `movdfl.c`, `fontdtex.c`, `creates.c`, the THRASH loader (`set3d.c`), DirectDraw (`ddraw.c`) |
| 575e00-580000 | real-kern | 17 | 30 KB | 1 | 0 | Large float kernels (7 functions, 3-7 KB each, one caller each), probably software geometry for fonts/shapes; not identified yet |
| 580000-58c000 | real-net | 325 | 36 KB | 8 | 0 | EA real networking: TCP (`tcp.c`), RAS, TAPI loader, sync objects, `packet.c`, `unrely.c`, `netgathr.c`. **Stubbed in the port** |
| 58c000-59d000 | real-snd | 354 | 58 KB | 2 | 0 | EA SND sound library on DirectSound (DirectSoundCreate users 0x58e400, 0x58e990, 0x597d20), memory-mapped files, `llpacket.c` (0x594f50-0x595460) |
| 59d000-5a0b80 | support | 97 | 13 KB | 14 | 1 | NFS Support/Utility: `UMemory` object heap, `UFileFind`, `UFileHandler`, FastBlock, error box |
| 5a0b80-5aeb10 | crt | 360 | 50 KB | 8 | 3 | MSVC 6 C/C++ runtime (sprintf, rand, memcpy, dynamic_cast, exceptions, stdio, heap, locale). Not ported. CRT objects start at odd addresses, EA objects are 16-aligned |
| 5aeb10-5af1f0 | real-tail | 8 | 0 KB | 1 | 0 | EA real objects linked between CRT objects: `SHAPE_loadfile` 0x5aeb10 (`loadshp.c`), debug helpers (IsDebuggerPresent, VirtualProtect) |
| 5af1f0-5b0000 | crt | 2 | 0 KB | 0 | 0 | MSVC runtime, continued |
| 5b0000-5b2000 | pacode | 13 | 3 KB | 0 | 0 | `PACODE` section: hand-written routines (MMX memcpy 0x5b0480) |

Data sections: `.rdata` 0x5b2000 (vtables 0x5b2494-0x5bb284, RTTI, strings, float constants), `.data`
0x5cb000 (0xf6478 bytes in memory, 0x1c000 on disk: the game globals, mostly zero-initialised), `.rsrc`,
`.reloc`.

## Imports

There are 239 imports from 14 DLLs:

- KERNEL32 132, USER32 48, WSOCK32 27 (by ordinal), ADVAPI32 8 (registry), WINMM 8 (timer, joystick),
  mrbupd 5 (updater), VERSION 3, DDRAW 2 (DirectDrawEnumerateA, DirectDrawCreate), DSOUND 2 (ordinals 1
  and 6), GDI32 2, DINPUT 1 (DirectInputCreateA), SHELL32 1.
- Loaded at run time: the THRASH driver (`Drivers\dx7z.dll`, 36 entry points), TAPI32, rasapi32,
  ICMP, `gimme.dll` (picture conversion for user driver photos; `FE_LoadDriverPictures` 0x4db650).

Direct3D itself is only used inside the THRASH DLL. There is no Miles or Bink: audio is EA's own SND
library, and movies are EA MAD (`showmad` 0x4dc300).

`tools/ghidra/ImportUsers.java` writes `re/imports.tsv` (import -> calling functions). Some imports are
referenced only from code that the first analysis missed; after `FillGaps.java` they resolve.

## C++ classes

The exe has 366 RTTI type descriptors and 360 vtables (complete object locators found by scanning
`.rdata`). Each entry below gives the class, its vtable address, `+offset` for a secondary vtable
(multiple inheritance), and the slot count up to the next vtable. The method lists are in
`re/vtables.tsv`, written by `tools/ghidra/rtti.py`.

### AI (21 vtables)

`AIDataRecord_LineSpeed_t` 5b24a0 (2), `AIDataRecord_t` 5b2494 (2), `AIHigh_Base` 5b25b0 (2), `AIHigh_BasicCop` 5b24b0 (2), `AIHigh_BasicPerp` 5b24d0 (3), `AIHigh_Cop` 5b2510 (2), `AIHigh_Human` 5b2544 (3), `AIHigh_None` 5b2594 (2), `AIHigh_Opponent` 5b2560 (3), `AIHigh_Player` 5b2570 (3), `AIHigh_Traffic` 5b2588 (2), `AIState_Base` 5b2500 (3), `AIState_Chase` 5b26c8 (3), `AIState_GotoSlice` 5b2720 (3), `AIState_Idle` 5b252c (3), `AIState_NonActive` 5b24f0 (3), `AIState_None` 5b25a0 (3), `AIState_Normal` 5b2668 (3), `AIState_Offroad` 5b26fc (3), `AIState_Purgatory` 5b2710 (3), `AIState_Racing` 5b26b8 (3)

### HUD (40 vtables)

`H321Go` 5b27ec (15), `HBillBoard` 5b28bc (13), `HButtonVisual` 5b2904 (8), `HCheat` 5b2944 (13), `HComboTextVisual` 5b297c (15), `HCongrats` 5b2a40 (12), `HDial` 5b2aac (16), `HExit` 5b2b0c (13), `HGear` 5b2b44 (15), `HGraphicDamage` 5b2b84 (15), `HGraphicVisual` 5b2bcc (6), `HInfo` 5b2bfc (13), `HLeader` 5b2c34 (15), `HMissionSummary` 5b2c88 (13), `HMultiTime` 5b2ccc (15), `HOverlay` 5b2d0c (12), `HPause` 5b2d54 (13), `HPosition` 5b2dbc (15), `HRaceSummary` 5b2e60 (13), `HRearview` 5b2e98 (15), `HRenderVisual` 5b2ed8 (6), `HReplay` 5b2f3c (15), `HReplaySave` 5b2f8c (13), `HSpeed` 5b2fd8 (15), `HSpeedometer` 5b3020 (16), `HTable` 5b3078 (6), `HTach` 5b3098 (16), `HTachDigital` 5b30e0 (15), `HTextDamage` 5b3120 (15), `HTextEntryVisual` 5b3160 (8), `HTextVisual` 5b3184 (6), `HTime` 5b31a0 (15), `HTournamentStandings` 5b3210 (13), `HTrackMap` 5b3248 (15), `HTrackRecords` 5b32b0 (13), `HTray` 5b3318 (15), `HVisual` 5b34bc (6), `HWinnings` 5b34d8 (12), `HWrongWay` 5b350c (12), `HYesNoOverlay` 5b3554 (13)

### 3D render, sky, particles (50 vtables)

`Headlights::LayerUVLight` 5b3dac (9), `Headlights::LayerVertexLight` 5b3b9c (10), `Headlights::LayerVertexLight_Ambient` 5b3b44 (10), `Headlights::LayerVertexLight_Opaque` 5b3b70 (10), `R3DBase` 5b358c (1), `R3DCar` 5b3600+12 (7), `R3DCar` 5b3620 (25), `R3DCarLOD` 5b36ac (7), `R3DCarParticle` 5b3794 (1), `R3DLOD` 5b36cc (7), `R3DLightLayer` 5b393c (9), `R3DObject` 5b37d0+12 (7), `R3DObject` 5b37f0 (22), `R3DObjectLOD` 5b3890 (7), `R3DObjectLayer` 5b3c44 (9), `R3DShadowMapTextureLayer` 5b3964 (9), `R3DSkyMapTextureLayer` 5b398c (9), `R3DSpecularLayer` 5b3c6c (9), `R3DSpecularTextureLayer` 5b39b4 (9), `R3DTypes::W3DObjectBundle` 5b384c (10), `RCustomLayers::LayerAmbient` 5b3acc (9), `RCustomLayers::LayerFalseSort1` 5b3a04 (9), `RCustomLayers::LayerFalseSort2` 5b39dc (9), `RCustomLayers::LayerFullAmbient` 5b3b1c (9), `RCustomLayers::LayerIndexedColour` 5b3a54 (9), `RCustomLayers::LayerIndexedUV` 5b3a2c (9), `RCustomLayers::LayerSetAlpha` 5b3af4 (9), `RCustomLayers::LayerTransformCoords` 5b3a7c (9), `RCustomLayers::LayerTransformIndexCoords` 5b3900 (9), `RCustomLayers::LayerUVAnimate` 5b3bc8 (9), `RCustomLayers::LayerUntransformedCoords` 5b3aa4 (9), `RDrawAnimArticleBundle` 5b3cb8 (7), `RDrawArticleBundle` 5b3cf0 (7), `RDrawLineBundle` 5b3d28 (7), `RGenericMovableParticleSystem` 5b3e24 (5), `RGenericMovableParticleSystem::RGenericMovableParticleCreator` 5b3cd8 (1), `RGenericParticleSystem` 5b3df8 (5), `RGenericParticleSystem::RGenericParticleCreator` 5b3d10 (1), `RParticleSystem` 5b3de0 (5), `Scroller_Base` 5b3f1c (2), `Scroller_BasicSky` 5b3f28 (2), `Scroller_FreeClouds` 5b3f44 (2), `SkyHorizon` 5b3e54 (4), `SkyLevel_Base` 5b3e70 (4), `SkyLevel_BasicSky` 5b3e8c (4), `SkyLevel_FreeClouds` 5b3eb8 (4), `SkyLevel_MultiSky` 5b3ea0 (4), `SkyMapSource` 5b3ee8 (2), `SkyMapSource_Noise` 5b3f08 (2), `SkyMapSource_Texture` 5b3ef4 (2)

### Scene elements and animation (10 vtables)

`Anim` 5b45bc (9), `CAudioElem` 5b4b58 (20), `CCameraElem` 5b4228 (20), `CGeomElem` 5b4280 (20), `CParticleElem` 5b44ac (20), `CSceneElem` 5b4500 (20), `CTriggerElem` 5b4554 (20), `CamAnim` 5b4634 (9), `QuatAnim` 5b460c (9), `SmackAnim` 5b45e4 (9)

### World and collision (18 vtables)

`PCollidableBundle` 5b465c (5), `PCollideArticleBundle` 5b4674 (5), `WAnimBundle` 5b46b8 (11), `WAnimatableArticleBundle` 5b469c (2), `WAnimatableBundle` 5b46a8 (2), `WArticleBundle` 5b46e8 (11), `WInstanceBundle` 5b4718 (11), `WLineBundle` 5b4750 (9), `WMidGroundBundle` 5b477c (11), `WSceneBundle` 5b47e0 (11), `WStaticBundle` 5b4814 (11), `WTreeElem` 5b4868 (5), `WTreeElem_BoxObject` 5b4938 (10), `WTreeElem_CylinderObject` 5b490c (10), `WTreeElem_Edge` 5b484c (6), `WTreeElem_Object` 5b48e0 (10), `WTreeElem_Quad` 5b4880 (11), `WTreeElem_Tri` 5b48b0 (11)

### Network (3 vtables)

`CConClient` 5b4968 (1), `CConHost` 5b4978 (1), `CConnection` 5b4970 (1)

### Front end (179 vtables)

`FE3DObject` 5b6808 (1), `FE3DRoom` 5b6eac (47), `FEAccessor` 5b4bbc (9), `FEAccessorIterator` 5b5414 (27), `FEAccessorWatch` 5b5484 (27), `FEAccountDisplayControl` 5b54f8 (27), `FEAliasedPositionableAccessor` 5b4be4 (31), `FEAnimatingGoToButton` 5b5d80 (27), `FEAnimatingPicture` 5b5568 (27), `FEAnimatingShowcaseControl` 5b55d8 (27), `FEApplication` 5b683c (12), `FEAvailableSongsAccessor` 5b5648 (27), `FEBeveledOptionsBoxes` 5bad34 (27), `FEBoolAccessor` 5b4c64 (30), `FEBottomBarMenu` 5b6888 (42), `FECallBackList` 5b9684 (55), `FECameraPicture` 5b5808 (27), `FECameraSelect` 5b5798 (27), `FECar3DControl` 5b5878 (27), `FECarAccessor` 5b74c0 (37), `FECarCompareCallbackList` 5b9764 (55), `FECarCompareDataAccessor` 5b9844 (37), `FECarLogoControl` 5b5900 (27), `FECarSettingButton` 5b5970 (28), `FECarSettingsCarAccessor` 5b4ce0 (37), `FECarSetupCallbackList` 5b7558 (55), `FECareerFirstScreen` 5b86b8 (55), `FECharVectAccessor` 5b4d78 (28), `FECharVectAccessorInputBox` 5b59e4 (27), `FEChatDisplayBox` 5b5a58 (27), `FEChatInputBox` 5b5ac8 (27), `FECheckBox` 5b5b38 (27), `FEChronicleCallBackList` 5b8b24 (55), `FEChronicleMainBoxControl` 5b8c04 (27), `FEChroniclePictureControl` 5b8c74 (27), `FEChroniclePictureIconControl` 5b8ce4 (27), `FEColorSelectBox` 5b5ba8 (30), `FEColourAccessor` 5b4e60 (28), `FEComboBox` 5b5c24 (30), `FECommInfoAccessor` 5b4ed4 (27), `FECommsCreateScreen` 5b8dfc (55), `FECommsErrorHandler` 5b8edc (55), `FECommsGamesScreen` 5b8fbc (56), `FECommsJoinScreen` 5b90a0 (55), `FECommsLanGamesScreen` 5b9180 (56), `FECommsModemScreen` 5b9264 (55), `FECommsPeerToPeerIP` 5b9344 (55), `FECommsServerScreen` 5b9424 (55), `FECommsWebGamesScreen` 5b9504 (56), `FEControl` 5b5ca0 (27), `FEControllerDeadZoneAccessor` 5bac0c (29), `FEControllerDeadZoneControl` 5bab9c (27), `FEControllerInputAccessor` 5bacf4 (9), `FEControllerSetupControl` 5bac84 (27), `FECreditsCallBackList` 5b98dc (55), `FECurrentSongSelection` 5b5728 (27), `FEDealership3DRoom` 5b6f6c (47), `FEDealershipCallbackList` 5b99bc (59), `FEDialog` 5ba060 (20), `FEDialogButton` 5ba108 (12), `FEDialogInput` 5ba23c (12), `FEDialogInputCancel` 5ba1e4 (21), `FEDialogOneItemNonSelfDeleting` 5ba0b4 (20), `FEDialogOneItemSelfDeleting` 5ba13c (20), `FEDialogTwoItem` 5ba190 (20), `FEDirectionAccessor` 5b5200 (28), `FEEmbossedVerticalSlider_MainFunctionality` 5b6a64 (12), `FEEmpty3DRoom` 5b7030 (47), `FEEndOfEraCallBackList` 5b84fc (55), `FEFaceInRace` 5b6a98 (1), `FEFactory3DRoom` 5b70f0 (47), `FEFactoryCallBackList` 5ba7c0 (58), `FEFactoryDriverCarAccessor` 5ba8ac (37), `FEFactoryDriverDataAccessor` 5ba798 (9), `FEFactoryDriverMapControl` 5ba9b4 (27), `FEFactoryDriverPictureControl` 5ba944 (27), `FEFactoryDriverPrizeCarAccessor` 5baa24 (37), `FEFactoryVictoryScreen` 5baabc (55), `FEFloatAccessor` 5b4f44 (27), `FEGameListAccessor` 5b4fbc (27), `FEGarage3DRoom` 5b71b4 (47), `FEGarageCallBackList` 5b9b94 (59), `FEGenericCarAccessor` 5b767c (46), `FEGlintyBoxControl` 5bada4 (27), `FEGlintyLineControl` 5bae14 (27), `FEGoToButton` 5b5d10 (27), `FEGouraudBoxControl` 5b5df0 (27), `FEGouraudIndicatorBar` 5b5e60 (27), `FEGraphicalTabButton` 5b5ed0 (29), `FEHeaderText` 5b5f48 (27), `FEHorizontalSlider` 5b5fc4 (27), `FEHudDisplayControl` 5b603c (27), `FEInputBox` 5b60ac (27), `FEIntAccessor` 5b502c (29), `FEKnockoutScreen` 5b9c84 (55), `FEKnockoutTrackListControl` 5b7e0c (27), `FEKnockoutTrackNumberAccessor` 5b50a4 (29), `FEListBox` 5b611c (30), `FELobbyPlayerListAccessor` 5b511c (27), `FELocalizedTextAccessor` 5b518c (28), `FELocalizedTextPart` 5b6e2c (7), `FEMainScreen` 5b9d64 (67), `FEMenu` 5b6b34 (41), `FEMergedCarListAccessor` 5b773c (38), `FEMiniPlayerBadge` 5b6440 (27), `FEMiniPlayerPicture` 5b6590 (27), `FEMovie` 5b6be4 (2), `FEMovieBox` 5b6198 (27), `FEMovieCircle` 5b6208 (27), `FEMovieControlButton` 5b6278 (27), `FEMultiplayerDataAccessor` 5b9604 (9), `FEOpponentCarAccessor` 5b77d8 (35), `FEOvalThing` 5b62e8 (27), `FEOwnedCarAccessor` 5b7868 (44), `FEPaintShopCBL` 5b9e74 (56), `FEPanel` 5b6358 (29), `FEPartsAllPartsAccessor` 5ba270 (27), `FEPartsCarSetupInventoryAccessor` 5ba350 (28), `FEPartsDamageRepairAccessor` 5ba3c4 (27), `FEPartsDamageRepairShoppingList` 5ba434 (27), `FEPartsRepairCBL` 5ba5a0 (60), `FEPartsShoppingBasket` 5ba2e0 (27), `FEPlayerBadge` 5b63d0 (27), `FEPlayerDriverPicture` 5b64b0 (27), `FEPlayerListAccessor` 5b5274 (27), `FEPlayerPicture` 5b6520 (27), `FEPositionableAccessor` 5b52e4 (27), `FEPrizeCarAccessor` 5b85dc (37), `FEPrizeCarMapControl` 5b8880 (27), `FEPurchasePartsCBL` 5ba694 (60), `FEQuickFixCallBackList` 5b9aac (57), `FERadioGroup` 5b6600 (30), `FERadioItem` 5b667c (29), `FERectPart` 5b6e0c (7), `FEScreenOptions` 5b9f58 (65), `FEScreenResolutionAccessor` 5b5354 (28), `FESelectedSongsAccessor` 5b56b8 (27), `FEShapePart` 5b6e64 (9), `FEShowcasePictureControl` 5b8d8c (27), `FEShowcaseTextAccessor` 5b8d64 (9), `FESlider` 5b66f4 (27), `FESlider_MainFunctionality` 5b6764 (12), `FESpecialUserListCharVectAccessor` 5b4dec (28), `FESpinning3DRoom` 5b7274 (47), `FEStringAccessor` 5b53c8 (18), `FETextPart` 5b6e8c (7), `FETournamentCallBackList` 5b8798 (57), `FETournamentDataAccessor` 5b81f0 (9), `FETournamentEventTypeAccessor` 5b8960 (28), `FETournyAccessor` 5b88f0 (27), `FETrackAccessor` 5b7e80 (29), `FETrackPicture` 5b7ef8 (27), `FETrackRecordsAccessor` 5b7f68 (27), `FETrackRecordsCallBackList` 5b7fd8 (55), `FETrackRecordsDescripAccessor` 5b80b8 (27), `FETrackSelect3DRoom` 5b7334 (47), `FETrackSmall2DSpline` 5b8128 (27), `FEUsedCarCallbackList` 5b7920 (59), `FEVerticalSlider` 5b6798 (27), `FEVictory3DRoom` 5b73fc (47), `FEVictoryScreen` 5b89d4 (55), `FEWordWrapAccessorWatch` 5baef4 (27), `FEWordWrapControl` 5bae84 (27), `FEdaAudio` 5b6bfc (3), `FEdaCameras` 5b6c2c (3), `FEdaCar` 5b7638 (3), `FEdaCarSetup` 5b766c (3), `FEdaChronicle` 5b8d54 (3), `FEdaComms` 5b95e8 (3), `FEdaControllers` 5bad1c (3), `FEdaDisplay` 5b6c3c (3), `FEdaFactoryDriver` 5ba788 (3), `FEdaGameOptions` 5b6d30 (3), `FEdaGraphics` 5b6d40 (3), `FEdaPlayerInfo` 5b8198 (3), `FEdaTestVariables` 5b6dc4 (3), `FEdaTournySim` 5b8218 (3), `FEdaTrack` 5b7dfc (3), `FEdaUserList` 5b6db4 (3)

### FERT runtime (22 vtables)

`FERT::Attribute` 5bb284 (5), `FERT::ColorAttribute` 5bb0c4 (5), `FERT::Control` 5bafd4 (20), `FERT::DataPart` 5bb240 (7), `FERT::Event` 5b6878 (1), `FERT::EventPump` 5bb150 (11), `FERT::IntAttribute` 5bb094 (5), `FERT::KeyPressEvent` 5bb190 (1), `FERT::LocalizedTextAttribute` 5b6e4c (5), `FERT::MouseButtonEvent` 5bb180 (1), `FERT::MouseMoveEvent` 5b6870 (1), `FERT::MouseWheelEvent` 5bb188 (1), `FERT::Part` 5bb1a0 (7), `FERT::PathNameAttribute` 5bb208 (5), `FERT::PointAttribute` 5bb0ac (5), `FERT::RectPart` 5bb1c0 (7), `FERT::ShapePart` 5bb1e0 (9), `FERT::StringAttribute` 5bb07c (5), `FERT::Target` 5bb028 (11), `FERT::TextPart` 5bb220 (7), `FERT::TimerEvent` 5bb148 (1), `FERT::Window` 5bb0e0 (25)

### EAGL (6 vtables)

`EAGL::LayerColours` 5b3bf0 (9), `EAGL::LayerCoords` 5b6a34 (9), `EAGL::LayerState` 5b69bc (9), `EAGL::LayerTexture` 5b6a0c (9), `EAGL::LayerUV` 5b69e4 (9), `EAGL::Material` 5b3c94 (2)

### Utility, registry, CRT (11 vtables)

`CExclusiveObj` 5b4ad8 (3), `NFSRegistryManager` 5b3fbc (3), `RegistryManager` 5b3fcc (3), `UFileFind` 5c1820 (1), `UFileHandler` 5c18ec (2), `UGameUpdater` 5b4ac8 (3), `__non_rtti_object` 5c1924 (2), `bad_cast` 5c1960 (2), `bad_typeid` 5c1930 (2), `exception` 5c1a88 (2), `type_info` 5c18f8 (1)

## Globals worth knowing

| Address | What |
|---|---|
| 0x5debf0, 0x5deb74, 0x5deb78 | Fatal error handler pointer; failing source file and line |
| 0x5e4fe8 | Allocator object (UMemory); slot 1 alloc, slot 2 free |
| 0x5e8b34 / 0x5e8b58 | Race cars (pointers) / count |
| 0x5e8c24 / 0x5e8b5c | Traffic cars / count |
| 0x5e6774 | AI controller per car (`AIHigh_*` objects) |
| 0x5e954c, 0x606ac8 | Action-word ring (32 frames) and its index |
| 0x5cc818 | Input packets queued (sim target = 2 x this) |
| 0x606aa0, 0x606a9c | Sim tick done / target |
| 0x606ac4 | Sim tick counter of the race (0x1c0 = start) |
| 0x606a8c, 0x606a88 | Race over / restart requested |
| 0x606acc, 0x606ad0 | Task lists run every tick / even ticks |
| 0x606af0, 0x607af0, 0x608af4 | Input packet rings (128 x 4 bytes per player), valid flags, write index |
| 0x6559e8, 0x6559ec, 0x6559f0 | 128 / 64 / 32 Hz timer counters |
| 0x6573e8 | Game settings block (0x3e24 bytes); first dword = game mode (3 = network) |
| 0x657424 | Race state (2 running, 3 ending) |
| 0x657a48/4c/50 | Race video mode width / height / bpp |
| 0x628c70 | Network session object (0 = single player) |
| 0x628130 | Render globals (near/far at +0xc) |
| 0x6115f0 | List of R3DCar objects (next at +0x474) |
| 0x6bd850-0x6bd8f8 | THRASH function pointers (bound by `THRASH_opendll`) |
| 0x65b360 | Save directory path |
