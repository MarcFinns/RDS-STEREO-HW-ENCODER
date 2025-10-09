/*
 * =====================================================================================
 *
 *                             ESP32 RDS STEREO ENCODER
 *                         Display Manager Implementation
 *
 * =====================================================================================
 *
 * File:         DisplayManager.cpp
 * Description:  Implementation of DisplayManager class - handles all visual
 * output including TFT display and VU meters.
 *
 * Key Features:
 *   • Real-time VU meters: Dual-channel audio level monitoring with smooth
 *     attack/release curves, color gradients, and peak markers
 *   • TFT text display: Station ID (PS) and scrolling radio text (RT)
 *   • Change detection: Flicker-free updates only when content actually changes
 *   • Queue integration: Receives PS/RT data from CommunicationManager
 *
 * Performance Optimizations:
 *   • 16ms task loop for smooth ~60 FPS VU meter updates
 *   • Only redraws changed elements to minimize SPI traffic
 *   • Efficient ADC sampling with moving maximum filter
 *   • Logarithmic audio scaling for perceptual accuracy
 *
 * Display Layout (428x142 TFT, vertical stack):
 *   ┌─────────────────────────────────┐ Y=10
 *   │         STATION ID              │ 30px height, cyan, size 3
 *   ├─────────────────────────────────┤ Y=45
 *   │   Scrolling Radio Text...       │ 25px height, white, size 2
 *   ├─────────────────────────────────┤ Y=75
 *   │ L ████████████████████████████  │ 18px height, color gradient
 *   │ R ████████████████████████████  │ 18px height, color gradient
 *   └─────────────────────────────────┘
 *
 * Author:       Claude Code Assistant
 * Created:      2024
 * Version:      2.0
 *
 * =====================================================================================
 */

#include "DisplayManager.h"

/* =====================================================================================
 *                          STATIC MEMBER VARIABLE DEFINITIONS
 * =====================================================================================
 */

// TFT Display Hardware Objects
Arduino_DataBus *DisplayManager::bus = nullptr; // SPI bus interface
Arduino_GFX *DisplayManager::gfx = nullptr;     // NV3007 graphics driver

// PS/RT Display Data (received from CommunicationManager via queue)
char DisplayManager::currentShortString[9] = ""; // Current PS text buffer
char DisplayManager::currentLongString[65] = ""; // Current RT text buffer

// TFT Text Display State Management
String DisplayManager::stationID = "STEREO RDS"; // Default station ID
String DisplayManager::radioText = "Fini 2025";  // Default radio text
String DisplayManager::prevStationID =
    ""; // Previous station ID (change detection)
String DisplayManager::prevRadioText =
    "";                             // Previous radio text (change detection)
byte DisplayManager::scrollPos = 0; // Current scroll position (pixels)
unsigned long DisplayManager::nextScrollAt = 0; // Next scroll animation time
bool DisplayManager::isDrawingText = false; // Race condition protection flag

// VU Meter Channel States (Left and Right audio channels)
Channel DisplayManager::leftCh = {PIN_L, 0,  0,      0,
                                  0,     -1, VU_L_Y, 0}; // Left channel state
Channel DisplayManager::rightCh = {PIN_R, 0,  0,      0,
                                   0,     -1, VU_R_Y, 0}; // Right channel state
unsigned long DisplayManager::nextDecayAt = 0; // Next VU decay step time

// VU Meter Change Detection (prevents unnecessary redraws)
int DisplayManager::prevLenL = 0;   // Previous left bar length
int DisplayManager::prevLenR = 0;   // Previous right bar length
int DisplayManager::prevPeakL = -1; // Previous left peak position
int DisplayManager::prevPeakR = -1; // Previous right peak position

/* =====================================================================================
 *                               PUBLIC INTERFACE
 * =====================================================================================
 */

/**
 * @brief Initialize display hardware and prepare for operation
 *
 * Called once during system startup to set up TFT display.
 * Configures ADC for VU meters and displays initial content.
 */
void DisplayManager::init()
{
  // Initialize TFT display hardware (basic setup only)
  initializeTFTDisplay();

  // Display splash screen for 4 seconds FIRST
  showSplashScreen();
}

/**
 * @brief Main FreeRTOS task function - runs continuously at ~60 FPS
 * @param parameter Unused FreeRTOS parameter (required by API)
 *
 * High-frequency task loop optimized for smooth VU meter animation:
 *   1. Non-blocking queue checks for new PS/RT data
 *   2. Text display updates with change detection (no flicker)
 *   3. Smooth text scrolling animation at ~7 FPS
 *   4. VU meter ADC sampling and level calculation
 *   5. Attack/release smoothing for natural VU meter behavior
 *   6. Peak detection and hold timing
 *   7. Optimized redraw only when VU levels actually change
 *
 * Task Parameters:
 *   • Core: 1 (dedicated to display processing)
 *   • Priority: 2 (high priority for smooth graphics)
 *   • Stack: 4096 bytes
 *   • Loop time: 16ms (~60 FPS for smooth VU meters)
 */
void DisplayManager::taskFunction(void *parameter)
{
  DisplayData receivedData; // Buffer for PS/RT data from queue

  delay(7000);

  // Main task loop - runs forever at high frequency
  while (true)
  {
    // ===== QUEUE PROCESSING =====
    // Check for new PS/RT data from CommunicationManager (non-blocking)
    if (xQueueReceive(displayQueue, &receivedData, 0) == pdTRUE)
    {
      updateTFTDisplay(receivedData); // Update display with new PS/RT data
    }

    // ===== TEXT DISPLAY PROCESSING =====
    // Handle text changes and scrolling animation
    updateTextDisplay();   // Check for text changes, trigger redraws if needed
    updateTextScrolling(); // Handle smooth pixel-based scrolling animation

    // ===== VU METER PROCESSING =====
    // Sample ADC inputs and calculate audio levels
    updateChannelLevel(leftCh);  // Sample left channel ADC
    updateChannelLevel(rightCh); // Sample right channel ADC

    // Apply smoothing for natural VU meter behavior
    smoothRise(leftCh);  // Fast attack for rising levels
    smoothRise(rightCh); // Fast attack for rising levels
    decayStepIfDue();    // Slow release for falling levels (both channels)

    // Update peak markers with hold/decay timing
    updatePeak(leftCh);  // Track left channel peak marker
    updatePeak(rightCh); // Track right channel peak marker

    // ===== OPTIMIZED REDRAW =====
    // Only redraw VU bars when values actually change (prevents unnecessary SPI
    // traffic)

    // Left channel VU bar update
    if (leftCh.avg != prevLenL || leftCh.peak != prevPeakL)
    {
      drawVUBar(leftCh, leftCh.avg, leftCh.peak, prevLenL,
                prevPeakL);    // Pass previous values
      prevLenL = leftCh.avg;   // Cache current length for next comparison
      prevPeakL = leftCh.peak; // Cache current peak for next comparison
    }

    // Right channel VU bar update
    if (rightCh.avg != prevLenR || rightCh.peak != prevPeakR)
    {
      drawVUBar(rightCh, rightCh.avg, rightCh.peak, prevLenR,
                prevPeakR);     // Pass previous values
      prevLenR = rightCh.avg;   // Cache current length for next comparison
      prevPeakR = rightCh.peak; // Cache current peak for next comparison
    }

    // Task timing: 16ms delay for smooth 60 FPS VU meter updates
    vTaskDelay(pdMS_TO_TICKS(16));
  }
}

/* =====================================================================================
 *                            TFT DISPLAY METHODS
 * =====================================================================================
 */

/**
 * @brief Initialize NV3007 TFT display and draw initial layout
 *
 * Complete hardware setup and initial display configuration:
 *   • SPI bus and display driver initialization
 *   • Landscape orientation and backlight control
 *   • ESP32 ADC configuration for VU meters (12-bit, full range)
 *   • Initial text display and empty VU bars
 *   • Timing initialization for animations
 */
void DisplayManager::initializeTFTDisplay()
{
  // ===== DISPLAY DRIVER SETUP =====
  // Create SPI bus interface (Hardware SPI on ESP32 VSPI pins)
  bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI,
                             GFX_NOT_DEFINED /* MISO */, VSPI /* spi_num */);

  // Create NV3007 display driver (428x142 landscape configuration)
  // Note: width/height are native portrait dimensions, rotation 1 makes it
  // landscape
  gfx = new Arduino_NV3007(
      bus, TFT_RST, 3 /* rotation */, false /* IPS */,
      DISPLAY_HEIGHT /* native width */, DISPLAY_WIDTH /* native height */,
      12 /* col offset 1 */, 0 /* row offset 1 */, 14 /* col offset 2 */,
      0 /* row offset 2 */, nv3007_279_init_operations,
      sizeof(nv3007_279_init_operations));

  // Initialize display controller
  if (!gfx->begin())
  {
    // Display initialization failed - halt system
    // Note: Cannot log to Serial as it's reserved for PC communication

    while (1)
    {
      delay(1000); // Infinite loop indicates display failure
    }
  }

  // ===== DISPLAY HARDWARE CONFIGURATION =====
  // Configure backlight control (full brightness)
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // ===== VU METER ADC SETUP =====
  // Configure ESP32 ADC for high-resolution audio sampling
  analogReadResolution(12);       // 12-bit resolution: 0-4095 range
  analogSetAttenuation(ADC_11db); // Full 0-3.3V input range
  pinMode(PIN_L, INPUT);          // Left audio input
  pinMode(PIN_R, INPUT);          // Right audio input

  // Initialize text display state for change detection
  prevStationID = stationID;                 // Cache initial station ID
  prevRadioText = radioText;                 // Cache initial radio text
  nextScrollAt = millis() + SCROLL_DELAY_MS; // Initialize scroll timing

  // Initialize VU meter timing
  nextDecayAt = millis() + DECAY_INTERVAL_MS; // Start decay timer
}

/**
 * @brief Display splash screen with device info
 *
 * Shows RDS STEREO ENCODER title, version, build date and initialization
 * progress for 4 seconds during system startup. Uses different text sizes
 * for visual hierarchy and centers everything for professional appearance.
 */
void DisplayManager::showSplashScreen()
{
  // ===== CLEAR SCREEN =====
  gfx->fillScreen(COLOR_BLACK);

  // ===== DISPLAY SPLASH IMAGE =====
  // Calculate position to center the 300x142 image on 428x142 display
  int16_t imageX = (DISPLAY_WIDTH - SPLASH_IMAGE_WIDTH) / 2; // (428-300)/2 = 64
  int16_t imageY =
      (DISPLAY_HEIGHT - SPLASH_IMAGE_HEIGHT) / 2; // (142-142)/2 = 0

  // Draw the RGB565 bitmap
  gfx->draw16bitRGBBitmap(imageX, imageY, LOGO_300, SPLASH_IMAGE_WIDTH,
                          SPLASH_IMAGE_HEIGHT);

  // ===== BUILD DATE OVERLAY =====
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_WHITE);

  char buildInfo[50];
  snprintf(buildInfo, sizeof(buildInfo), "Built: %s", __DATE__);
  int16_t buildWidth = strlen(buildInfo) * 6;
  int16_t buildX = (DISPLAY_WIDTH - buildWidth) / 2; // Centered
  gfx->setCursor(buildX, 5);                         // Top of screen
  gfx->print(buildInfo);

  // ===== DISPLAY DURATION =====
  delay(4000); // Show splash for 4 seconds

  // ===== TRANSITION TO NORMAL DISPLAY =====
  // Clear screen and set up normal display layout
  gfx->fillScreen(COLOR_BLACK);

  // Draw initial normal content
  drawCenteredStationID(); // Station ID in top area
  drawScrollingText();     // Radio text in middle area

  // Draw VU meter labels (isolated graphics state)
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(VU_MARGIN_X, VU_L_Y + 5);
  gfx->print("L");
  gfx->setCursor(VU_MARGIN_X, VU_R_Y + 5);
  gfx->print("R");
  // Graphics state will be reset by subsequent operations

  // Draw initial empty VU bars
  drawVUBar(leftCh, 0, -1, 0, -1);
  drawVUBar(rightCh, 0, -1, 0, -1);

  // Draw dB scale between the bars
  drawVUScale();
}

/**
 * @brief Update TFT display with new PS/RT data from queue
 * @param data PS/RT data structure received from CommunicationManager
 *
 * Process incoming PS/RT data and trigger display updates:
 *   1. Copy strings to internal buffers (safe null termination)
 *   2. Convert to String objects for text rendering
 *   3. Change detection will trigger redraws automatically
 */
void DisplayManager::updateTFTDisplay(const DisplayData &data)
{
  // ===== SAFE STRING COPYING =====
  // Copy PS text with guaranteed null termination
  strncpy(currentShortString, data.shortString, sizeof(currentShortString) - 1);
  currentShortString[sizeof(currentShortString) - 1] = '\0';

  // Copy RT text with guaranteed null termination
  strncpy(currentLongString, data.longString, sizeof(currentLongString) - 1);
  currentLongString[sizeof(currentLongString) - 1] = '\0';

  // ===== STRING CONVERSION FOR TFT =====
  // Convert C strings to Arduino String objects for text rendering
  stationID = String(currentShortString); // PS -> Station ID
  radioText = String(currentLongString);  // RT -> Radio Text

  // Note: Change detection in updateTextDisplay() will handle redraws
  // automatically
}

/**
 * @brief Draw station ID (PS) centered in top area of display
 *
 * Visual specification:
 *   • Position: Full width, Y=20-60 (40px height)
 *   • Font: Size 3 (large), Cyan color
 *   • Layout: Horizontally and vertically centered
 *   • Behavior: Only redraws if station ID actually changed
 */
void DisplayManager::drawCenteredStationID()
{
  // ===== CLEAR AREA =====
  // Clear entire station area with black background
  gfx->fillRect(0, STATION_Y, DISPLAY_WIDTH, STATION_HEIGHT, COLOR_BLACK);

  // ===== FONT CONFIGURATION =====
  gfx->setTextSize(3);           // Large font size
  gfx->setTextColor(COLOR_CYAN); // Distinctive cyan color

  // ===== TEXT PREPARATION =====
  // Trim trailing spaces for proper centering
  String trimmedStationID = stationID;
  trimmedStationID.trim(); // Remove leading and trailing whitespace

  // ===== CENTERING CALCULATION =====
  // Get text bounding box for precise centering (use trimmed text)
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(trimmedStationID, 0, 0, &x1, &y1, &w, &h);

  // Calculate centered position
  int x = (DISPLAY_WIDTH - w) / 2;              // Horizontal center
  int y = STATION_Y + (STATION_HEIGHT - h) / 2; // Vertical center in area

  // ===== DRAW TEXT =====
  gfx->setCursor(x, y);
  gfx->print(trimmedStationID); // Use trimmed version for display
}

/**
 * @brief Draw radio text (RT) with smooth scrolling support
 *
 * Visual specification:
 *   • Position: 398px width with 15px margins, Y=38-63 (25px height)
 *   • Font: Size 2 (medium), White color
 *   • Behavior: Centered if short, smooth scrolling if long
 *   • Animation: 2 pixels every 150ms = ~13 pixels/second (~7 FPS)
 */
void DisplayManager::drawScrollingText()
{
  // ===== RACE CONDITION PROTECTION =====
  if (isDrawingText)
    return; // Another call is already in progress
  isDrawingText = true;

  // ===== CLEAR AREA PRECISELY =====
  // Clear only the text area to avoid affecting other display elements
  gfx->fillRect(TEXT_MARGIN_X, TEXT_Y, TEXT_WIDTH, TEXT_HEIGHT, COLOR_BLACK);

  // ===== FONT CONFIGURATION =====
  gfx->setTextSize(2);            // Medium font for readability
  gfx->setTextColor(COLOR_WHITE); // White text on black background

  // ===== SLIDING WINDOW SCROLLING =====
  // Fixed position, sliding content - much safer approach

  const int SAFE_Y = TEXT_Y + 10; // 10px from top (moved higher)

  // ===== CALCULATE SAFE WINDOW SIZE =====
  // Use actual display measurements to determine max characters that fit
  const int AVAILABLE_WIDTH =
      TEXT_WIDTH - 30; // TEXT_WIDTH minus margins (300-30=270px)
  const int ESTIMATED_CHAR_WIDTH =
      12; // Approximate width per character at size 2
  const int WINDOW_SIZE =
      AVAILABLE_WIDTH / ESTIMATED_CHAR_WIDTH; // ~22 characters

  String displayText;
  int displayX;

  if (radioText.length() <= WINDOW_SIZE)
  {
    // ===== SHORT TEXT: CENTERED DISPLAY =====
    displayText = radioText;

    // Calculate centered position
    int16_t x1, y1;
    uint16_t textWidth, textHeight;
    gfx->getTextBounds(displayText, 0, 0, &x1, &y1, &textWidth, &textHeight);
    displayX = TEXT_MARGIN_X +
               (TEXT_WIDTH - textWidth) / 2; // Center in available width
  }
  else
  {
    // ===== LONG TEXT: SLIDING WINDOW =====
    // Create circular buffer effect
    String extendedText = radioText + "   " + radioText; // Add spacing and wrap

    // Calculate window position (scrollPos is the offset)
    int fullCycleLength = radioText.length() + 3; // Original text + 3 spaces
    int startPos = scrollPos % fullCycleLength;

    // Extract window from extended text
    if (startPos + WINDOW_SIZE <= extendedText.length())
    {
      displayText = extendedText.substring(startPos, startPos + WINDOW_SIZE);
    }
    else
    {
      // Handle wrap-around case
      displayText = extendedText.substring(startPos);
      while (displayText.length() < WINDOW_SIZE)
      {
        displayText +=
            extendedText.substring(0, WINDOW_SIZE - displayText.length());
      }
    }

    // Use fixed left position for scrolling text
    displayX = TEXT_MARGIN_X + 15; // Fixed position for scrolling
  }

  // Draw the text at calculated position
  gfx->setCursor(displayX, SAFE_Y);
  gfx->print(displayText);

  // ===== CLEAR PROTECTION FLAG =====
  isDrawingText = false;
}

/**
 * @brief Handle text scrolling animation timing
 *
 * Smooth pixel-based scrolling implementation:
 *   • Only processes scrolling for text longer than display width
 *   • Advances scroll position by SCROLL_STEP_SIZE pixels per update
 *   • Updates every 150ms (50ms * 3 = ~7 FPS) for readable scrolling
 *   • Seamless wraparound when text completely scrolls off screen
 *   • Immediately triggers redraw for smooth animation
 */
void DisplayManager::updateTextScrolling()
{
  unsigned long now = millis();

  // ===== SLIDING WINDOW SCROLLING =====
  // Advance the window position for long text

  // Only scroll if text is longer than window size (using calculated
  // WINDOW_SIZE)
  const int AVAILABLE_WIDTH = TEXT_WIDTH - 30;
  const int ESTIMATED_CHAR_WIDTH = 12;
  const int WINDOW_SIZE = AVAILABLE_WIDTH / ESTIMATED_CHAR_WIDTH;

  if (radioText.length() > WINDOW_SIZE && now >= nextScrollAt)
  {
    // ===== ADVANCE WINDOW POSITION =====
    scrollPos++; // Move window by 1 character

    // ===== WRAPAROUND =====
    // Reset when we've scrolled through the entire cycle (text + spacing)
    int fullCycleLength = radioText.length() + 3; // Original text + 3 spaces
    if (scrollPos >= fullCycleLength)
    {
      scrollPos = 0; // Reset to beginning for seamless loop
    }

    // ===== TIMING UPDATE =====
    nextScrollAt =
        now + (SCROLL_DELAY_MS * 3); // Slow down by 200% (50ms -> 150ms)

    // ===== REDRAW WINDOW =====
    drawScrollingText(); // Redraw with new window position
  }
}

/**
 * @brief Check for text changes and trigger updates
 *
 * Change detection system prevents unnecessary redraws and flicker:
 *   • Compare current text against cached previous values
 *   • Only trigger redraws when content actually changes
 *   • Reset scroll animation state when new text arrives
 *   • Update cache to prevent repeated updates
 */
void DisplayManager::updateTextDisplay()
{
  // ===== STATION ID CHANGE DETECTION =====
  if (stationID != prevStationID)
  {
    drawCenteredStationID();   // Redraw station ID area
    prevStationID = stationID; // Update cache to prevent repeated updates
  }

  // ===== RADIO TEXT CHANGE DETECTION =====
  if (radioText != prevRadioText)
  {
    // ===== RESET SCROLLING STATE =====
    // New text requires fresh start for scrolling animation
    scrollPos = 0;                             // Start from beginning
    nextScrollAt = millis() + SCROLL_DELAY_MS; // Reset scroll timing

    // ===== IMMEDIATE DISPLAY =====
    drawScrollingText(); // Show new text immediately

    // ===== UPDATE CACHE =====
    prevRadioText = radioText; // Prevent repeated updates until next change
  }
}

/**
 * @brief Draw one VU meter bar with color gradient and peak marker
 * @param ch Channel data structure (left or right)
 * @param newLen Bar length in pixels (0 to VU_BAR_WIDTH)
 * @param newPeak Peak marker position (-1 if hidden, 0-VU_BAR_WIDTH if visible)
 *
 * Professional VU meter rendering with color zones:
 *   • Green zone: 0-70% (safe audio levels)
 *   • Yellow zone: 70-85% (moderate levels)
 *   • Orange zone: 85-95% (high levels)
 *   • Red zone: 95-100% (peak/overload levels)
 *   • Peak marker: 3px white bar showing recent maximum
 *   • Outline: Dark gray border for visual definition
 */
void DisplayManager::drawVUBar(Channel &ch, int newLen, int newPeak,
                               int prevLen, int prevPeak)
{
  // ===== POSITION CALCULATION =====
  int barY = ch.row;                       // Y position for this channel
  int barX = VU_MARGIN_X + VU_LABEL_WIDTH; // X position (after L/R label)

  // ===== OPTIMIZED UPDATES: ONLY CHANGE WHAT'S DIFFERENT =====

  // If bar got shorter, clear the area that's no longer filled
  if (newLen < prevLen)
  {
    int clearX = barX + newLen;
    int clearWidth = prevLen - newLen;
    gfx->fillRect(clearX, barY, clearWidth, VU_BAR_HEIGHT, COLOR_BLACK);
  }

  // ===== COLOR GRADIENT BAR =====
  // Draw any part of the bar that needs to be drawn
  if (newLen > 0)
  {
    // Determine what range needs to be drawn
    int startX = 0; // Default: draw from beginning
    int endX = newLen;

    // If this is an incremental update, only draw the new part
    if (prevLen > 0 && newLen > prevLen)
    {
      startX = prevLen; // Only draw the extended portion
    }

    // Draw the required range
    for (int x = startX; x < endX; x++)
    {
      uint16_t color;
      if (x < VU_GREEN_THRESHOLD)
        color = COLOR_GREEN;
      else if (x < VU_YELLOW_THRESHOLD)
        color = COLOR_YELLOW;
      else if (x < VU_RED_THRESHOLD)
        color = COLOR_ORANGE;
      else
        color = COLOR_RED;

      // Draw vertical line for this pixel column
      gfx->drawFastVLine(barX + x, barY + 2, VU_BAR_HEIGHT - 4, color);
    }
  }

  // ===== PEAK MARKER UPDATES =====
  // Clear old peak marker if it moved or disappeared
  if (prevPeak >= 0 && prevPeak != newPeak)
  {
    int oldPeakX = barX + prevPeak;
    gfx->fillRect(oldPeakX, barY, PEAK_WIDTH, VU_BAR_HEIGHT, COLOR_BLACK);
  }

  // Draw new peak marker at current position
  if (newPeak >= 0 && newPeak < VU_BAR_WIDTH)
  {
    int peakX = barX + newPeak;
    gfx->fillRect(peakX, barY, PEAK_WIDTH, VU_BAR_HEIGHT, COLOR_WHITE);
  }

  // ===== BAR OUTLINE =====
  // Draw dark gray border for visual definition
  gfx->drawRect(barX - 1, barY - 1, VU_BAR_WIDTH + 2, VU_BAR_HEIGHT + 2,
                COLOR_DARK_GRAY);
}

/**
 * @brief Draw dB scale markings between VU bars
 *
 * Draws professional dB reference marks positioned between the left and right
 * VU bars for easy level reference during audio monitoring.
 */
void DisplayManager::drawVUScale()
{
  int barX = VU_MARGIN_X + VU_LABEL_WIDTH; // X position (after L/R label)

  // Center the scale exactly between the two VU bars
  // VU_L_Y = 120, VU_BAR_HEIGHT = 25, VU_BAR_SPACING = 30
  // Left bar bottom: 120 + 25 = 145
  // Right bar top: 145 + 30 = 175
  // Center between: 145 + (30/2) = 160
  int scaleY = VU_L_Y + VU_BAR_HEIGHT + (VU_BAR_SPACING / 2); // Should be Y=160

  // Define dB positions (as percentage of bar width)
  struct
  {
    float position; // 0.0 to 1.0 across bar width
    const char *label;
    bool major;     // true for major tick marks
    uint16_t color; // Color for this label
  } dbMarks[] = {
      {0.4f, "-20", false, COLOR_GREEN}, // -20dB (safe level - green)
      {0.6f, "-10", false, COLOR_GREEN}, // -10dB (safe level - green)
      {0.8f, "-3", false, COLOR_YELLOW}, // -3dB (caution level - yellow)
      {0.85f, "0", true, COLOR_WHITE},   // 0dB (reference level - white)
      {0.95f, "+3", false, COLOR_RED}    // +3dB (peak/overload - red)
  };

  for (int i = 0; i < 5; i++)
  {
    int markX = barX + (int)(dbMarks[i].position * VU_BAR_WIDTH);
    int tickHeight = dbMarks[i].major ? 4 : 2; // Smaller ticks for cleaner look

    // Draw ticks with proper spacing above and below center
    // Create gap between ticks for the label to sit in between
    int gap = 12; // Increased gap for better spacing around text

    // Tick above: draw upward from center minus gap
    int upperTickStart = scaleY - gap / 2;
    for (int t = 1; t <= tickHeight; t++)
    {
      gfx->drawPixel(markX, upperTickStart - t, COLOR_WHITE);
    }

    // Tick below: draw downward from center plus gap
    int lowerTickStart = scaleY + gap / 2;
    for (int t = 1; t <= tickHeight; t++)
    {
      gfx->drawPixel(markX, lowerTickStart + t, COLOR_WHITE);
    }

    // Draw label centered on the cyan line with bold effect
    gfx->setTextSize(1);
    gfx->setTextColor(dbMarks[i].color);           // Use color from the array
    int labelWidth = strlen(dbMarks[i].label) * 6; // Approximate width

    // Fine-tune text positioning: move up by 1 more pixel
    int textHeight = 8; // Size 1 font height
    int labelY = scaleY - (textHeight / 2) +
                 1; // Move baseline UP from center (adjusted)

    // Create bold effect by drawing text multiple times with slight offsets
    int centerX = markX - labelWidth / 2;
    gfx->setCursor(centerX, labelY);
    gfx->print(dbMarks[i].label);
    gfx->setCursor(centerX + 1,
                   labelY); // Offset by 1 pixel to create bold effect
    gfx->print(dbMarks[i].label);
  }
}

/* =====================================================================================
 *                              VU METER METHODS
 * =====================================================================================
 */

/**
 * @brief Read ADC pin with proper settling time for accurate measurements
 * @param pin Arduino analog pin designation (PIN_L or PIN_R)
 * @return Raw 12-bit ADC value (0-4095 range)
 *
 * ESP32 ADC reading technique for maximum accuracy:
 *   1. Dummy read allows sample-and-hold capacitor to settle
 *   2. Optional delay improves sensitivity to rapid changes
 *   3. Actual read provides accurate audio level measurement
 */
unsigned int DisplayManager::readADC(byte pin)
{
  analogRead(pin); // Discard first read (allows settling time)

#if (SAMPLE_SPACING_US > 0)
  delayMicroseconds(
      SAMPLE_SPACING_US); // Optional delay for improved sensitivity
#endif

  return analogRead(pin); // Return accurate second reading
}

/**
 * @brief Convert raw ADC value to bar pixels with logarithmic scaling
 * @param v Raw 12-bit ADC value (0-4095)
 * @return Bar length in pixels (0 to VU_BAR_WIDTH)
 *
 * Perceptual audio level mapping algorithm:
 *   • Linear ADC values don't match human audio perception
 *   • Gamma curve (0.35) provides perceptual scaling
 *   • Quiet signals become more visible on display
 *   • Noise gate eliminates random flicker
 *   • Result scales to exact bar width for display
 */
int DisplayManager::mapToBarLog(unsigned int v)
{
  if (v == 0)
    return 0; // Fast path for silence

  // ===== NORMALIZATION =====
  const float x = (float)v / 4095.0f; // ESP32 12-bit ADC: 0..4095 → 0.0..1.0

  // ===== PERCEPTUAL CURVE =====
  const float GAMMA = 0.35f; // Compression factor (< 1.0 boosts quiet signals)
  const float y = powf(x, GAMMA); // Apply gamma correction

  // ===== SCALING AND ROUNDING =====
  int pixels =
      (int)(y * (float)VU_BAR_WIDTH + 0.5f); // Scale to bar width + round

  // ===== NOISE GATE =====
  if (pixels <= NOISE_FLOOR_CELLS)
    return 0; // Eliminate tiny flicker from noise

  // ===== BOUNDS CHECKING =====
  if (pixels < 0)
    pixels = 0;
  else if (pixels > VU_BAR_WIDTH)
    pixels = VU_BAR_WIDTH;

  return pixels;
}

/**
 * @brief Get appropriate color for VU bar position
 * @param position Pixel position within bar (0 to VU_BAR_WIDTH)
 * @return 16-bit RGB565 color value
 *
 * Professional VU meter color standards:
 *   • Green: Safe operating levels (0-70%)
 *   • Yellow: Moderate levels requiring attention (70-85%)
 *   • Orange: High levels approaching limits (85-95%)
 *   • Red: Peak levels indicating potential overload (95-100%)
 */
uint16_t DisplayManager::getVUColor(int position)
{
  if (position <= VU_GREEN_THRESHOLD)
    return COLOR_GREEN;
  else if (position <= VU_YELLOW_THRESHOLD)
    return COLOR_YELLOW;
  else if (position <= VU_RED_THRESHOLD)
    return COLOR_ORANGE;
  else
    return COLOR_RED;
}

/**
 * @brief Sample ADC and update channel target level using moving maximum filter
 * @param ch Channel structure to update (leftCh or rightCh)
 *
 * Moving maximum filter for audio level detection:
 *   • Collects multiple ADC samples per level calculation
 *   • Tracks maximum value in sample window (peak detection)
 *   • Updates target level when window is complete
 *   • Applies noise floor gating to eliminate flicker
 *   • Provides smooth, responsive audio level tracking
 */
void DisplayManager::updateChannelLevel(Channel &ch)
{
  // ===== ADC SAMPLING =====
  unsigned int s = readADC(ch.pin); // Get current ADC reading

  // ===== PEAK DETECTION =====
  if (s > ch.maxSample)
    ch.maxSample = s; // Track maximum value in current window

  // ===== WINDOW COMPLETION CHECK =====
  if (++ch.index >= SAMPLE_WINDOW)
  {
    // ===== LEVEL CALCULATION =====
    ch.index = 0;                         // Reset sample counter
    ch.level = mapToBarLog(ch.maxSample); // Convert to bar pixels
    ch.maxSample = 0;                     // Reset for next window

    // ===== NOISE GATE =====
    if (ch.level <= NOISE_FLOOR_CELLS)
      ch.level = 0; // Suppress low-level noise and flicker
  }
}

/**
 * @brief Apply fast attack smoothing to rising audio levels
 * @param ch Channel structure to update
 *
 * Fast attack response ensures immediate visual feedback for audio peaks:
 *   • Only affects rising levels (target > current)
 *   • Large differences use maximum step size (ATTACK_STEP)
 *   • Small differences use remaining difference (prevents overshoot)
 *   • Creates natural, responsive VU meter behavior
 */
void DisplayManager::smoothRise(Channel &ch)
{
  if (ch.level > ch.avg)
  {
    int delta = ch.level - ch.avg; // Calculate difference
    int step = (delta > ATTACK_STEP) ? ATTACK_STEP : delta; // Limit step size
    ch.avg = ch.avg + step; // Apply smoothing step
  }
}

/**
 * @brief Apply slow release smoothing to falling levels (both channels)
 *
 * Timer-based decay creates natural VU meter falling behavior:
 *   • 20ms intervals provide smooth visual decay
 *   • Slower than attack for realistic ballistics
 *   • Applied to both channels simultaneously
 *   • Prevents jarring instant level drops
 *   • Creates professional VU meter appearance
 */
void DisplayManager::decayStepIfDue()
{
  unsigned long now = millis();

  // ===== TIMING CHECK =====
  if (now >= nextDecayAt)
  {
    nextDecayAt = now + DECAY_INTERVAL_MS; // Schedule next decay step

    // ===== LEFT CHANNEL DECAY =====
    if (leftCh.avg > leftCh.level)
    {
      int delta = leftCh.avg - leftCh.level; // Calculate difference
      int step =
          (delta > RELEASE_STEP) ? RELEASE_STEP : delta; // Limit step size
      leftCh.avg = leftCh.avg - step;                    // Apply decay step
    }

    // ===== RIGHT CHANNEL DECAY =====
    if (rightCh.avg > rightCh.level)
    {
      int delta = rightCh.avg - rightCh.level; // Calculate difference
      int step =
          (delta > RELEASE_STEP) ? RELEASE_STEP : delta; // Limit step size
      rightCh.avg = rightCh.avg - step;                  // Apply decay step
    }
  }
}

/**
 * @brief Update peak marker position and hold/decay timing
 * @param ch Channel structure to update
 *
 * Peak marker behavior matching professional VU meters:
 *   • Latches immediately at new maximum levels
 *   • Holds at peak position for PEAK_HOLD_MS duration
 *   • Clears only when bar drops below peak AND hold time expires
 *   • Provides visual feedback for recent maximum audio levels
 *   • Essential for monitoring peak audio levels
 */
void DisplayManager::updatePeak(Channel &ch)
{
  unsigned long now = millis();

  // ===== PEAK LATCH =====
  // New peak detected - latch at current level
  if (ch.avg - 1 > ch.peak) // -1 ensures peak stays ahead of bar
  {
    ch.peak = ch.avg - 1; // Set peak position
    if (ch.peak < 0)
      ch.peak = -1;                        // Hide if bar is at zero
    ch.peakHoldUntil = now + PEAK_HOLD_MS; // Start hold timer
  }

  // ===== PEAK CLEAR =====
  // Clear peak marker when conditions are met
  else if (ch.peak >= 0 &&            // Peak is visible AND
           now >= ch.peakHoldUntil && // Hold time has expired AND
           ch.avg <= ch.peak)         // Bar has dropped below peak
  {
    ch.peak = -1; // Hide peak marker
  }
}

/* =====================================================================================
 *                              END OF IMPLEMENTATION
 * =====================================================================================
 */