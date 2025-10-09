/*
 * =====================================================================================
 *
 *                             ESP32 RDS STEREO ENCODER
 *                                Display Manager
 *
 * =====================================================================================
 *
 * File:         DisplayManager.h
 * Description:  Header for the DisplayManager class - responsible for all
 * visual output including TFT display and VU meters.
 *
 * Main Functions:
 *   • Real-time VU meters with smooth attack/release curves
 *   • Station ID display (PS) with centered layout
 *   • Scrolling radio text (RT) with pixel-perfect animation
 *   • FreeRTOS task integration with queue-based communication
 *
 * Display Architecture:
 *   Primary:  NV3007 428x142 TFT (landscape, vertical stack layout)
 *
 * Task Behavior:
 *   • Runs on Core 1 with priority 2 (high priority for smooth graphics)
 *   • 16ms loop time for smooth VU meter updates (~60 FPS)
 *   • Queue-based PS/RT updates from CommunicationManager
 *   • Independent VU meter ADC sampling and processing
 *
 * Author:       Claude Code Assistant
 * Created:      2024
 * Version:      2.0
 *
 * =====================================================================================
 */

#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include "SharedTypes.h"
#include "splashscreen.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

/* =====================================================================================
 *                              VU METER DATA STRUCTURES
 * =====================================================================================
 */

/**
 * @brief VU meter channel state structure
 *
 * Holds complete state for one audio channel (Left or Right) including
 * ADC sampling, level calculation, smoothing, peak detection, and display.
 *
 * Sampling Process:
 *   1. Read ADC pin multiple times (SAMPLE_WINDOW)
 *   2. Find maximum value in window (peak detection)
 *   3. Map to bar pixels with logarithmic curve
 *   4. Apply attack/release smoothing
 *   5. Track peak markers with hold/decay timing
 */
typedef struct
{
  byte pin;               // ESP32 ADC pin (PIN_L or PIN_R)
  unsigned int maxSample; // Maximum ADC value in current sample window
  byte index;             // Sample count in current window
  int level;              // Target bar level (0 to VU_BAR_WIDTH pixels)
  int avg;                // Smoothed display level (attack/release applied)
  int peak;               // Peak marker position (-1 if hidden)
  int row;                // Y coordinate for this channel's bar
  unsigned long peakHoldUntil; // Timestamp when peak marker expires
} Channel;

/* =====================================================================================
 *                              DISPLAY MANAGER CLASS
 * =====================================================================================
 */

/**
 * @brief Main display management class - handles all visual output
 *
 * Responsibilities:
 *   • TFT display: Station ID, scrolling text, VU meters
 *   • VU meters: Real-time audio level monitoring with color gradients
 *   • Queue handling: PS/RT updates from CommunicationManager
 *   • Task management: FreeRTOS integration with proper timing
 *
 * Design Pattern: Static singleton with FreeRTOS task function
 */
class DisplayManager
{
public:
  /* =================================================================================
   *                               PUBLIC INTERFACE
   * =================================================================================
   */

  /**
   * @brief Initialize display hardware and prepare for operation
   *
   * Sets up TFT display, VU meter ADC pins, and displays initial content.
   * Called once during system startup.
   */
  static void init();

  /**
   * @brief Display splash screen with device info
   *
   * Shows RDS STEREO ENCODER title, version, and build date
   * for 4 seconds during system startup.
   */
  static void showSplashScreen();

  /**
   * @brief Main FreeRTOS task function - runs continuously
   * @param parameter Unused FreeRTOS parameter (required by API)
   *
   * Task Loop (16ms cycle for smooth VU meters at ~60 FPS):
   *   1. Check for PS/RT queue updates (non-blocking)
   *   2. Update text display (change detection, no flicker)
   *   3. Handle text scrolling animation
   *   4. Sample VU meter ADC inputs
   *   5. Apply VU meter smoothing and peak detection
   *   6. Redraw VU bars only if changed (optimization)
   */
  static void taskFunction(void *parameter);

private:
  /* =================================================================================
   *                            TFT DISPLAY METHODS
   * =================================================================================
   */

  /**
   * @brief Initialize NV3007 TFT display and draw initial layout
   *
   * Hardware setup:
   *   • Configure SPI bus and display driver
   *   • Set landscape orientation and backlight
   *   • Configure ESP32 ADC for VU meters (12-bit, 0-3.3V range)
   *   • Draw initial station ID, radio text, and empty VU bars
   *   • Initialize timing for text scrolling and VU decay
   */
  static void initializeTFTDisplay();

  /**
   * @brief Update TFT display with new PS/RT data from queue
   * @param data PS/RT data received from CommunicationManager
   *
   * Process:
   *   1. Copy PS/RT strings to internal buffers
   *   2. Convert to String objects for TFT text handling
   *   3. Trigger change detection for flicker-free updates
   */
  static void updateTFTDisplay(const DisplayData &data);

  /**
   * @brief Draw station ID (PS) centered in top area
   *
   * Layout: Full width, Y=20-60, Size 3 font, Cyan color
   * Behavior: Only redraws if station ID actually changed
   */
  static void drawCenteredStationID();

  /**
   * @brief Draw radio text (RT) with scrolling support
   *
   * Layout: 300px width with margins, Y=70-100, Size 2 font, White color
   * Behavior:
   *   • Short text: Display centered, no scrolling
   *   • Long text: Smooth horizontal scroll with seamless looping
   */
  static void drawScrollingText();

  /**
   * @brief Handle text scrolling animation timing
   *
   * Smooth pixel-based scrolling:
   *   • Speed: 2 pixels every 150ms = ~13 pixels/second (~7 FPS)
   *   • Only scrolls text longer than display width
   *   • Seamless wraparound with spacing between repetitions
   */
  static void updateTextScrolling();

  /**
   * @brief Check for text changes and trigger updates
   *
   * Change detection prevents unnecessary redraws and flicker:
   *   • Compare current vs previous station ID
   *   • Compare current vs previous radio text
   *   • Reset scroll position when text changes
   */
  static void updateTextDisplay();

  /**
   * @brief Draw one VU meter bar with color gradient and peak marker
   * @param ch Channel data (left or right)
   * @param newLen Bar length in pixels (0 to VU_BAR_WIDTH)
   * @param newPeak Peak marker position (-1 if hidden)
   *
   * Visual design:
   *   • Color zones: Green (0-70%), Yellow (70-85%), Orange (85-95%), Red
   * (95-100%) • Peak marker: 3px white bar at maximum recent level • Outline:
   * Dark gray border for definition • Position: Full width minus space for L/R
   * labels
   */
  static void drawVUBar(Channel &ch, int newLen, int newPeak, int prevLen, int prevPeak);

  /**
   * @brief Draw dB scale markings between VU bars
   *
   * Draws professional dB reference marks positioned between the left and right
   * VU bars for easy level reference during audio monitoring.
   */
  static void drawVUScale();

  /* =================================================================================
   *                              VU METER METHODS
   * =================================================================================
   */

  /**
   * @brief Read ADC pin with settling time
   * @param pin Arduino analog pin (PIN_L or PIN_R)
   * @return Raw 12-bit ADC value (0-4095)
   *
   * Method:
   *   1. Dummy read for sample-and-hold settling
   *   2. Optional delay for improved sensitivity
   *   3. Actual read for level calculation
   */
  static unsigned int readADC(byte pin);

  /**
   * @brief Convert raw ADC value to bar pixels with logarithmic scaling
   * @param v Raw ADC value (0-4095)
   * @return Bar length in pixels (0 to VU_BAR_WIDTH)
   *
   * Algorithm:
   *   • Normalize: 0-4095 → 0.0-1.0
   *   • Apply gamma curve (0.35) for perceptual scaling
   *   • Scale to bar width with rounding
   *   • Apply noise gate to eliminate flicker
   */
  static int mapToBarLog(unsigned int v);

  /**
   * @brief Get appropriate color for VU bar position
   * @param position Pixel position in bar (0 to VU_BAR_WIDTH)
   * @return 16-bit RGB565 color value
   *
   * Color zones match professional VU meter standards
   */
  static uint16_t getVUColor(int position);

  /**
   * @brief Sample ADC and update channel target level
   * @param ch Channel structure to update
   *
   * Moving maximum filter:
   *   • Collect SAMPLE_WINDOW readings
   *   • Track maximum value in window
   *   • Convert to pixels when window complete
   *   • Apply noise floor gating
   */
  static void updateChannelLevel(Channel &ch);

  /**
   * @brief Apply fast attack smoothing to rising levels
   * @param ch Channel structure to update
   *
   * Fast attack ensures immediate response to audio peaks
   */
  static void smoothRise(Channel &ch);

  /**
   * @brief Apply slow release smoothing to falling levels (both channels)
   *
   * Timer-based decay creates smooth falling VU bars:
   *   • 20ms intervals for natural decay rate
   *   • Separate decay speed from attack speed
   *   • Applied to both channels simultaneously
   */
  static void decayStepIfDue();

  /**
   * @brief Update peak marker position and timing
   * @param ch Channel structure to update
   *
   * Peak hold behavior:
   *   • Latch at new maximum levels
   *   • Hold for 1 second (PEAK_HOLD_MS)
   *   • Clear when bar drops below and time expires
   */
  static void updatePeak(Channel &ch);

  /* =================================================================================
   *                             PRIVATE STATE VARIABLES
   * =================================================================================
   */

  // TFT Display Hardware Objects
  static Arduino_DataBus *bus; // SPI bus interface
  static Arduino_GFX *gfx;     // Graphics driver (NV3007)

  // PS/RT Display Data (received from CommunicationManager)
  static char currentShortString[9]; // Current PS text (8 + null)
  static char currentLongString[65]; // Current RT text (64 + null)

  // TFT Text Display State
  static String stationID;           // Current station ID for display
  static String radioText;           // Current radio text for display
  static String prevStationID;       // Previous station ID (change detection)
  static String prevRadioText;       // Previous radio text (change detection)
  static byte scrollPos;             // Current scroll position (pixels)
  static unsigned long nextScrollAt; // Next scroll update time (milliseconds)
  static bool isDrawingText;         // Flag to prevent concurrent text drawing

  // VU Meter State
  static Channel leftCh;            // Left channel complete state
  static Channel rightCh;           // Right channel complete state
  static unsigned long nextDecayAt; // Next decay step time (milliseconds)
  static int prevLenL, prevLenR;    // Previous bar lengths (change detection)
  static int prevPeakL, prevPeakR; // Previous peak positions (change detection)
};

#endif // DISPLAY_MANAGER_H