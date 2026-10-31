# Front end

The front end is about 2,400 functions (0x4b8000-0x524000). It sits on FERT, a front-end runtime of
165 functions (0x524000-0x52c000, `\frontend\fert\src\Window.cpp`). Below FERT is the INI text layer
(0x5234c0-0x523ae0), already ported (`src/render/car_ini.c`).

## The data

`FEData\Controls` holds the control templates: `*.ctr`, and `*.scr` for screens. Each is an INI file:

- `[control]`: the control's `id` (`REAL.LABEL`, `NFS5.BOTTOMBARBUTTON`, ...), a `description`, its
  `parts` and `states`, and attributes as `<name>.value` / `.flags` / `.type` / `.label`.
- `[state.<name>]`: the parts that state shows.
- `[part.<name>]`: the part's `id` (`RECT`, `SHAPE`, `TEXT`, `LOCALIZEDTEXT`, `DATA`) and its
  attributes.

The templates' values are expressions (`size.value=label.size.width*2`), and the game evaluates them.
A control's update (FERT_Control_v14 0x524f70) works in two passes:
- Every attribute's text is defined as a symbol: the control's as `control.<name>`, each part's as
  `<part>.<name>`, and a point's halves as `.x`/`.y` and `.width`/`.height`.
- Each attribute is then evaluated. Its symbols are expanded as text, recursively and without
  parentheses (0x52df30); a symbol that refers back to itself fails, and an exact `##` is dropped. The
  dots become `X` and an embedded C interpreter (0x52f120-0x532000) evaluates the result; an error
  gives 0.
- By kind: an int is 0 when empty; a point evaluates its two halves (nothing without a comma); a colour
  is hexadecimal after `0x`, else evaluated; a string replaces each `%name%` with name's expansion.
- A text part then measures its text in its font and sets its size (0x527310), so `label.size.width`
  is the label's width at the next update.

The templates use `+ - * / % & | ?:`, parentheses, and decimal and hexadecimal integers. The port
expands as the original does and evaluates with its own C expression evaluator (`src/front/fert_expr.c`).

`FEData\Layouts\*.lay` are the screens. `[section.0]` is the screen itself: size, colour, `room` (the 3D
room), `callbacklist`, `musicfile`. Each further `[section.n]` places a control by its template `id` and
`name`, with the attributes it overrides (`pos`, `stringID`, `fontPathName`, `destination`, ...).

## FERT

- Control templates are registered from both file kinds (FERT_ListControls 0x52c140): 0x1c-byte
  records of id, description and file.
- The layouts are listed once (FERT_ListLayouts 0x528900, `%s*.lay`).
- A control parses its template (FERT_Control_Parse 0x5246a0). First its attributes, then each part of
  `parts` (`part.<name>` through the part factory 0x4e5830, by id), then each state of `states`
  (0x529ab0).
- Classes (vtables): Target 0x5bb028, Control 0x5bafd4, Window 0x5bb0e0, Part 0x5bb1a0 with RectPart,
  ShapePart, TextPart and DataPart, and Attribute 0x5bb284 with String, Int, Point, Color, PathName and
  LocalizedText. The EventPump (0x5bb150) delivers MouseMove, MouseButton, MouseWheel, KeyPress and
  Timer events.

## 3D rooms

The application keeps three things beside its screen stack: the overlay screen of the screen's `screen`
attribute (+0xe4: `ID_SCREEN` is `screenempty.lay`, `ID_MOVIEOVAL` `screenmovieoval.lay`, ...; 0x4e1010),
the bottom bar (+0xe0) and the 3D room of its `room` attribute (+0xe8, made by name through the factory
map 0x6909c0: `ID_GARAGE`, `ID_DEALER`, `ID_FACTORY`, `ID_TRACK`, `ID_VICTORY`; 0x4e6f10). Its frame
(0x4d2980) draws the room first, in its own 3D camera (+0xfc: type 6, half angle 45 until a room sets
its own, an identity frame), then the 2D over it.

- **FE3DObject** (0x4d0bb0): a car state of its own (0x1254 bytes) and an R3DCar on it. Kind 0 loads
  `GameData\CarModel\<name>`, kind 2 a room model from `FEData\Models\`. Its position is set with
  half its height added (0x4d1150), its rotation about y set and about x and z turned further
  (0x4d1050, 0x4d1070, 0x4d10e0, in turns).
- **FE3DRoom** (0x4e6d60) holds the room's place (+0x278), rotation (+0x284), the camera's half angle
  (+0x290), the ambient and diffuse colours (+0x294, +0x2a0, 0..255), the light's direction (+0x2b8)
  and the car the screen puts in it (+0x26c).
- **FESpinning3DRoom** (0x4e9570) owns its model (+0x2ec). Its draw (0x4e9690) places the model and
  the car at the room's place, gives both their light (0x4e7b60: the room's colours for the model; a
  fixed 0.3 / 0.7 grey from (0, -0.707, 0.707) for the car), turns both about y once every +0x270
  seconds (30), turns the model's hinges 8 and 9, and draws them.
- **FEGarage3DRoom** (0x4e91d0): the model `garage` at (0, -1.1, 4.8), half angle 37.5, ambient
  (55, 65, 55), diffuse 120. Its tick turns the car's hinges 1 and 2. Its draw also sets the car's floor
  shadow (the 'FESH' texture and a matrix from the car's footprint), which is pending.
- **FECar3DControl** (`NFS5.3DCAR`, 0x4bf870 ..): with position type 0, it makes the accessor's car and
  hands it to the room (slot 41). It draws none of its parts (its `area` rect is a placeholder for the
  layout editor).

Port deviation: with these identity orientations, the port draws the models upside down while their
positions are right. The race's cars carry a half turn about their forward axis that the front end's
objects don't, and where the original makes up for it is not found yet. Until then the port draws each
front-end object with that half turn added, about the point the original's draw puts at its position
(`FE3DObject_Draw`).

## Overlays, panels and data areas

- **Overlay screens** (the application's +0xe4, 0x4e1010): a screen's `screen` id names the layout drawn
  over the room and under the screen. With the rooms' detail on (0x5d9d38) the ids map as follows:
  `ID_SCREEN` is `screenempty`, `ID_DARKOVAL` `screendarkoval`, `ID_FULLOVAL` `screenfulloval`,
  `ID_MOVIEOVAL` `screenmovieoval` and `ID_OPTIONOVAL` `screenoptionoval`. In the options opened from
  the race, the last is `screenoptionovallite`. These overlays are the translucent arcs, ovals and frames
  around the menus.
- **Panels** (`NFS5.PANEL`, FEPanel 0x4ce480 ..) hold the screen of their `filename`, loaded when opened
  (0x4ce4d0) and dropped when closed (0x4ce540). They start closed and hidden. Events go on to the
  panel's screen (0x4ce570), and its area rect is the layout editor's.
- **Radio groups** (`NFS5.RADIOGROUP`, FERadioGroup 0x4cf8a0 ..) are panels open from the start. Their
  screen's tab buttons (FERadioItem) each name a panel. Choosing one opens that panel and closes the one
  open before (0x4cf960). The options screen is one: a group of five tabs over five panels, two of which
  hold groups of their own.
- **Data areas** (FEda*) make the accessors. Their values start from the options in fe.txt, each with
  its key from the options' table (Options_Parse 0x4b0db0, (id, target, key) triples).
  - FEdaAudio (0x4e3fb0): five levels 0..127 (options 0x37..0x3b), mode and quality (0x36, 0x35).
  - FEdaDisplay (0x4e4610): the HUD's 14 choices (0x5b..0x69).
  - FEdaGraphics (0x4e4e00): the renderer's (0x44..0x54) and the gamma (0x4d).
  - An int accessor's text for the argument PERCENT is its position in per cent (0x4bc0e0).
  - A list accessor's items are string ids from a zero-ended list.
- **Control draws** the port has beside the GoTo buttons and the combo boxes:
  - the gouraud box (0x4c9a10): four corner colours, each corner moved by its offsets;
  - the indicator bar (0x4ca170 -> 0x521530): its accessor's share as two gouraud halves, set by a click
    (0x4c9f50);
  - the accessor watch: its accessor's text in its label.

## The port's order

1. FERT's model: templates, layouts, controls, parts, attributes. Check: every template and layout
   parses.
2. Drawing the parts (rect, shape, text) through the HUD's 2D layer and the fonts.
3. The event pump from gasm's mouse and keys, and the controls' behaviours.
4. `FEApplication_Run` 0x4d2e70 as an app state: the data areas, the main screen, quick race, the
   race's start and the return to the menus.
5. The rest: career, dealership, garage, options, Factory Driver, the other 3D rooms, movies.
