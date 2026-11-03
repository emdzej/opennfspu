# Split screen (internal notes)

The race has a two-player split-screen mode that the PC front end never offers.

## How it is switched on

- `COMM_MODE` (0x657404; the port's `g_race_players`) set to 1. In the options symbol table (0x5d63a0) the value
  is `GameSetup_kSplitScreen` (`kNoComm` 0, `Network` 4). The shipped `fe.txt` has `comm_mode = 0`,
  `numplayerracecars = 1`, `car[0].class=human`, `car[1].class=ai` and `catchup_logic = 0` (0x657420).
- At startup GameMain (0x4b6840) clears the race block (0x6573e8, 0x3e24 bytes) and reads fe.txt into option
  records: Options_Read 0x4b6450, then 0x4b5cd0 reads the lines, then Options_Import 0x4b5260 parses one line:
  the name goes to an id through the table at 0x5d1e40, and the value is parsed by its type. Each record is then
  applied (Option_Apply 0x4b4950).
- Before each race the same thing happens again. FEApplication_Run takes the record list and edits it with the
  front end's choices; the records are applied; then Race_Configure runs on the list. A `comm_mode = 1` that the
  front end doesn't overwrite therefore reaches the race.
- The port does not do this yet. It reads a few keys by name (render options, the HUD's in Options_Apply) and
  builds the race from the front end's accessors (app.c). Options_Import is the first step. It also matters
  without split screen: every fe.txt race key then reaches the race the way the original's do.

## Where the race branches on it (COMM_MODE == 1)

| Address | Function | What |
|---|---|---|
| 0x401060 | FUN_00401060 | a per-frame condition |
| 0x403ed0 | AIHigh_Cop_v0 | police behaviour |
| 0x405b30 | FUN_00405b30 | 4 instead of 2 |
| 0x40e080 | AIState_Purgatory_v2 | a table index offset by 4 |
| 0x410dc0 | CarSpecs_Load | with CATCHUP_LOGIC and car flag 4 |
| 0x415fe0 | Race_UpdatePositions | with CATCHUP_LOGIC: the catch-up |
| 0x463f70 | camera setup | two cameras (0x627e80 records, 0x10c bytes each) |
| 0x4641b0 | camera teardown | two cameras freed |
| 0x4a6e50, 0x4a7430 | the car's control or drive code | the second player's inputs |

Still to map: the render views (whether 0x466df0 or another constructor splits the main view in two), the
two HUDs, the second player's control bindings (`c1_command*` in fe.txt points at player 1's), and the
0x627f40 flag bit 0x80 that camera.c:1121 treats as the second player's camera.

## Plan

1. Options_Import (0x4b5260) and the startup/per-race record flow, with the port's accessor path kept as the
   front end's edits. The hash runs must not move unless a fe.txt key really changes the race.
2. The two cameras and views, top and bottom halves.
3. Two HUDs, and the second player's controls.
4. The catch-up logic.
