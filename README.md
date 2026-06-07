# mojmidi4 Arduino sketch

## Library dependencies

Install these Arduino libraries before compiling:

- `Control_Surface`
- `U8g2`
- `OneButton`
- `HC4067`
- `ResponsiveAnalogRead`
- `AiEsp32RotaryEncoder`


## MIDI note edit mode

Long-press encoder button 2 to enter or exit MIDI note edit mode. In this mode,
press one of the 16 mux buttons to load that pad's current settings, turn
encoder 1 to choose the pad message type (`NOTE`, `CC`, or `PC`), and turn
encoder 2 to choose the MIDI value. When the selected pad type is `CC`, turn
potentiometer 1 to choose the CC press value/velocity that will be saved with
that pad. Moving any other potentiometer selects that pot instead; pots stay as
`CC`, and encoder 2 edits the pot's CC number. Single-click an encoder button
to select that encoder's CC number for editing,
and double-click encoder button 2 to save the selected target for the current
controller channel/bank to ESP32 non-volatile memory. Repeat the same workflow on
channels 1, 2, 3, and 4 to store different pad, pot, and encoder mappings per
channel.

This sketch is intended to be compiled from a folder that contains **one** active
`.ino` file: `mojmidi4.ino`.

Header tabs such as `graphics.h` and `display.h` are part of this sketch and are
included by `mojmidi4.ino`; they do not count as duplicate Arduino sketch files.

The Arduino IDE compiles every `.ino` tab/file in the sketch folder into the same
program. If an old copy such as `mojmidi4_1.ino`, `mojmidi4_backup.ino`, or any
other duplicate sketch file remains in the same folder, the build will fail with
`redefinition` errors for globals such as `midi`, `numButtons`, `display`,
`setup()`, and `loop()`.

If you see errors like:

```text
redefinition of 'cs::BluetoothMIDI_Interface midi'
redefinition of 'void setup()'
redefinition of 'void loop()'
```

remove the extra `.ino` copy from the sketch folder or rename it to an extension
that the Arduino IDE does not compile, for example:

```text
mojmidi4_1.ino.bak
```

After removing the duplicate tab/file, compile `mojmidi4.ino` again.
