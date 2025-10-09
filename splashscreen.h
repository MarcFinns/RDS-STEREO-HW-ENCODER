#ifndef SPLASHSCREEN_H
#define SPLASHSCREEN_H

#include <Arduino.h>

// Splash screen image data - 300x142 pixels, RGB565 format
extern const unsigned short PROGMEM LOGO_300[42600];

// Image dimensions
#define SPLASH_IMAGE_WIDTH 300
#define SPLASH_IMAGE_HEIGHT 142

#endif // SPLASHSCREEN_H