# PALETTE: the reference instrument's controls, from its demo video

This note records how the reference instrument is operated in its maker's demo video, and what PALETTE takes from it.
The source is one video, watched for behaviour only:

- "Hologram Electronics Chroma Console", Hologram Electronics, <https://www.youtube.com/watch?v=NnKPP8fTlc4>, 5:05.

The video has no narration. It shows the unit from above beside a player, a hand on the controls, and short text
cards. Everything in section 1 is what the pictures and the cards show, in my own words, with the time in the video.
A statement that the pictures do not show outright is marked **inferred**. No frame, artwork, or text of the video is
in this repository. [PALETTE.md](PALETTE.md) describes the instrument that results.

## 1. What the video shows

### 1.1 The panel

- Four columns, one a module, left to right: Character, Movement, Diffusion, Texture. Each column has a colour, two
  knobs above each other, and one button under them (0:08, 0:50, 1:02).
- The upper knobs are TILT, RATE, TIME, and MIX. The lower knob of each column is that module's AMOUNT. A second name
  is printed under each knob, for a second layer (1:06: EFFECT VOL under AMOUNT).
- Under each button is a list of the module's five effects, each with a dot of its own colour (1:06).
- Two footswitches. The left one is marked for tap and for capture, the right one for bypass and for presets (1:18).
- A bar of lights between the footswitches changes colour with what the unit is doing (1:21, 2:18).

### 1.2 Choosing an effect (1:06 to 1:11)

- A card says that effects are selected by pressing the module's button.
- A finger presses the Texture button five times. The button's light changes with each press, through the colours of
  the five dots in the list, top to bottom. One press is one step down the list.
- Before the first press the button is dark. **Inferred:** a dark button is a module that is off, and a press on it
  starts at the first effect.
- **Inferred:** after the fifth effect the next press starts again at the first. The video stops at the fifth.

### 1.3 Shaping an effect (0:26, 0:54, 1:02 to 1:04)

- A card says that effects are controlled by the colour-coded knobs in each module's column. The picture lights one
  column, and a hand turns its upper knob and then its lower one.
- The knobs are always live: the hand goes straight to a knob while the sound plays. Nothing is selected first.

### 1.4 The order of the modules (1:20 to 1:26)

- A card says that modules can be re-routed in any order.
- Two fingers press the first and the fourth button together (1:20.0). All four buttons light, and the bar of lights
  changes (1:20.5).
- One finger then presses the four buttons one after another: the fourth, the third, the second, the first (1:23.5 to
  1:25.0). With each press one more segment of the bar lights, in the colour of the module pressed.
- After the fourth press the bar goes back to what it showed before (1:26.0). **Inferred:** the modules now run in
  the order they were pressed, and the fourth press ends the mode by itself.

### 1.5 One module into another (1:27 to 1:31)

- A card says that one module can overdrive another, or the whole chain can be pushed into soft clipping.
- A hand turns knobs of the upper row, the rightmost last. **Inferred:** these are the second layer's level knobs
  (a module's EFFECT VOL, the OUTPUT LEVEL). The video does not show how the second layer is entered.

### 1.6 Bypass, and which modules it switches (1:32 to 1:36)

- A card says that one can choose which modules bypass and engage, and calls it dual bypass.
- All four buttons are lit. A finger presses the right footswitch (1:34.0): the lights of Diffusion and Texture go
  out, and those of Character and Movement stay. A second press brings them back (1:35.5).
- So the footswitch switches a chosen set of modules, and the others keep sounding. **Inferred:** with no set
  chosen it switches all four. The video does not show how the set is chosen.

### 1.7 Recording a knob's movement (1:36 to 2:10)

- Cards call it gesture recording: knob movements are recorded and looped.
- Two fingers press two buttons together (1:38). The bar turns white. A hand turns a knob (1:46 to 1:48), then two
  buttons are pressed again (1:50). The player then plays with both hands while the sound keeps moving.
- **Inferred:** the two-button press arms the recording, the knob's movement is the recording, and the second press
  ends it.

### 1.8 Capturing a loop (2:12 to 2:48)

- Cards say that capture records short sustaining loops, and up to 30 seconds of looped phrases, after the effects
  by default or before them.
- A finger, later a shoe, holds the left footswitch (2:16 to 2:28). The bar changes colour several times.
  **Inferred:** holding the left footswitch records, and the colours are record, overdub, and play.

### 1.9 Presets (3:42 to 3:48)

- A card says that all sounds and settings are saved in one of 80 user presets.
- A finger holds the right footswitch (3:44.0 to 3:45.0) while the bar shows a few segments. A hand then presses the
  buttons (3:45.5 to 3:46.0), and the bar changes.
- **Inferred:** holding the right footswitch opens the presets, and the four buttons pick one. The video does not
  show how a preset is saved.

### 1.10 Feedback and timing

- Every press answers at once with a light: the button's colour for an effect, a segment of the bar for the order.
- There is no screen and no menu. A mode (the order, a gesture, the presets) is entered with one gesture, shows
  itself on the bar, and ends by itself or with the same gesture.

## 2. What PALETTE takes from it

PALETTE runs on a pedal with one knob with a push, a volume knob, seven buttons, two footswitches, and a 240 x 240
screen. The reference has eight knobs. That is the one difference that forces another mapping: one knob serves four
columns, so a column must be selected before the knob is its knob.

| In the video | On PALETTE | Why it differs, where it does |
| --- | --- | --- |
| A module's button: each press is the next effect | The same, once the module is selected. The first press on another module selects it and changes nothing | The first press makes the knob that module's. Without it, reaching a module's AMOUNT would change its effect |
| The button's light shows the effect by its place in the list | The screen lists the module's five effects for a moment after a press, the selected one lit | The pedal has a screen. Its button lights are not driven yet |
| A column's two knobs are always live | The knob is the selected module's AMOUNT. PUSH goes to the column's other knob (TILT, RATE, or TIME), then to the second layer's (SENSITIVITY or DRIFT, EFFECT VOL) | One knob |
| Two buttons together, then the four buttons in the wanted order | FX, then the four module buttons in the wanted order. The screen numbers each module as it is pressed, and the fourth press ends the mode | FX is a button of its own here, so no chord is needed |
| The right footswitch: bypass, for a chosen set of modules | The same. The set is chosen in the order mode: hold the right footswitch and press a module's button | The choice is **inferred** from the result the video shows |
| The right footswitch held: the presets, picked with the buttons | The same: a list on the screen. The footswitches step through it, the knob scrolls it, and the four module buttons jump between four banks of twenty | A list on a screen can be stepped by foot |
| The left footswitch: tap | The same. REV taps too | |
| The left footswitch held: capture | Nothing yet | Capture is not built |
| Two buttons together: a gesture | Nothing yet | Gesture is not built |
| A bar of lights for the mode | The screen's last line, and the tiles | |

A long press of a module's button switches the module off and on. The video does not show a long press. It comes
from the reference's manual, and PALETTE keeps it.

## 3. What changed in PALETTE's controls

| Control | Before | Now |
| --- | --- | --- |
| The right footswitch | The next preset. Held: bypass | Bypass and engage. Held: the presets |
| The left footswitch | The preset before. Held: bypass | Tap tempo |
| The presets | The footswitches, at any time | Hold the right footswitch for the list. There the footswitches step, the knob scrolls, and AMP, MOD, DLY, and CAB pick a bank |
| The order | FX picks the selected module up, the knob moves it | FX, then the four module buttons in the wanted order |
| Bypass | All four modules | All four, or the set chosen in the order mode |
| After an effect change | The tile shows the new name | The five effects are listed for a moment, the new one lit |
