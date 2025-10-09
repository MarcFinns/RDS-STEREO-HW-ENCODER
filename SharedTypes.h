/*
 * =====================================================================================
 *
 *                             ESP32 RDS STEREO ENCODER
 *                            Shared Types & Configuration
 *
 * =====================================================================================
 *
 * File:         SharedTypes.h
 * Description:  Central configuration and shared data structures for the ESP32
 * RDS encoder bridge system. Contains all constants, data types, and hardware
 * definitions used across the entire project.
 *
 * System Architecture:
 *   - CommunicationManager: Handles PC ↔ ESP32 ↔ MRDS1322 communication
 *   - DisplayManager: Manages TFT display + VU meters
 *   - Main: FreeRTOS task initialization and queue management
 *
 * Hardware Setup:
 *   - ESP32 DevKit with dual-core processing
 *   - MRDS1322 RDS encoder chip (Serial1, GPIO21/22, 19200 baud)
 *   - NV3007 428x142 TFT display (SPI, GPIO16/17/18/19, landscape mode)
 *   - Dual-channel audio input for VU meters (GPIO36/39, ADC pins A0/A3)
 *
 * Communication Flow:
 *   PC → ESP32 (Serial USB) → MRDS1322 (Serial1) → ESP32 → TFT Display
 *
 * Author:       Claude Code Assistant
 * Created:      2024
 * Version:      2.0
 *
 * =====================================================================================
 */

#ifndef SHARED_TYPES_H
#define SHARED_TYPES_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <Arduino.h>

/* =====================================================================================
 *                                DATA STRUCTURES
 * =====================================================================================
 */

/**
 * @brief PS/RT display data structure for queue communication
 *
 * Carries RDS Program Service (PS) and Radio Text (RT) data from the
 * CommunicationManager to the DisplayManager via FreeRTOS queue.
 *
 * Data Flow: MRDS1322 → CommunicationManager → Queue → DisplayManager → TFT
 */
struct DisplayData
{
  char shortString[9]; // PS: Program Service Name (8 chars + null terminator)
  char longString[65]; // RT: Radio Text message (64 chars + null terminator)
};

/**
 * @brief PC command buffer for enhanced collision-avoidance queuing
 *
 * Stores PC commands that arrive during ESP32 polling cycles with command type
 * classification for intelligent processing. READ commands require immediate
 * response handling while WRITE commands can be processed with normal delay.
 */
struct PCCommand
{
  uint8_t data[128];  // Raw command data from PC
  size_t length;      // Actual command length in bytes
  bool isReadCommand; // true if READ command (requires immediate response)
  uint32_t timestamp; // Command arrival time for timeout handling
};

/* =====================================================================================
 *                              FREERTOS QUEUE HANDLES
 * =====================================================================================
 */

extern QueueHandle_t
    displayQueue; // PS/RT data: CommunicationManager → DisplayManager
extern QueueHandle_t
    pcCommandQueue; // PC commands queued during MRDS1322 polling

/* =====================================================================================
 *                            MRDS1322 RDS ENCODER PROTOCOL
 * =====================================================================================
 */

// MRDS1322 Memory Map - RDS Data Storage Addresses
#define MRDS1322_PS_ADDR 0xC8 // Program Service Name start address
#define MRDS1322_PS_LEN 8     // PS length: 8 characters (data only)
#define MRDS1322_RT_ADDR 0x20 // Radio Text start address
#define MRDS1322_RT_LEN 64    // RT length: 64 characters (data only)

// MRDS1322 Serial Protocol - Command/Response Frame Structure
#define MRDS1322_START_BYTE 0xFE // Frame start delimiter
#define MRDS1322_READ_CMD 0xD0   // Read command opcode
#define MRDS1322_WRITE_CMD 0x00  // Write command opcode (actual observed)
#define MRDS1322_STOP_BYTE 0xFF  // Frame end delimiter
#define MRDS1322_STUFF_BYTE 0xFD // Byte-stuffing escape character

// Command Processing Timeouts
#define PC_COMMAND_TIMEOUT_MS                                                  \
  2000 // Maximum time to wait for PC command response
#define READ_RESPONSE_TIMEOUT_MS                                               \
  1500 // Maximum time to wait for MRDS1322 read response

/*
 * MRDS1322 Protocol Example:
 *
 * Read PS Command:  FE D0 C8 08 FF
 *   FE = Start byte
 *   D0 = Read command
 *   C8 = PS start address (0xC8)
 *   08 = Length (8 bytes)
 *   FF = Stop byte
 *
 * Response:         FE <8 PS bytes> FF
 *   FE = Start byte
 *   <data> = PS characters (with byte-stuffing if needed)
 *   FF = Stop byte
 *
 * Byte-Stuffing Rules:
 *   Data byte FD → FD 00
 *   Data byte FE → FD 01
 *   Data byte FF → FD 02
 */

/* =====================================================================================
 *                          ESP32 HARDWARE PIN CONFIGURATION
 * =====================================================================================
 */

// ===== SERIAL COMMUNICATION PINS =====
#define SERIAL1_RX 21 // MRDS1322 RX pin (GPIO21)
#define SERIAL1_TX 22 // MRDS1322 TX pin (GPIO22)
// Note: Serial (USB) uses default pins and is initialized separately

// ===== TFT DISPLAY PINS (NV3007 - SPI Interface) =====
#define TFT_CS 16   // Chip Select (GPIO16)
#define TFT_RST 5   // Reset (GPIO5)
#define TFT_DC 17   // Data/Command (GPIO17)
#define TFT_MOSI 18 // SPI MOSI Data (GPIO18)
#define TFT_SCLK 19 // SPI Clock (GPIO19)
#define TFT_BL 4    // Backlight Control (GPIO4)

// ===== VU METER ADC INPUT PINS =====
#define PIN_L A3 // Left audio channel: GPIO39 (ADC1_CH3)
#define PIN_R A0 // Right audio channel: GPIO36 (ADC1_CH0)

/*
 * Complete Pin Assignment Reference:
 *
 * ┌──────────────┬─────────────────────┬──────────────────────────┐
 * │ ESP32 GPIO   │ Function            │ Connected To             │
 * ├──────────────┼─────────────────────┼──────────────────────────┤
 * │ GPIO21       │ SERIAL1_RX          │ MRDS1322 TX              │
 * │ GPIO22       │ SERIAL1_TX          │ MRDS1322 RX              │
 * │ GPIO16       │ TFT_CS              │ NV3007 Chip Select       │
 * │ GPIO5        │ TFT_RST             │ NV3007 Reset             │
 * │ GPIO17       │ TFT_DC              │ NV3007 Data/Command      │
 * │ GPIO18       │ TFT_MOSI            │ NV3007 SPI Data          │
 * │ GPIO19       │ TFT_SCLK            │ NV3007 SPI Clock         │
 * │ GPIO4        │ TFT_BL              │ NV3007 Backlight         │
 * │ GPIO39 (A3)  │ PIN_L (ADC1_CH3)    │ Left Audio Input         │
 * │ GPIO36 (A0)  │ PIN_R (ADC1_CH0)    │ Right Audio Input        │
 * │ USB (N/A)    │ Serial (PC Comm)    │ PC USB Connection        │
 * └──────────────┴─────────────────────┴──────────────────────────┘
 */

/* =====================================================================================
 *                          TFT DISPLAY HARDWARE CONFIGURATION
 * =====================================================================================
 */

// Display Controller & Resolution
#define DISPLAY_WIDTH 428  // NV3007 width in landscape mode
#define DISPLAY_HEIGHT 142 // NV3007 height in landscape mode

/*
 * Display Layout - Vertical Stack Design:
 *
 * ┌─────────────────────────────────┐ ← Y=10
 * │         STATION ID              │   Station area (30px tall)
 * │      (Cyan, Size 3)             │
 * ├─────────────────────────────────┤ ← Y=38
 * │   Scrolling Radio Text...       │   Text area (25px tall)
 * │     (White, Size 2)             │
 * ├─────────────────────────────────┤ ← Y=75
 * │ L ████████████████████████████  │   VU Left (18px tall)
 * │ R ████████████████████████████  │   VU Right (18px tall)
 * └─────────────────────────────────┘ ← Y=142
 */

// Station ID Area - Top section, full width
#define STATION_Y 10      // Top margin for station ID
#define STATION_HEIGHT 30 // Vertical space for large text

// Radio Text Area - Middle section, full width with side margins
#define TEXT_Y 38        // Y position below station ID
#define TEXT_HEIGHT 25   // Vertical space for medium text
#define TEXT_MARGIN_X 15 // Left/right margins for text
#define TEXT_WIDTH                                                             \
  (DISPLAY_WIDTH - 2 * TEXT_MARGIN_X) // Usable text width: 398px

// VU Meters Area - Bottom section, full width with margins
#define VU_Y 75        // Y position below radio text
#define VU_MARGIN_X 25 // Left/right margins for VU meters
#define VU_WIDTH (DISPLAY_WIDTH - 2 * VU_MARGIN_X) // Total VU area width: 378px
#define VU_LABEL_WIDTH 20 // Space reserved for "L"/"R" labels

/* =====================================================================================
 *                              VU METER CONFIGURATION
 * =====================================================================================
 */

// VU Bar Dimensions & Layout
#define VU_BAR_WIDTH (VU_WIDTH - VU_LABEL_WIDTH) // Actual bar width: 358px
#define VU_BAR_HEIGHT 18                         // Bar thickness
#define VU_BAR_SPACING 20 // Space between L/R bars (includes scale)
#define VU_L_Y VU_Y       // Left channel Y position
#define VU_R_Y                                                                 \
  (VU_L_Y + VU_BAR_HEIGHT + VU_BAR_SPACING) // Right channel Y position

// VU Meter Behavior Parameters
#define SAMPLE_WINDOW 2      // Samples to collect per level calculation
#define ATTACK_STEP 3        // Pixels per step for rising bars (fast attack)
#define RELEASE_STEP 4       // Pixels per step for falling bars (slow release)
#define DECAY_INTERVAL_MS 20 // Milliseconds between decay steps
#define PEAK_HOLD_MS 1000    // Peak marker hold time (1 second)
#define NOISE_FLOOR_CELLS 1  // Minimum pixels to ignore (noise gate)
#define SAMPLE_SPACING_US 0  // Microseconds between ADC reads (0 = disabled)

/* =====================================================================================
 *                              DISPLAY COLOR PALETTE
 * =====================================================================================
 */

// Basic Colors (16-bit RGB565 format)
#define COLOR_BLACK 0x0000      // Background color
#define COLOR_WHITE 0xFFFF      // Peak markers, general text
#define COLOR_RED 0xF800        // VU meter red zone
#define COLOR_GREEN 0x07E0      // VU meter safe zone
#define COLOR_BLUE 0x001F       // Unused
#define COLOR_YELLOW 0xFFE0     // VU meter caution zone
#define COLOR_CYAN 0x07FF       // Station ID text
#define COLOR_MAGENTA 0xF81F    // Unused
#define COLOR_ORANGE 0xFD20     // VU meter warning zone
#define COLOR_DARK_GREEN 0x03E0 // Unused
#define COLOR_LIGHT_GRAY 0xC618 // Unused
#define COLOR_DARK_GRAY 0x7BEF  // VU bar outlines

// VU Meter Color Thresholds (proportional to bar width)
#define VU_GREEN_THRESHOLD (VU_BAR_WIDTH * 0.7)   // 70% = Green zone
#define VU_YELLOW_THRESHOLD (VU_BAR_WIDTH * 0.85) // 85% = Yellow zone
#define VU_RED_THRESHOLD (VU_BAR_WIDTH * 0.95) // 95% = Orange zone (Red = 100%)

/*
 * VU Meter Color Mapping:
 *
 *   0% ──────── 70% ── 85% ─ 95% ── 100%
 *   Green      Yellow Orange  Red
 *   ████████   ███    ██      █
 *   (Safe)   (Loud) (Hot)  (Peak)
 */

// Peak Marker Appearance
#define PEAK_WIDTH 3              // Peak marker width in pixels
#define PEAK_HEIGHT VU_BAR_HEIGHT // Peak marker height (matches bar height)

/* =====================================================================================
 *                            TEXT SCROLLING CONFIGURATION
 * =====================================================================================
 */

#define SCROLL_DELAY_MS 50 // Base delay (multiplied by 3 in code = 150ms actual)
#define SCROLL_STEP_SIZE 2 // Pixels to advance per step (smooth motion)

/*
 * Text Scrolling Behavior:
 *
 * - If text fits in display width: Show centered, no scrolling
 * - If text exceeds width: Smooth right-to-left pixel scrolling
 * - Speed: 2 pixels every 150ms = ~13 pixels/second (~7 FPS updates)
 * - Loop: Seamless with spacing between repetitions
 */

#endif // SHARED_TYPES_H