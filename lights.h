#ifndef LIGHTS_H
#define LIGHTS_H

#include <Arduino.h>
#include <FastLED.h>

#define STATUS_LED_PIN 43

const int statusLedCount = 16;
const uint8_t statusLedPassiveBrightness = 76;
const uint8_t statusLedActiveBrightness = 255;
const uint8_t startupRainbowDelayMs = 8;

static CRGB statusLed[statusLedCount];

inline void initializeStatusLed() {
  FastLED.addLeds<WS2812B, STATUS_LED_PIN, GRB>(statusLed, statusLedCount);
  FastLED.clear(true);
}

inline CRGB channelStatusColor(int midiChannel, uint8_t brightness) {
  switch (midiChannel) {
    case 1:
      return CRGB(brightness, 0, 0);
    case 2:
      return CRGB(0, brightness, 0);
    case 3:
      return CRGB(brightness, 0, brightness);
    case 0:
    default:
      return CRGB(0, 0, brightness);
  }
}

inline void runStartupRainbow() {
  for (uint8_t hue = 0; hue < 255; hue += 8) {
    fill_rainbow(statusLed, statusLedCount, hue, 255 / statusLedCount);
    FastLED.show();
    delay(startupRainbowDelayMs);
  }
  FastLED.clear(true);
}

inline void updateHallPadStatusLeds(uint16_t hallPadStates, int midiChannel) {
  for (int channel = 0; channel < statusLedCount; channel++) {
    bool isPressed = (hallPadStates >> channel) & 1;
    uint8_t brightness = isPressed ? statusLedActiveBrightness : statusLedPassiveBrightness;
    statusLed[channel] = channelStatusColor(midiChannel, brightness);
  }
  FastLED.show();
}

#endif
