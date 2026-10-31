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

## The port's order

1. FERT's model: templates, layouts, controls, parts, attributes. Check: every template and layout
   parses.
2. Drawing the parts (rect, shape, text) through the HUD's 2D layer and the fonts.
3. The event pump from gasm's mouse and keys, and the controls' behaviours.
4. `FEApplication_Run` 0x4d2e70 as an app state: the data areas, the main screen, quick race, the
   race's start and the return to the menus.
5. The rest: career, dealership, garage, options, Factory Driver, the 3D rooms, movies.
