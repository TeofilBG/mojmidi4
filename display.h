#ifndef DISPLAY_H
#define DISPLAY_H

#include "graphics.h"
#include <Wire.h>

const uint8_t OLED_I2C_ADDRESS = 0x3C;
const uint8_t OLED_SDA_PIN = 5;
const uint8_t OLED_SCL_PIN = 6;

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// Track what's currently shown to avoid redundant redraws
static int lastDisplayChannel = -1;
static int lastDisplayEnc1 = -1;
static int lastDisplayEnc2 = -1;

inline void initializeDisplay() {
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  display.setI2CAddress(OLED_I2C_ADDRESS * 2);
  if (!display.begin()) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearBuffer();
}

inline void printChannelAndEncoders(int channel, int enc1Value, int enc2Value) {
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

  Graphics::drawStatusScreen(display, channelText, enc1Text, enc2Text);
}

inline void printMidiNoteEditScreen(int noteValue, const char *messageTypeText, const char *targetText, bool saved) {
  lastDisplayChannel = -1;
  lastDisplayEnc1 = -1;
  lastDisplayEnc2 = -1;

  char noteText[4];
  char statusText[20];
  snprintf(noteText, sizeof(noteText), "%03d", noteValue);

  if (saved) {
    snprintf(statusText, sizeof(statusText), "Saved");
  } else if (targetText != nullptr && targetText[0] != '\0') {
    snprintf(statusText, sizeof(statusText), "%s", targetText);
  } else {
    snprintf(statusText, sizeof(statusText), "Select control");
  }

  Graphics::drawMidiNoteEditScreen(display, noteText, messageTypeText, statusText);
}

#endif
