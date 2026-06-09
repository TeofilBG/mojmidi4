#include <Arduino.h>
#include <Control_Surface.h>
#include <ResponsiveAnalogRead.h>
#include <AiEsp32RotaryEncoder.h>
#include <OneButton.h>
#include <Preferences.h>
#include "display.h"

// Set to 1 to enable Serial debug output
#define DEBUG 0

// Bluetooth MIDI interface provided by the Control Surface library
BluetoothMIDI_Interface midi;

// XIAO ESP32S3 Plus shared MUX select pins
#define MUX_S0 1
#define MUX_S1 2
#define MUX_S2 3
#define MUX_S3 4

// XIAO ESP32S3 Plus MUX COM/SIG pins
#define MUX1_SIG 7   // 16 analog Hall-effect pad switches
#define MUX2_SIG 8   // 6 Potentiometers
#define MUX3_SIG 9   // 10 Digital buttons

const int numMuxPads = 16;
const int numPots = 6;
const int numButtons = 10;

// Define the MIDI notes for the 10 digital buttons on MUX 3
const int midiNotes[numButtons] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

uint16_t previousMuxValues = 0;
uint16_t previousButtonMuxValues = 0;

const int hallTriggerThreshold = 1500;
const int hallReleaseThreshold = 1650;
const int hallMinVelocityDelta = 5;
const int hallMaxVelocityDelta = 200;
int muxPadLastValues[numMuxPads] = {0};
int muxPadMaxDeltas[numMuxPads] = {0};
int currentMuxPadVelocities[numMuxPads] = {0};

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

const int numMidiBanks = 4;
const int defaultMuxCCVelocity = 127;

// Define the default MIDI values for the multiplexer buttons
const int defaultMuxMidiNotes[numMuxPads] = {72, 73, 74, 75, 68, 69, 70, 71, 64, 65, 66, 67, 60, 61, 62, 63};
int muxMidiNotes[numMidiBanks][numMuxPads] = {
  {72, 73, 74, 75, 68, 69, 70, 71, 64, 65, 66, 67, 60, 61, 62, 63},
  {72, 73, 74, 75, 68, 69, 70, 71, 64, 65, 66, 67, 60, 61, 62, 63},
  {72, 73, 74, 75, 68, 69, 70, 71, 64, 65, 66, 67, 60, 61, 62, 63},
  {72, 73, 74, 75, 68, 69, 70, 71, 64, 65, 66, 67, 60, 61, 62, 63}
};
int muxMessageTypes[numMidiBanks][numMuxPads] = {
  {MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE,
   MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE,
   MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE,
   MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE},
  {MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE,
   MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE,
   MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE,
   MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE},
  {MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE,
   MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE,
   MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE,
   MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE},
  {MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE,
   MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE,
   MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE,
   MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE, MUX_MESSAGE_NOTE}
};
int muxCCVelocities[numMidiBanks][numMuxPads] = {
  {127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127},
  {127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127},
  {127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127},
  {127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127}
};

const int minEditableMidiNote = 21;
const int maxEditableMidiNote = 108;
const char midiNoteStorageNamespace[] = "mojmidi";

Preferences midiNotePreferences;

// ResponsiveAnalogRead objects for the 6 potentiometers on MUX 2
ResponsiveAnalogRead pots[numPots] = {
  ResponsiveAnalogRead(MUX2_SIG, true),
  ResponsiveAnalogRead(MUX2_SIG, true),
  ResponsiveAnalogRead(MUX2_SIG, true),
  ResponsiveAnalogRead(MUX2_SIG, true),
  ResponsiveAnalogRead(MUX2_SIG, true),
  ResponsiveAnalogRead(MUX2_SIG, true)
};

// Store previous mapped MIDI CC values (0-127) for change detection
int previousMidiCCValues[numPots] = {-1, -1, -1, -1, -1, -1};

// Define default MIDI CC numbers for each potentiometer
const int defaultMidiCCNumbers[numPots] = {1, 2, 3, 4, 5, 6};
int midiCCNumbers[numMidiBanks][numPots] = {
  {1, 2, 3, 4, 5, 6},
  {1, 2, 3, 4, 5, 6},
  {1, 2, 3, 4, 5, 6},
  {1, 2, 3, 4, 5, 6}
};

const int minEditableMidiController = 0;
const int maxEditableMidiController = 127;
const int potSelectThreshold = 64;
int lastEditPotValues[numPots] = {0, 0, 0, 0, 0, 0};

const int defaultEncoderCCNumbers[2] = {10, 11};
int encoderCCNumbers[numMidiBanks][2] = {
  {10, 11},
  {10, 11},
  {10, 11},
  {10, 11}
};

// Define independent buttons for MIDI channel switching
const int channelButton1 = 41;
const int channelButton2 = 42;

OneButton encoderButton1(channelButton1, true, true);
OneButton encoderButton2(channelButton2, true, true);

// Current MIDI channel (0-3)
int midiChannel = 0;

bool midiNoteEditMode = false;
bool midiNoteMappingsSaved = false;
int selectedMidiNote = 60;
int selectedMuxCCVelocity = defaultMuxCCVelocity;
int selectedMessageType = MUX_MESSAGE_NOTE;
int selectedEditTarget = EDIT_TARGET_NONE;
int editedMuxChannel = -1;
int editedPotChannel = -1;
int editedEncoderChannel = -1;
int lastEditEncoder1Position = 0;
int lastEditEncoder2Position = 0;

// Rotary Encoder Pins and Setup
#define ROTARY_ENCODER_A_PIN_1 44
#define ROTARY_ENCODER_B_PIN_1 38
#define ROTARY_ENCODER_A_PIN_2 39
#define ROTARY_ENCODER_B_PIN_2 40
#define ROTARY_ENCODER_STEPS 6

AiEsp32RotaryEncoder rotaryEncoder1(ROTARY_ENCODER_A_PIN_1, ROTARY_ENCODER_B_PIN_1, -1, -1, ROTARY_ENCODER_STEPS);
AiEsp32RotaryEncoder rotaryEncoder2(ROTARY_ENCODER_A_PIN_2, ROTARY_ENCODER_B_PIN_2, -1, -1, ROTARY_ENCODER_STEPS);

int encoder1Values[numMidiBanks] = {64, 64, 64, 64};
int encoder2Values[numMidiBanks] = {64, 64, 64, 64};

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

int constrainMidiDataValue(int value) {
  return constrain(value, minEditableMidiController, maxEditableMidiController);
}

int midiDataValueFromPot(uint16_t rawValue) {
  return constrainMidiDataValue(map(rawValue, 0, 4095, 127, 0));
}

int currentMidiBank() {
  return constrain(midiChannel, 0, numMidiBanks - 1);
}

int currentEncoderCCNumber(int encoderChannel) {
  int safeEncoderChannel = constrain(encoderChannel, 0, 1);
  return constrain(encoderCCNumbers[currentMidiBank()][safeEncoderChannel],
                   minEditableMidiController,
                   maxEditableMidiController);
}

const char *currentEditTypeLabel() {
  if (selectedEditTarget == EDIT_TARGET_POT || selectedEditTarget == EDIT_TARGET_ENCODER) return "CC";
  return muxMessageTypeLabel(selectedMessageType);
}

void printCurrentMidiEditScreen(bool saved) {
  char targetText[20] = "";
  if (selectedEditTarget == EDIT_TARGET_PAD && editedMuxChannel >= 0) {
    if (selectedMessageType == MUX_MESSAGE_CONTROL_CHANGE) {
      snprintf(targetText, sizeof(targetText), "Pad:%02d V:%03d", editedMuxChannel + 1, selectedMuxCCVelocity);
    } else {
      snprintf(targetText, sizeof(targetText), "Pad:%02d", editedMuxChannel + 1);
    }
  } else if (selectedEditTarget == EDIT_TARGET_POT && editedPotChannel >= 0) {
    snprintf(targetText, sizeof(targetText), "Pot:%02d", editedPotChannel + 1);
  } else if (selectedEditTarget == EDIT_TARGET_ENCODER && editedEncoderChannel >= 0) {
    snprintf(targetText, sizeof(targetText), "Enc:%02d", editedEncoderChannel + 1);
  }
  printMidiNoteEditScreen(selectedMidiNote, currentEditTypeLabel(), targetText, saved);
}

void selectMuxChannel(uint8_t channel) {
  digitalWrite(MUX_S0, channel & 0x01);
  digitalWrite(MUX_S1, (channel >> 1) & 0x01);
  digitalWrite(MUX_S2, (channel >> 2) & 0x01);
  digitalWrite(MUX_S3, (channel >> 3) & 0x01);
  delayMicroseconds(5);
}

uint16_t readMuxDigitalInputs(int inputPin, int channelCount) {
  uint16_t currentMuxValues = 0;
  for (int channel = 0; channel < channelCount; channel++) {
    selectMuxChannel(channel);
    if (digitalRead(inputPin) == LOW) {
      currentMuxValues |= (1 << channel);
    }
  }
  return currentMuxValues;
}

uint16_t readMuxPadRaw(int channel) {
  selectMuxChannel(channel);
  return analogRead(MUX1_SIG);
}

void initializeMuxPadTracking() {
  for (int channel = 0; channel < numMuxPads; channel++) {
    muxPadLastValues[channel] = readMuxPadRaw(channel);
    muxPadMaxDeltas[channel] = 0;
    currentMuxPadVelocities[channel] = 0;
  }
}

int muxPadVelocityFromDelta(int maxDelta) {
  return map(constrain(maxDelta, hallMinVelocityDelta, hallMaxVelocityDelta),
             hallMinVelocityDelta,
             hallMaxVelocityDelta,
             1,
             127);
}

uint16_t readMuxHallPads() {
  uint16_t currentMuxValues = 0;
  for (int channel = 0; channel < numMuxPads; channel++) {
    int value = readMuxPadRaw(channel);
    int delta = muxPadLastValues[channel] - value;
    if (delta > muxPadMaxDeltas[channel]) {
      muxPadMaxDeltas[channel] = delta;
    }

    bool wasPressed = (previousMuxValues >> channel) & 1;
    bool isPressed = wasPressed;

    if (!wasPressed && value < hallTriggerThreshold) {
      isPressed = true;
      currentMuxPadVelocities[channel] = muxPadVelocityFromDelta(muxPadMaxDeltas[channel]);
    } else if (wasPressed && value > hallReleaseThreshold) {
      isPressed = false;
      muxPadMaxDeltas[channel] = 0;
      currentMuxPadVelocities[channel] = 0;
    }

    if (isPressed) {
      currentMuxValues |= (1 << channel);
    }

    muxPadLastValues[channel] = value;
  }
  return currentMuxValues;
}

uint16_t readMuxButtons() {
  return readMuxHallPads();
}

uint16_t readDirectButtons() {
  return readMuxDigitalInputs(MUX3_SIG, numButtons);
}

void loadMuxMidiNotes() {
  midiNotePreferences.begin(midiNoteStorageNamespace, true);
  for (int bank = 0; bank < numMidiBanks; bank++) {
    for (int channel = 0; channel < numMuxPads; channel++) {
      char key[7];
      snprintf(key, sizeof(key), "b%dn%02d", bank, channel);
      muxMidiNotes[bank][channel] = midiNotePreferences.getUChar(key, defaultMuxMidiNotes[channel]);
      snprintf(key, sizeof(key), "b%dt%02d", bank, channel);
      muxMessageTypes[bank][channel] = wrapMuxMessageType(midiNotePreferences.getUChar(key, MUX_MESSAGE_NOTE));
      snprintf(key, sizeof(key), "b%dv%02d", bank, channel);
      muxCCVelocities[bank][channel] = constrainMidiDataValue(midiNotePreferences.getUChar(key, defaultMuxCCVelocity));
      muxMidiNotes[bank][channel] = constrain(muxMidiNotes[bank][channel],
                                              valueMinimumForType(muxMessageTypes[bank][channel], EDIT_TARGET_PAD),
                                              valueMaximumForType(muxMessageTypes[bank][channel], EDIT_TARGET_PAD));
    }

    for (int channel = 0; channel < numPots; channel++) {
      char key[7];
      snprintf(key, sizeof(key), "b%dc%02d", bank, channel);
      midiCCNumbers[bank][channel] = midiNotePreferences.getUChar(key, defaultMidiCCNumbers[channel]);
      midiCCNumbers[bank][channel] = constrain(midiCCNumbers[bank][channel], minEditableMidiController, maxEditableMidiController);
    }

    for (int channel = 0; channel < 2; channel++) {
      char key[7];
      snprintf(key, sizeof(key), "b%de%02d", bank, channel);
      encoderCCNumbers[bank][channel] = midiNotePreferences.getUChar(key, defaultEncoderCCNumbers[channel]);
      encoderCCNumbers[bank][channel] = constrain(encoderCCNumbers[bank][channel], minEditableMidiController, maxEditableMidiController);
    }
  }
  selectedEditTarget = EDIT_TARGET_NONE;
  selectedMessageType = MUX_MESSAGE_NOTE;
  midiNotePreferences.end();
}

void saveMuxMidiNotes() {
  if (selectedEditTarget == EDIT_TARGET_PAD && editedMuxChannel >= 0) {
    muxMidiNotes[midiChannel][editedMuxChannel] = constrainSelectedValue(selectedMidiNote);
    muxMessageTypes[midiChannel][editedMuxChannel] = wrapMuxMessageType(selectedMessageType);
    muxCCVelocities[midiChannel][editedMuxChannel] = constrainMidiDataValue(selectedMuxCCVelocity);
  } else if (selectedEditTarget == EDIT_TARGET_POT && editedPotChannel >= 0) {
    midiCCNumbers[midiChannel][editedPotChannel] = constrainSelectedValue(selectedMidiNote);
    selectedMessageType = MUX_MESSAGE_CONTROL_CHANGE;
  } else if (selectedEditTarget == EDIT_TARGET_ENCODER && editedEncoderChannel >= 0) {
    encoderCCNumbers[midiChannel][editedEncoderChannel] = constrainSelectedValue(selectedMidiNote);
    selectedMessageType = MUX_MESSAGE_CONTROL_CHANGE;
  } else {
    printCurrentMidiEditScreen(false);
    return;
  }

  midiNotePreferences.begin(midiNoteStorageNamespace, false);
  for (int bank = 0; bank < numMidiBanks; bank++) {
    for (int channel = 0; channel < numMuxPads; channel++) {
      char key[7];
      snprintf(key, sizeof(key), "b%dn%02d", bank, channel);
      midiNotePreferences.putUChar(key, static_cast<uint8_t>(constrain(muxMidiNotes[bank][channel],
                                                                        valueMinimumForType(muxMessageTypes[bank][channel], EDIT_TARGET_PAD),
                                                                        valueMaximumForType(muxMessageTypes[bank][channel], EDIT_TARGET_PAD))));
      snprintf(key, sizeof(key), "b%dt%02d", bank, channel);
      midiNotePreferences.putUChar(key, static_cast<uint8_t>(wrapMuxMessageType(muxMessageTypes[bank][channel])));
      snprintf(key, sizeof(key), "b%dv%02d", bank, channel);
      midiNotePreferences.putUChar(key, static_cast<uint8_t>(constrainMidiDataValue(muxCCVelocities[bank][channel])));
    }

    for (int channel = 0; channel < numPots; channel++) {
      char key[7];
      snprintf(key, sizeof(key), "b%dc%02d", bank, channel);
      midiNotePreferences.putUChar(key, static_cast<uint8_t>(constrain(midiCCNumbers[bank][channel], minEditableMidiController, maxEditableMidiController)));
    }

    for (int channel = 0; channel < 2; channel++) {
      char key[7];
      snprintf(key, sizeof(key), "b%de%02d", bank, channel);
      midiNotePreferences.putUChar(key, static_cast<uint8_t>(constrain(encoderCCNumbers[bank][channel], minEditableMidiController, maxEditableMidiController)));
    }
  }
  midiNotePreferences.end();
  midiNoteMappingsSaved = true;
  printCurrentMidiEditScreen(true);
#if DEBUG
  Serial.println("MIDI mappings saved");
#endif
}

void enterMidiNoteEditMode() {
  if (midiNoteEditMode) return;

  encoder1Values[midiChannel] = rotaryEncoder1.readEncoder();
  encoder2Values[midiChannel] = rotaryEncoder2.readEncoder();
  selectedEditTarget = EDIT_TARGET_NONE;
  selectedMidiNote = constrain(muxMidiNotes[midiChannel][0], minEditableMidiNote, maxEditableMidiNote);
  selectedMuxCCVelocity = constrainMidiDataValue(muxCCVelocities[midiChannel][0]);
  selectedMessageType = wrapMuxMessageType(muxMessageTypes[midiChannel][0]);
  editedMuxChannel = -1;
  editedPotChannel = -1;
  editedEncoderChannel = -1;
  midiNoteMappingsSaved = false;
  midiNoteEditMode = true;

  previousMuxValues = readMuxButtons();
  lastEditEncoder1Position = rotaryEncoder1.readEncoder();
  lastEditEncoder2Position = rotaryEncoder2.readEncoder();
  for (int channel = 0; channel < numPots; channel++) {
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
  selectedMidiNote = currentEncoderCCNumber(encoderChannel);
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
  midiChannel = (midiChannel + direction + numMidiBanks) % numMidiBanks;
  rotaryEncoder1.setEncoderValue(encoder1Values[midiChannel]);
  rotaryEncoder2.setEncoderValue(encoder2Values[midiChannel]);
  for (int channel = 0; channel < numPots; channel++) {
    previousMidiCCValues[channel] = -1;
  }
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
  for (int channel = 0; channel < numMuxPads; channel++) {
    bool previousState = (previousMuxValues >> channel) & 1;
    bool currentState  = (currentMuxValues  >> channel) & 1;

    if (!previousState && currentState) {
      selectedEditTarget = EDIT_TARGET_PAD;
      editedMuxChannel = channel;
      editedPotChannel = -1;
      editedEncoderChannel = -1;
      selectedMessageType = wrapMuxMessageType(muxMessageTypes[midiChannel][channel]);
      selectedMidiNote = constrainSelectedValue(muxMidiNotes[midiChannel][channel]);
      selectedMuxCCVelocity = constrainMidiDataValue(muxCCVelocities[midiChannel][channel]);
      lastEditEncoder1Position = rotaryEncoder1.readEncoder();
      lastEditEncoder2Position = rotaryEncoder2.readEncoder();
      midiNoteMappingsSaved = false;
      printCurrentMidiEditScreen(false);
#if DEBUG
      Serial.print("Hall Pad "); Serial.print(channel);
      Serial.print(" current value "); Serial.print(selectedMidiNote);
      Serial.print(" type "); Serial.println(muxMessageTypeLabel(selectedMessageType));
#endif
    }
  }

  for (int channel = 0; channel < numPots; channel++) {
    int currentPotValue = readPotentiometer(channel);
    if (abs(currentPotValue - lastEditPotValues[channel]) >= potSelectThreshold) {
      if (selectedEditTarget == EDIT_TARGET_PAD && selectedMessageType == MUX_MESSAGE_CONTROL_CHANGE && channel == 0) {
        selectedMuxCCVelocity = midiDataValueFromPot(currentPotValue);
        midiNoteMappingsSaved = false;
        printCurrentMidiEditScreen(false);
#if DEBUG
        Serial.print("Pad CC velocity -> "); Serial.println(selectedMuxCCVelocity);
#endif
      } else {
        selectedEditTarget = EDIT_TARGET_POT;
        editedPotChannel = channel;
        editedMuxChannel = -1;
        editedEncoderChannel = -1;
        selectedMessageType = MUX_MESSAGE_CONTROL_CHANGE;
        selectedMidiNote = constrain(midiCCNumbers[midiChannel][channel], minEditableMidiController, maxEditableMidiController);
        lastEditEncoder1Position = rotaryEncoder1.readEncoder();
        lastEditEncoder2Position = rotaryEncoder2.readEncoder();
        midiNoteMappingsSaved = false;
        printCurrentMidiEditScreen(false);
#if DEBUG
        Serial.print("Pot "); Serial.print(channel);
        Serial.print(" current CC "); Serial.println(selectedMidiNote);
#endif
      }
    }
    lastEditPotValues[channel] = currentPotValue;
  }

  previousMuxValues = currentMuxValues;
}

// Read a single potentiometer sample via the mux (ResponsiveAnalogRead handles smoothing)
uint16_t readPotentiometer(int channel) {
  selectMuxChannel(channel);
  uint16_t value = analogRead(MUX2_SIG);
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

  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  pinMode(MUX_S3, OUTPUT);
  selectMuxChannel(0);

  pinMode(MUX1_SIG, INPUT);
  pinMode(MUX2_SIG, INPUT);
  pinMode(MUX3_SIG, INPUT_PULLUP);
  setupEncoderButtons();

  analogReadResolution(12);

  for (int i = 0; i < numPots; i++) {
    pots[i].setAnalogResolution(4096);
  }

  initializeMuxPadTracking();

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
  previousButtonMuxValues = readDirectButtons();

  initializeDisplay();
  printChannelAndEncoders(midiChannel + 1, encoder1Values[midiChannel], encoder2Values[midiChannel]);
}

void loop() {
  midi.update();
  tickEncoderButtons();

  if (midiNoteEditMode) {
    handleMidiNoteEditMode();
  } else if (midi.isConnected()) {
    // --- Digital buttons on MUX 3 ---
    uint16_t currentButtonMuxValues = readDirectButtons();
    for (int i = 0; i < numButtons; i++) {
      bool previousState = (previousButtonMuxValues >> i) & 1;
      bool currentState  = (currentButtonMuxValues  >> i) & 1;

      if (!previousState && currentState) {
        sendMidiMessage(MIDIMessageType::NoteOn, 0, midiNotes[i], 127);
#if DEBUG
        Serial.print("Direct MUX Button "); Serial.print(i); Serial.println(" pressed");
#endif
      } else if (previousState && !currentState) {
        sendMidiMessage(MIDIMessageType::NoteOff, 0, midiNotes[i], 127);
      }
    }
    previousButtonMuxValues = currentButtonMuxValues;

    // --- Hall-effect pad switches on MUX 1 ---
    uint16_t currentMuxValues = readMuxButtons();

    for (int channel = 0; channel < numMuxPads; channel++) {
      bool previousState = (previousMuxValues >> channel) & 1;
      bool currentState  = (currentMuxValues  >> channel) & 1;

      int muxType = wrapMuxMessageType(muxMessageTypes[midiChannel][channel]);
      int muxValue = constrain(muxMidiNotes[midiChannel][channel],
                               valueMinimumForType(muxType, EDIT_TARGET_PAD),
                               valueMaximumForType(muxType, EDIT_TARGET_PAD));
      int muxCCVelocity = constrainMidiDataValue(muxCCVelocities[midiChannel][channel]);

      if (!previousState && currentState) {
        if (muxType == MUX_MESSAGE_NOTE) {
          sendMidiMessage(MIDIMessageType::NoteOn, midiChannel, muxValue, currentMuxPadVelocities[channel]);
        } else if (muxType == MUX_MESSAGE_CONTROL_CHANGE) {
          sendMidiMessage(MIDIMessageType::ControlChange, midiChannel, muxValue, muxCCVelocity);
        } else if (muxType == MUX_MESSAGE_PROGRAM_CHANGE) {
          sendMidiMessage(MIDIMessageType::ProgramChange, midiChannel, muxValue, 0);
        }
#if DEBUG
        Serial.print("Hall Pad "); Serial.print(channel);
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
    for (int channel = 0; channel < numPots; channel++) {
      pots[channel].update(readPotentiometer(channel));
      uint16_t rawValue = pots[channel].getValue();

      // Compare on mapped 0-127 value to avoid sub-step jitter
      int midiCCValue = midiDataValueFromPot(rawValue);
      if (midiCCValue != previousMidiCCValues[channel]) {
        sendMidiMessage(MIDIMessageType::ControlChange, midiChannel, midiCCNumbers[midiChannel][channel], midiCCValue);
        previousMidiCCValues[channel] = midiCCValue;
#if DEBUG
        Serial.print("Pot "); Serial.print(channel);
        Serial.print(" -> CC "); Serial.print(midiCCNumbers[midiChannel][channel]);
        Serial.print(" val "); Serial.print(midiCCValue);
        Serial.print(" ch "); Serial.println(midiChannel);
#endif
      }
    }

    // --- Rotary encoders ---
    int encoder1Position = rotaryEncoder1.readEncoder();
    int encoder2Position = rotaryEncoder2.readEncoder();

    if (encoder1Position != encoder1Values[midiChannel]) {
      sendMidiMessage(MIDIMessageType::ControlChange, midiChannel, encoderCCNumbers[midiChannel][0], encoder1Position);
      encoder1Values[midiChannel] = encoder1Position;
#if DEBUG
      Serial.print("Enc1: "); Serial.print(encoder1Position);
      Serial.print(" -> CC "); Serial.print(encoderCCNumbers[midiChannel][0]); Serial.print(" ch "); Serial.println(midiChannel);
#endif
      printChannelAndEncoders(midiChannel + 1, encoder1Values[midiChannel], encoder2Values[midiChannel]);
    }

    if (encoder2Position != encoder2Values[midiChannel]) {
      sendMidiMessage(MIDIMessageType::ControlChange, midiChannel, encoderCCNumbers[midiChannel][1], encoder2Position);
      encoder2Values[midiChannel] = encoder2Position;
#if DEBUG
      Serial.print("Enc2: "); Serial.print(encoder2Position);
      Serial.print(" -> CC "); Serial.print(encoderCCNumbers[midiChannel][1]); Serial.print(" ch "); Serial.println(midiChannel);
#endif
      printChannelAndEncoders(midiChannel + 1, encoder1Values[midiChannel], encoder2Values[midiChannel]);
    }
  }

  delay(10);
}
