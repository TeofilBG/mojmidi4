#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <Arduino.h>
#include <U8g2lib.h>

namespace Graphics {
  const uint8_t TITLE_X = 17;
  const uint8_t TITLE_Y = 12;
  const uint8_t CHANNEL_X = 38;
  const uint8_t CHANNEL_Y = 37;
  const uint8_t ENC1_X = 1;
  const uint8_t ENC2_X = 80;
  const uint8_t ENCODER_Y = 58;
  const uint8_t EDIT_LABEL_X = 1;
  const uint8_t EDIT_LABEL_Y = 22;
  const uint8_t EDIT_NOTE_X = 82;
  const uint8_t EDIT_NOTE_Y = 43;
  const uint8_t EDIT_STATUS_X = 1;
  const uint8_t EDIT_STATUS_Y = 60;
  const uint8_t EDIT_TYPE_X = 98;
  const uint8_t EDIT_TYPE_Y = 58;

  inline void drawStatusScreen(U8G2 &oled, const char *channelText, const char *enc1Text, const char *enc2Text) {
    oled.clearBuffer();

    oled.setFont(u8g2_font_6x12_tf);
    oled.drawStr(TITLE_X, TITLE_Y, "EUCALIPTUS MIDI");

    oled.setFont(u8g2_font_10x20_tf);
    oled.drawStr(CHANNEL_X, CHANNEL_Y, channelText);

    oled.setFont(u8g2_font_6x12_tf);
    oled.drawStr(ENC1_X, ENCODER_Y, enc1Text);
    oled.drawStr(ENC2_X, ENCODER_Y, enc2Text);

    oled.sendBuffer();
  }

  inline void drawMidiNoteEditScreen(U8G2 &oled, const char *noteText, const char *typeText, const char *statusText) {
    oled.clearBuffer();

    oled.setFont(u8g2_font_7x13_tf);
    oled.drawStr(EDIT_LABEL_X, EDIT_LABEL_Y, "MIDI val");

    oled.setFont(u8g2_font_10x20_tf);
    oled.drawStr(EDIT_NOTE_X, EDIT_NOTE_Y, noteText);

    oled.setFont(u8g2_font_6x12_tf);
    oled.drawStr(EDIT_STATUS_X, EDIT_STATUS_Y, statusText);
    oled.drawStr(EDIT_TYPE_X, EDIT_TYPE_Y, typeText);

    oled.sendBuffer();
  }
}

#endif
