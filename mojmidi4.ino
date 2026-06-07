#include <Arduino.h>
#include <Control_Surface.h>
#include "HC4067.h"
#include <ResponsiveAnalogRead.h>
#include <AiEsp32RotaryEncoder.h>
#include <OneButton.h>
#include <Preferences.h>
#include "display.h"

// Set to 1 to enable Serial debug output
#define DEBUG 0

// Bluetooth MIDI interface provided by the Control Surface library
BluetoothMIDI_Interface midi;

// Define the number of direct buttons and their pins
const int numButtons = 8;
const int buttonPins[numButtons] = {12, 13, 19, 23, 32, 33, 34, 35};

// Define the MIDI notes for the direct buttons
const int midiNotes[numButtons] = {0, 1, 2, 3, 4, 5, 6, 7};

// Variables to store the current and previous states of the direct buttons
int buttonStates[numButtons] = {0};
int lastButtonStates[numButtons] = {0};

// Define the HC4067 multiplexer with the appropriate control pins
HC4067 mp(18, 5, 17, 16); // S0, S1, S2, S3 pins

const int muxInputPin = 0;
uint16_t previousMuxValues = 0;

enum MuxMessageType {
  MUX_MESSAGE_NOTE = 0,
  MUX_MESSAGE_CONTROL_CHANGE = 1,
  MUX_MESSAGE_PROGRAM_CHANGE = 2,
  NUM_MUX_MESSAGE_TYPES = 3
};

enum EditTargetType {
  EDIT_TARGET_NONE = 0,
  EDIT_TARGET_PAD = 1,
  EDIT_TARGET_POT = 2,
  EDIT_TARGET_ENCODER = 3
};

// Define the default MIDI values for the multiplexer buttons
const int defaultMuxMidiNotes[16] = {72, 73, 74, 75, 68, 69, 70, 71, 64, 65, 66, 67, 60, 61, 62, 63};
int muxMidiNotes[16] = {72, 73, 74, 75, 68, 69, 70, 71, 64, 65, 66, 67, 60, 61, 62, 63};
int muxMessageTypes[16] = {MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE,
                          MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE,
                          MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE,
                          MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE};

const int minEditableMidiNote = 21;
const int maxEditableMidiNote = 108;
const char midiNoteStorageNamespace[] = "mojmidi";

Preferences midiNotePreferences;

// Potentiometer signal pin
const int potInputPin = 2;

// ResponsiveAnalogRead objects for potentiometers (array replaces pot0..pot3)
ResponsiveAnalogRead pots[4] = {
  ResponsiveAnalogRead(potInputPin, true),
  ResponsiveAnalogRead(potInputPin, true),
  ResponsiveAnalogRead(potInputPin, true),
  ResponsiveAnalogRead(potInputPin, true)
};

// Store previous mapped MIDI CC values (0-127) for change detection
int previousMidiCCValues[4] = {-1, -1, -1, -1};

// Define default MIDI CC numbers for each potentiometer
const int defaultMidiCCNumbers[4] = {1, 2, 3, 4};
int midiCCNumbers[4] = {1, 2, 3, 4};

const int minEditableMidiController = 0;
const int maxEditableMidiController = 127;
const int potSelectThreshold = 64;
int lastEditPotValues[4] = {0, 0, 0, 0};

const int defaultEncoderCCNumbers[2] = {10, 11};
int encoderCCNumbers[2] = {10, 11};

// Define independent buttons for MIDI channel switching
const int channelButton1 = 15;
const int channelButton2 = 4;

OneButton encoderButton1(channelButton1, true, true);
OneButton encoderButton2(channelButton2, true, true);

// Current MIDI channel (0-3)
int midiChannel = 0;

bool midiNoteEditMode = false;
bool midiNoteMappingsSaved = false;
int selectedMidiNote = 60;
int selectedMessageType = MUX_MESSAGE_NOTE;
int selectedEditTarget = EDIT_TARGET_NONE;
int editedMuxChannel = -1;
int editedPotChannel = -1;
int editedEncoderChannel = -1;
int lastEditEncoder1Position = 0;
int lastEditEncoder2Position = 0;

// Rotary Encoder Pins and Setup
#define ROTARY_ENCODER_A_PIN_1 26
#define ROTARY_ENCODER_B_PIN_1 25
#define ROTARY_ENCODER_A_PIN_2 27
#define ROTARY_ENCODER_B_PIN_2 14
#define ROTARY_ENCODER_STEPS 6

AiEsp32RotaryEncoder rotaryEncoder1(ROTARY_ENCODER_A_PIN_1, ROTARY_ENCODER_B_PIN_1, -1, -1, ROTARY_ENCODER_STEPS);
AiEsp32RotaryEncoder rotaryEncoder2(ROTARY_ENCODER_A_PIN_2, ROTARY_ENCODER_B_PIN_2, -1, -1, ROTARY_ENCODER_STEPS);

int encoder1Values[4] = {64, 64, 64, 64};
int encoder2Values[4] = {64, 64, 64, 64};

// ISR for rotary encoders
void IRAM_ATTR readEncoderISR1() {
  rotaryEncoder1.readEncoder_ISR();
}

void IRAM_ATTR readEncoderISR2() {
  rotaryEncoder2.readEncoder_ISR();
}

Channel midiChannelFromZeroBased(int zeroBasedChannel) {
  return Channel(static_cast<uint8_t>(zeroBasedChannel));
}

void sendMidiMessage(MIDIMessageType type, int zeroBasedChannel, int data1, int data2) {
  midi.sendChannelMessage(type, midiChannelFromZeroBased(zeroBasedChannel), data1, data2);
  midi.sendNow();
}

const char *muxMessageTypeLabel(int messageType) {
  switch (messageType) {
    case MUX_MESSAGE_CONTROL_CHANGE:
      return "CC";
    case MUX_MESSAGE_PROGRAM_CHANGE:
      return "PC";
    case MUX_MESSAGE_NOTE:
    default:
      return "NOTE";
  }
}

int wrapMuxMessageType(int messageType) {
  while (messageType < 0) messageType += NUM_MUX_MESSAGE_TYPES;
  return messageType % NUM_MUX_MESSAGE_TYPES;
}

int valueMinimumForType(int messageType, int targetType) {
  if (targetType == EDIT_TARGET_POT || targetType == EDIT_TARGET_ENCODER) return minEditableMidiController;
  if (messageType == MUX_MESSAGE_NOTE) return minEditableMidiNote;
  return minEditableMidiController;
}

int valueMaximumForType(int messageType, int targetType) {
  if (targetType == EDIT_TARGET_POT || targetType == EDIT_TARGET_ENCODER) return maxEditableMidiController;
  if (messageType == MUX_MESSAGE_NOTE) return maxEditableMidiNote;
  return maxEditableMidiController;
}

int selectedValueMinimum() {
  return valueMinimumForType(selectedMessageType, selectedEditTarget);
}

int selectedValueMaximum() {
  return valueMaximumForType(selectedMessageType, selectedEditTarget);
}

int constrainSelectedValue(int value) {
  return constrain(value, selectedValueMinimum(), selectedValueMaximum());
}

const char *currentEditTypeLabel() {
  if (selectedEditTarget == EDIT_TARGET_POT || selectedEditTarget == EDIT_TARGET_ENCODER) return "CC";
  return muxMessageTypeLabel(selectedMessageType);
}

void printCurrentMidiEditScreen(bool saved) {
  char targetText[8] = "";
  if (selectedEditTarget == EDIT_TARGET_PAD && editedMuxChannel >= 0) {
    snprintf(targetText, sizeof(targetText), "Pad:%02d", editedMuxChannel + 1);
  } else if (selectedEditTarget == EDIT_TARGET_POT && editedPotChannel >= 0) {
    snprintf(targetText, sizeof(targetText), "Pot:%02d", editedPotChannel + 1);
  } else if (selectedEditTarget == EDIT_TARGET_ENCODER && editedEncoderChannel >= 0) {
    snprintf(targetText, sizeof(targetText), "Enc:%02d", editedEncoderChannel + 1);
  }
  printMidiNoteEditScreen(selectedMidiNote, currentEditTypeLabel(), targetText, saved);
}

uint16_t readMuxButtons() {
  uint16_t currentMuxValues = 0;
  for (int channel = 0; channel < 16; channel++) {
    mp.setChannel(channel);
    if (digitalRead(muxInputPin) == LOW) {
      currentMuxValues |= (1 << channel);
    }
  }
  return currentMuxValues;
}

void loadMuxMidiNotes() {
  midiNotePreferences.begin(midiNoteStorageNamespace, true);
  for (int channel = 0; channel < 16; channel++) {
    char key[5];
    snprintf(key, sizeof(key), "n%02d", channel);
    muxMidiNotes[channel] = midiNotePreferences.getUChar(key, defaultMuxMidiNotes[channel]);
    snprintf(key, sizeof(key), "t%02d", channel);
    muxMessageTypes[channel] = wrapMuxMessageType(midiNotePreferences.getUChar(key, MUX_MESSAGE_NOTE));
    muxMidiNotes[channel] = constrain(muxMidiNotes[channel],
                                      valueMinimumForType(muxMessageTypes[channel], EDIT_TARGET_PAD),
                                      valueMaximumForType(muxMessageTypes[channel], EDIT_TARGET_PAD));
  }
  for (int channel = 0; channel < 4; channel++) {
    char key[5];
    snprintf(key, sizeof(key), "c%02d", channel);
    midiCCNumbers[channel] = midiNotePreferences.getUChar(key, defaultMidiCCNumbers[channel]);
    midiCCNumbers[channel] = constrain(midiCCNumbers[channel], minEditableMidiController, maxEditableMidiController);
  }
  for (int channel = 0; channel < 2; channel++) {
    char key[5];
    snprintf(key, sizeof(key), "e%02d", channel);
    encoderCCNumbers[channel] = midiNotePreferences.getUChar(key, defaultEncoderCCNumbers[channel]);
    encoderCCNumbers[channel] = constrain(encoderCCNumbers[channel], minEditableMidiController, maxEditableMidiController);
  }
  selectedEditTarget = EDIT_TARGET_NONE;
  selectedMessageType = MUX_MESSAGE_NOTE;
  midiNotePreferences.end();
}

void saveMuxMidiNotes() {
  if (selectedEditTarget == EDIT_TARGET_PAD && editedMuxChannel >= 0) {
    muxMidiNotes[editedMuxChannel] = constrainSelectedValue(selectedMidiNote);
    muxMessageTypes[editedMuxChannel] = wrapMuxMessageType(selectedMessageType);
  } else if (selectedEditTarget == EDIT_TARGET_POT && editedPotChannel >= 0) {
    midiCCNumbers[editedPotChannel] = constrainSelectedValue(selectedMidiNote);
    selectedMessageType = MUX_MESSAGE_CONTROL_CHANGE;
  } else if (selectedEditTarget == EDIT_TARGET_ENCODER && editedEncoderChannel >= 0) {
    encoderCCNumbers[editedEncoderChannel] = constrainSelectedValue(selectedMidiNote);
    selectedMessageType = MUX_MESSAGE_CONTROL_CHANGE;
  } else {
    printCurrentMidiEditScreen(false);
    return;
  }

  midiNotePreferences.begin(midiNoteStorageNamespace, false);
  for (int channel = 0; channel < 16; channel++) {
    char key[5];
    snprintf(key, sizeof(key), "n%02d", channel);
    midiNotePreferences.putUChar(key, static_cast<uint8_t>(constrain(muxMidiNotes[channel],
                                                                      valueMinimumForType(muxMessageTypes[channel], EDIT_TARGET_PAD),
                                                                      valueMaximumForType(muxMessageTypes[channel], EDIT_TARGET_PAD))));
    snprintf(key, sizeof(key), "t%02d", channel);
    midiNotePreferences.putUChar(key, static_cast<uint8_t>(wrapMuxMessageType(muxMessageTypes[channel])));
  }
  for (int channel = 0; channel < 4; channel++) {
    char key[5];
    snprintf(key, sizeof(key), "c%02d", channel);
    midiNotePreferences.putUChar(key, static_cast<uint8_t>(constrain(midiCCNumbers[channel], minEditableMidiController, maxEditableMidiController)));
  }
  for (int channel = 0; channel < 2; channel++) {
    char key[5];
    snprintf(key, sizeof(key), "e%02d", channel);
    midiNotePreferences.putUChar(key, static_cast<uint8_t>(constrain(encoderCCNumbers[channel], minEditableMidiController, maxEditableMidiController)));
  }
  midiNotePreferences.end();
  midiNoteMappingsSaved = true;
  printCurrentMidiEditScreen(true);
#if DEBUG
  Serial.println("Mux MIDI notes saved");
#endif
}

void enterMidiNoteEditMode() {
  if (midiNoteEditMode) return;

  encoder1Values[midiChannel] = rotaryEncoder1.readEncoder();
  encoder2Values[midiChannel] = rotaryEncoder2.readEncoder();
  selectedEditTarget = EDIT_TARGET_NONE;
  selectedMidiNote = constrain(muxMidiNotes[0], minEditableMidiNote, maxEditableMidiNote);
  selectedMessageType = wrapMuxMessageType(muxMessageTypes[0]);
  editedMuxChannel = -1;
  editedPotChannel = -1;
  editedEncoderChannel = -1;
  midiNoteMappingsSaved = false;
  midiNoteEditMode = true;

  previousMuxValues = readMuxButtons();
  lastEditEncoder1Position = rotaryEncoder1.readEncoder();
  lastEditEncoder2Position = rotaryEncoder2.readEncoder();
  for (int channel = 0; channel < 4; channel++) {
    lastEditPotValues[channel] = readPotentiometer(channel);
  }
  printCurrentMidiEditScreen(false);
#if DEBUG
  Serial.println("MIDI note edit mode entered");
#endif
}

void exitMidiNoteEditMode() {
  if (!midiNoteEditMode) return;

  selectedMidiNote = constrain(selectedMidiNote, minEditableMidiNote, maxEditableMidiNote);
  midiNoteEditMode = false;
  selectedEditTarget = EDIT_TARGET_NONE;
  previousMuxValues = readMuxButtons();
  rotaryEncoder1.setEncoderValue(encoder1Values[midiChannel]);
  rotaryEncoder2.setEncoderValue(encoder2Values[midiChannel]);
  printChannelAndEncoders(midiChannel + 1, encoder1Values[midiChannel], encoder2Values[midiChannel]);
#if DEBUG
  Serial.println("MIDI note edit mode exited");
#endif
}

void toggleMidiNoteEditMode() {
  if (midiNoteEditMode) {
    exitMidiNoteEditMode();
  } else {
    enterMidiNoteEditMode();
  }
}

void selectEncoderForEdit(int encoderChannel) {
  selectedEditTarget = EDIT_TARGET_ENCODER;
  editedEncoderChannel = encoderChannel;
  editedMuxChannel = -1;
  editedPotChannel = -1;
  selectedMessageType = MUX_MESSAGE_CONTROL_CHANGE;
  selectedMidiNote = constrain(encoderCCNumbers[encoderChannel], minEditableMidiController, maxEditableMidiController);
  lastEditEncoder1Position = rotaryEncoder1.readEncoder();
  lastEditEncoder2Position = rotaryEncoder2.readEncoder();
  midiNoteMappingsSaved = false;
  printCurrentMidiEditScreen(false);
#if DEBUG
  Serial.print("Encoder "); Serial.print(encoderChannel);
  Serial.print(" current CC "); Serial.println(selectedMidiNote);
#endif
}

void switchMidiChannel(int direction) {
  encoder1Values[midiChannel] = rotaryEncoder1.readEncoder();
  encoder2Values[midiChannel] = rotaryEncoder2.readEncoder();
  midiChannel = (midiChannel + direction + 4) % 4;
  rotaryEncoder1.setEncoderValue(encoder1Values[midiChannel]);
  rotaryEncoder2.setEncoderValue(encoder2Values[midiChannel]);
#if DEBUG
  Serial.print("MIDI channel -> "); Serial.println(midiChannel);
#endif
  printChannelAndEncoders(midiChannel + 1, encoder1Values[midiChannel], encoder2Values[midiChannel]);
}

void handleEncoderButton1Click() {
  if (midiNoteEditMode) {
    selectEncoderForEdit(0);
  } else {
    switchMidiChannel(1);
  }
}

void handleEncoderButton2Click() {
  if (midiNoteEditMode) {
    selectEncoderForEdit(1);
  } else {
    switchMidiChannel(-1);
  }
}

void handleEncoderButton1DoubleClick() {
#if DEBUG
  Serial.println("Encoder button 1 double click");
#endif
}

void handleEncoderButton2DoubleClick() {
  if (midiNoteEditMode) {
    saveMuxMidiNotes();
  }
#if DEBUG
  else {
    Serial.println("Encoder button 2 double click");
  }
#endif
}

void handleEncoderButton1LongPress() {
#if DEBUG
  Serial.println("Encoder button 1 long press");
#endif
}

void handleEncoderButton2LongPress() {
  toggleMidiNoteEditMode();
}

void setupEncoderButtons() {
  encoderButton1.setDebounceMs(50);
  encoderButton1.setClickMs(400);
  encoderButton1.setPressMs(800);
  encoderButton1.attachClick(handleEncoderButton1Click);
  encoderButton1.attachDoubleClick(handleEncoderButton1DoubleClick);
  encoderButton1.attachLongPressStart(handleEncoderButton1LongPress);

  encoderButton2.setDebounceMs(50);
  encoderButton2.setClickMs(400);
  encoderButton2.setPressMs(800);
  encoderButton2.attachClick(handleEncoderButton2Click);
  encoderButton2.attachDoubleClick(handleEncoderButton2DoubleClick);
  encoderButton2.attachLongPressStart(handleEncoderButton2LongPress);
}

void tickEncoderButtons() {
  encoderButton1.tick();
  encoderButton2.tick();
}

void handleMidiNoteEditMode() {
  int encoder1Position = rotaryEncoder1.readEncoder();
  int encoder2Position = rotaryEncoder2.readEncoder();
  int typeDelta = encoder1Position - lastEditEncoder1Position;
  int valueDelta = encoder2Position - lastEditEncoder2Position;
  lastEditEncoder1Position = encoder1Position;
  lastEditEncoder2Position = encoder2Position;

  if (typeDelta != 0 && selectedEditTarget != EDIT_TARGET_POT && selectedEditTarget != EDIT_TARGET_ENCODER) {
    selectedMessageType = wrapMuxMessageType(selectedMessageType + typeDelta);
    selectedMidiNote = constrainSelectedValue(selectedMidiNote);
    midiNoteMappingsSaved = false;
    printCurrentMidiEditScreen(false);
  }

  if (valueDelta != 0) {
    selectedMidiNote = constrainSelectedValue(selectedMidiNote + valueDelta);
    midiNoteMappingsSaved = false;
    printCurrentMidiEditScreen(false);
  }

  uint16_t currentMuxValues = readMuxButtons();
  for (int channel = 0; channel < 16; channel++) {
    bool previousState = (previousMuxValues >> channel) & 1;
    bool currentState  = (currentMuxValues  >> channel) & 1;

    if (!previousState && currentState) {
      selectedEditTarget = EDIT_TARGET_PAD;
      editedMuxChannel = channel;
      editedPotChannel = -1;
      editedEncoderChannel = -1;
      selectedMessageType = wrapMuxMessageType(muxMessageTypes[channel]);
      selectedMidiNote = constrainSelectedValue(muxMidiNotes[channel]);
      lastEditEncoder1Position = rotaryEncoder1.readEncoder();
      lastEditEncoder2Position = rotaryEncoder2.readEncoder();
      midiNoteMappingsSaved = false;
      printCurrentMidiEditScreen(false);
#if DEBUG
      Serial.print("Mux Button "); Serial.print(channel);
      Serial.print(" current value "); Serial.print(selectedMidiNote);
      Serial.print(" type "); Serial.println(muxMessageTypeLabel(selectedMessageType));
#endif
    }
  }

  for (int channel = 0; channel < 4; channel++) {
    int currentPotValue = readPotentiometer(channel);
    if (abs(currentPotValue - lastEditPotValues[channel]) >= potSelectThreshold) {
      selectedEditTarget = EDIT_TARGET_POT;
      editedPotChannel = channel;
      editedMuxChannel = -1;
      editedEncoderChannel = -1;
      selectedMessageType = MUX_MESSAGE_CONTROL_CHANGE;
      selectedMidiNote = constrain(midiCCNumbers[channel], minEditableMidiController, maxEditableMidiController);
      lastEditEncoder1Position = rotaryEncoder1.readEncoder();
      lastEditEncoder2Position = rotaryEncoder2.readEncoder();
      midiNoteMappingsSaved = false;
      printCurrentMidiEditScreen(false);
#if DEBUG
      Serial.print("Pot "); Serial.print(channel);
      Serial.print(" current CC "); Serial.println(selectedMidiNote);
#endif
    }
    lastEditPotValues[channel] = currentPotValue;
  }

  previousMuxValues = currentMuxValues;
}

// Read a single potentiometer sample via the mux (ResponsiveAnalogRead handles smoothing)
uint16_t readPotentiometer(int channel) {
  mp.setChannel(channel);
  uint16_t value = analogRead(potInputPin);
  if (value < 204) value = 0; // Below 5% of 12-bit range -> treat as 0
  return value;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Initializing Bluetooth...");
  midi.setName("EUCALIPTUS MIDI RED"); ///////////////////////////////////////////////// NAME THE MIDI CONTROLLER
  midi.begin();
  Serial.println("Waiting for connections...");

  loadMuxMidiNotes();

  for (int i = 0; i < numButtons; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }

  pinMode(muxInputPin, INPUT);
  setupEncoderButtons();

  analogReadResolution(12);

  for (int i = 0; i < 4; i++) {
    pots[i].setAnalogResolution(4096);
  }

  pinMode(ROTARY_ENCODER_A_PIN_1, INPUT_PULLUP);
  pinMode(ROTARY_ENCODER_B_PIN_1, INPUT_PULLUP);
  pinMode(ROTARY_ENCODER_A_PIN_2, INPUT_PULLUP);
  pinMode(ROTARY_ENCODER_B_PIN_2, INPUT_PULLUP);

  rotaryEncoder1.begin();
  rotaryEncoder1.setup(readEncoderISR1);
  rotaryEncoder1.setBoundaries(0, 127, false);
  rotaryEncoder1.setAcceleration(100);
  rotaryEncoder1.setEncoderValue(encoder1Values[midiChannel]);

  rotaryEncoder2.begin();
  rotaryEncoder2.setup(readEncoderISR2);
  rotaryEncoder2.setBoundaries(0, 127, false);
  rotaryEncoder2.setAcceleration(100);
  rotaryEncoder2.setEncoderValue(encoder2Values[midiChannel]);
  previousMuxValues = readMuxButtons();

  initializeDisplay();
  printChannelAndEncoders(midiChannel + 1, encoder1Values[midiChannel], encoder2Values[midiChannel]);
}

void loop() {
  midi.update();
  tickEncoderButtons();

  if (midiNoteEditMode) {
    handleMidiNoteEditMode();
  } else if (midi.isConnected()) {
    // --- Direct buttons ---
    for (int i = 0; i < numButtons; i++) {
      buttonStates[i] = digitalRead(buttonPins[i]);
      if (buttonStates[i] != lastButtonStates[i]) {
        if (buttonStates[i] == LOW) {
          sendMidiMessage(MIDIMessageType::NoteOn, 0, midiNotes[i], 127);
#if DEBUG
          Serial.print("Direct Button "); Serial.print(i); Serial.println(" pressed");
#endif
        } else {
          sendMidiMessage(MIDIMessageType::NoteOff, 0, midiNotes[i], 127);
        }
        lastButtonStates[i] = buttonStates[i];
      }
    }

    // --- Multiplexer buttons ---
    uint16_t currentMuxValues = readMuxButtons();

    for (int channel = 0; channel < 16; channel++) {
      bool previousState = (previousMuxValues >> channel) & 1;
      bool currentState  = (currentMuxValues  >> channel) & 1;

      int muxType = wrapMuxMessageType(muxMessageTypes[channel]);
      int muxValue = constrain(muxMidiNotes[channel],
                               valueMinimumForType(muxType, EDIT_TARGET_PAD),
                               valueMaximumForType(muxType, EDIT_TARGET_PAD));

      if (!previousState && currentState) {
        if (muxType == MUX_MESSAGE_NOTE) {
          sendMidiMessage(MIDIMessageType::NoteOn, midiChannel, muxValue, 127);
        } else if (muxType == MUX_MESSAGE_CONTROL_CHANGE) {
          sendMidiMessage(MIDIMessageType::ControlChange, midiChannel, muxValue, 127);
        } else if (muxType == MUX_MESSAGE_PROGRAM_CHANGE) {
          sendMidiMessage(MIDIMessageType::ProgramChange, midiChannel, muxValue, 0);
        }
#if DEBUG
        Serial.print("Mux Button "); Serial.print(channel);
        Serial.print(" pressed on MIDI channel "); Serial.println(midiChannel);
#endif
      } else if (previousState && !currentState) {
        if (muxType == MUX_MESSAGE_NOTE) {
          sendMidiMessage(MIDIMessageType::NoteOff, midiChannel, muxValue, 127);
        } else if (muxType == MUX_MESSAGE_CONTROL_CHANGE) {
          sendMidiMessage(MIDIMessageType::ControlChange, midiChannel, muxValue, 0);
        }
      }
    }
    previousMuxValues = currentMuxValues;

    // --- Potentiometers ---
    for (int channel = 0; channel < 4; channel++) {
      pots[channel].update(readPotentiometer(channel));
      uint16_t rawValue = pots[channel].getValue();

      // Compare on mapped 0-127 value to avoid sub-step jitter
      int midiCCValue = map(rawValue, 0, 4095, 127, 0);
      if (midiCCValue != previousMidiCCValues[channel]) {
        sendMidiMessage(MIDIMessageType::ControlChange, midiChannel, midiCCNumbers[channel], midiCCValue);
        previousMidiCCValues[channel] = midiCCValue;
#if DEBUG
        Serial.print("Pot "); Serial.print(channel);
        Serial.print(" -> CC "); Serial.print(midiCCNumbers[channel]);
        Serial.print(" val "); Serial.print(midiCCValue);
        Serial.print(" ch "); Serial.println(midiChannel);
#endif
      }
    }

    // --- Rotary encoders ---
    int encoder1Position = rotaryEncoder1.readEncoder();
    int encoder2Position = rotaryEncoder2.readEncoder();

    if (encoder1Position != encoder1Values[midiChannel]) {
      sendMidiMessage(MIDIMessageType::ControlChange, midiChannel, encoderCCNumbers[0], encoder1Position);
      encoder1Values[midiChannel] = encoder1Position;
#if DEBUG
      Serial.print("Enc1: "); Serial.print(encoder1Position);
      Serial.print(" -> CC "); Serial.print(encoderCCNumbers[0]); Serial.print(" ch "); Serial.println(midiChannel);
#endif
      printChannelAndEncoders(midiChannel + 1, encoder1Values[midiChannel], encoder2Values[midiChannel]);
    }

    if (encoder2Position != encoder2Values[midiChannel]) {
      sendMidiMessage(MIDIMessageType::ControlChange, midiChannel, encoderCCNumbers[1], encoder2Position);
      encoder2Values[midiChannel] = encoder2Position;
#if DEBUG
      Serial.print("Enc2: "); Serial.print(encoder2Position);
      Serial.print(" -> CC "); Serial.print(encoderCCNumbers[1]); Serial.print(" ch "); Serial.println(midiChannel);
#endif
      printChannelAndEncoders(midiChannel + 1, encoder1Values[midiChannel], encoder2Values[midiChannel]);
    }
  }

  delay(10);
}
