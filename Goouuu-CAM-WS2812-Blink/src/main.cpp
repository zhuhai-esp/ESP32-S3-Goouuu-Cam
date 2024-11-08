#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#define PIN_PIXS 48
#define PIX_NUM 1

Adafruit_NeoPixel pixels(PIX_NUM, PIN_PIXS, NEO_GRB + NEO_KHZ800);

void setup() {
  pixels.begin();
  pixels.setBrightness(8);
}

void loop() {
  for (uint16_t i = 0; i < 65536; i += 30) {
    pixels.rainbow(i, -1, 255, 255);
    pixels.show();
  }
}