#include <Arduino.h>
#include <Control_Surface.h>
#include "HC4067.h"
#include <ResponsiveAnalogRead.h>
#include <AiEsp32RotaryEncoder.h>
#include <U8g2lib.h>
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

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

// Define the MIDI notes for the multiplexer buttons
const int muxMidiNotes[16] = {72, 73, 74, 75, 68, 69, 70, 71, 64, 65, 66, 67, 60, 61, 62, 63};

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

// Define MIDI CC numbers for each potentiometer
const int midiCCNumbers[4] = {1, 2, 3, 4};

// Define independent buttons for MIDI channel switching
const int channelButton1 = 15;
const int channelButton2 = 4;

int channelButton1State = HIGH;
int lastChannelButton1State = HIGH;
int channelButton2State = HIGH;
int lastChannelButton2State = HIGH;

// Current MIDI channel (0-3)
int midiChannel = 0;

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

// OLED Display Setup
#define OLED_I2C_ADDRESS 0x3C
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// Track what's currently shown to avoid redundant redraws
int lastDisplayChannel = -1;
int lastDisplayEnc1 = -1;
int lastDisplayEnc2 = -1;

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

void printChannelAndEncoders(int channel, int enc1Value, int enc2Value) {
  // Only redraw if something changed
  if (channel == lastDisplayChannel && enc1Value == lastDisplayEnc1 && enc2Value == lastDisplayEnc2) return;
  lastDisplayChannel = channel;
  lastDisplayEnc1 = enc1Value;
  lastDisplayEnc2 = enc2Value;

  char channelText[6];
  char enc1Text[13];
  char enc2Text[13];
  snprintf(channelText, sizeof(channelText), "CH:%02d", channel);
  snprintf(enc1Text, sizeof(enc1Text), "ENC1:%d", enc1Value);
  snprintf(enc2Text, sizeof(enc2Text), "ENC2:%d", enc2Value);

  display.clearBuffer();

  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(24, 7, "EUCALIPTUS MIDI");

  display.setFont(u8g2_font_10x20_tf);
  display.drawStr(38, 23, channelText);

  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(1, 31, enc1Text);
  display.drawStr(86, 31, enc2Text);

  display.sendBuffer();
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

  for (int i = 0; i < numButtons; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }

  pinMode(muxInputPin, INPUT);
  pinMode(channelButton1, INPUT_PULLUP);
  pinMode(channelButton2, INPUT_PULLUP);

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

  display.setI2CAddress(OLED_I2C_ADDRESS * 2);
  if (!display.begin()) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearBuffer();
  printChannelAndEncoders(midiChannel + 1, encoder1Values[midiChannel], encoder2Values[midiChannel]);
}

void loop() {
  midi.update();

  if (midi.isConnected()) {
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
    // Fix: set bit at position 'channel' so channel N maps to bit N
    uint16_t currentMuxValues = 0;
    for (int channel = 0; channel < 16; channel++) {
      mp.setChannel(channel);
      if (digitalRead(muxInputPin) == LOW) {
        currentMuxValues |= (1 << channel);
      }
    }

    for (int channel = 0; channel < 16; channel++) {
      bool previousState = (previousMuxValues >> channel) & 1;
      bool currentState  = (currentMuxValues  >> channel) & 1;

      if (!previousState && currentState) {
        sendMidiMessage(MIDIMessageType::NoteOn, midiChannel, muxMidiNotes[channel], 127);
#if DEBUG
        Serial.print("Mux Button "); Serial.print(channel);
        Serial.print(" pressed on MIDI channel "); Serial.println(midiChannel);
#endif
      } else if (previousState && !currentState) {
        sendMidiMessage(MIDIMessageType::NoteOff, midiChannel, muxMidiNotes[channel], 127);
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

    // --- MIDI channel switching ---
    channelButton1State = digitalRead(channelButton1);
    channelButton2State = digitalRead(channelButton2);

    if (channelButton1State == LOW && lastChannelButton1State == HIGH) {
      encoder1Values[midiChannel] = rotaryEncoder1.readEncoder();
      encoder2Values[midiChannel] = rotaryEncoder2.readEncoder();
      midiChannel = (midiChannel + 1) % 4;
      rotaryEncoder1.setEncoderValue(encoder1Values[midiChannel]);
      rotaryEncoder2.setEncoderValue(encoder2Values[midiChannel]);
#if DEBUG
      Serial.print("MIDI channel -> "); Serial.println(midiChannel);
#endif
      printChannelAndEncoders(midiChannel + 1, encoder1Values[midiChannel], encoder2Values[midiChannel]);
      delay(200); // debounce
    }

    if (channelButton2State == LOW && lastChannelButton2State == HIGH) {
      encoder1Values[midiChannel] = rotaryEncoder1.readEncoder();
      encoder2Values[midiChannel] = rotaryEncoder2.readEncoder();
      midiChannel = (midiChannel - 1 + 4) % 4;
      rotaryEncoder1.setEncoderValue(encoder1Values[midiChannel]);
      rotaryEncoder2.setEncoderValue(encoder2Values[midiChannel]);
#if DEBUG
      Serial.print("MIDI channel -> "); Serial.println(midiChannel);
#endif
      printChannelAndEncoders(midiChannel + 1, encoder1Values[midiChannel], encoder2Values[midiChannel]);
      delay(200); // debounce
    }

    lastChannelButton1State = channelButton1State;
    lastChannelButton2State = channelButton2State;

    // --- Rotary encoders ---
    int encoder1Position = rotaryEncoder1.readEncoder();
    int encoder2Position = rotaryEncoder2.readEncoder();

    if (encoder1Position != encoder1Values[midiChannel]) {
      sendMidiMessage(MIDIMessageType::ControlChange, midiChannel, 10, encoder1Position);
      encoder1Values[midiChannel] = encoder1Position;
#if DEBUG
      Serial.print("Enc1: "); Serial.print(encoder1Position);
      Serial.print(" -> CC10 ch "); Serial.println(midiChannel);
#endif
      printChannelAndEncoders(midiChannel + 1, encoder1Values[midiChannel], encoder2Values[midiChannel]);
    }

    if (encoder2Position != encoder2Values[midiChannel]) {
      sendMidiMessage(MIDIMessageType::ControlChange, midiChannel, 11, encoder2Position);
      encoder2Values[midiChannel] = encoder2Position;
#if DEBUG
      Serial.print("Enc2: "); Serial.print(encoder2Position);
      Serial.print(" -> CC11 ch "); Serial.println(midiChannel);
#endif
      printChannelAndEncoders(midiChannel + 1, encoder1Values[midiChannel], encoder2Values[midiChannel]);
    }
  }

  delay(10);
}
