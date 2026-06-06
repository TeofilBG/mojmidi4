#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <Arduino.h>
#include <U8g2lib.h>

namespace Graphics {
  const uint8_t TITLE_X = 24;
  const uint8_t TITLE_Y = 7;
  const uint8_t CHANNEL_X = 38;
  const uint8_t CHANNEL_Y = 23;
  const uint8_t ENC1_X = 1;
  const uint8_t ENC2_X = 86;
  const uint8_t ENCODER_Y = 31;

  inline void drawStatusScreen(U8G2 &oled, const char *channelText, const char *enc1Text, const char *enc2Text) {
    oled.clearBuffer();

    oled.setFont(u8g2_font_5x7_tf);
    oled.drawStr(TITLE_X, TITLE_Y, "EUCALIPTUS MIDI");

    oled.setFont(u8g2_font_10x20_tf);
    oled.drawStr(CHANNEL_X, CHANNEL_Y, channelText);

    oled.setFont(u8g2_font_5x7_tf);
    oled.drawStr(ENC1_X, ENCODER_Y, enc1Text);
    oled.drawStr(ENC2_X, ENCODER_Y, enc2Text);

    oled.sendBuffer();
  }
}

#endif
