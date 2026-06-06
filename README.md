# mojmidi4 Arduino sketch

This sketch is intended to be compiled from a folder that contains **one** active
`.ino` file: `mojmidi4.ino`.

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
