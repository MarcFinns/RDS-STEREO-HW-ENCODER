# ESP32 RDS Stereo Encoder Bridge

A sophisticated dual-core ESP32 system that creates a transparent communication bridge between PC RDS software and MRDS1322 RDS encoder chips, while providing real-time visual display of RDS data and audio levels.

## System Overview

This project implements a transparent communication bridge between PC RDS software and MRDS1322 RDS encoder chip, while simultaneously providing real-time visual display of RDS data (PS/RT) and audio levels (VU meters) on a TFT display.

### Key Features

- **Dual-core ESP32 processing** for optimal performance
- **FreeRTOS task-based design** with priority optimization
- **Queue-based inter-task communication** for thread safety
- **Collision avoidance system** for seamless PC-MRDS1322 bridging
- **Real-time graphics** with ~60 FPS VU meter updates
- **Professional VU meters** with color-coded zones and peak detection

## Hardware Requirements

### Main Components
- **ESP32 DevKit** (dual-core processing)
- **MRDS1322 RDS encoder chip**
- **NV3007 428x142 TFT display**
- **Dual-channel audio input** for VU meters

### Hardware Design Files

The **`hardware/`** folder contains the complete PCB design for this project, including schematics and PCB layout. This hardware has been **built and fully tested** in real-world conditions.

### MRDS1322 RDS Encoder

The **MRDS1322** RDS encoder chip is from [Pira.cz](http://pira.cz). Datasheet available in the `hardware/` folder. Use the **TinyRDS** Windows program (also from Pira.cz) to send RDS data through this ESP32 bridge to the encoder.

## Pin Assignments

### TFT Display (NV3007) - SPI Interface
```
ESP32 Pin | Function    | NV3007 Pin
----------|-------------|-------------
GPIO19    | SCLK        | SCK
GPIO18    | MOSI        | SDA
GPIO16    | CS          | CS
GPIO17    | DC/RS       | DC
GPIO5     | RST         | RES
GPIO4     | Backlight   | LED
3.3V      | Power       | VCC
GND       | Ground      | GND
```

### Serial Communication
```
Interface | ESP32 Pin    | Baud Rate | Purpose
----------|------------- |-----------|------------------
Serial    | USB          | 19200     | PC Communication
Serial1   | GPIO21 (RX)  | 19200     | MRDS1322 RDS Chip
          | GPIO22 (TX)  |           |
```

### VU Meters - Audio Input (ADC)
```
Channel | ESP32 Pin | ADC Channel | Purpose
--------|-----------|-------------|----------------
Left    | GPIO39    | ADC1_CH3    | Left audio input
Right   | GPIO36    | ADC1_CH0    | Right audio input
```

### Complete Pin Reference Table
All pin assignments are centralized in `SharedTypes.h` for easy modification:

```
┌──────────────┬─────────────────────┬──────────────────────────┐
│ ESP32 GPIO   │ Function            │ Connected To             │
├──────────────┼─────────────────────┼──────────────────────────┤
│ GPIO21       │ SERIAL1_RX          │ MRDS1322 TX              │
│ GPIO22       │ SERIAL1_TX          │ MRDS1322 RX              │
│ GPIO16       │ TFT_CS              │ NV3007 Chip Select       │
│ GPIO5        │ TFT_RST             │ NV3007 Reset             │
│ GPIO17       │ TFT_DC              │ NV3007 Data/Command      │
│ GPIO18       │ TFT_MOSI            │ NV3007 SPI Data          │
│ GPIO19       │ TFT_SCLK            │ NV3007 SPI Clock         │
│ GPIO4        │ TFT_BL              │ NV3007 Backlight         │
│ GPIO39 (A3)  │ PIN_L (ADC1_CH3)    │ Left Audio Input         │
│ GPIO36 (A0)  │ PIN_R (ADC1_CH0)    │ Right Audio Input        │
│ USB (N/A)    │ Serial (PC Comm)    │ PC USB Connection        │
└──────────────┴─────────────────────┴──────────────────────────┘
```

**Note**: All pin defines are located in `SharedTypes.h` lines 136-151. To change pin assignments for PCB design, simply modify the `#define` values in that single location.

## System Architecture

### Communication Flow
```
PC Software → Serial USB → ESP32 Core 0 → Serial1 → MRDS1322
                ↓              ↓
         Response Routing  PS/RT Monitoring
                ↓              ↓
            PC Software    Core 1 → TFT Display
                               ↓
                          VU Meters (GPIO36/39)
```

### Task Architecture
- **CommunicationManager** (Core 0, Priority 1): PC-MRDS1322 bridging
- **DisplayManager** (Core 1, Priority 2): TFT graphics + VU meters
- **Queue-based data exchange** for thread-safe operation

## Display Layout

### TFT Display (428x142, Landscape)
```
┌─────────────────────────────────┐ ← Y=10
│         STATION ID              │   Station area (30px tall)
│      (Cyan, Size 3)             │   Auto-centered, space-trimmed
├─────────────────────────────────┤ ← Y=38
│   Scrolling Radio Text...       │   Text area (25px tall)
│     (White, Size 2)             │   Sliding window scrolling
├─────────────────────────────────┤ ← Y=75
│ L ████████████████████████████  │   VU Left (18px tall)
│   |   |  |  ||  |     dB Scale  │   Professional scale with
│  -20 -10 -3  0 +3   (colored)   │   color-coded bold labels
│   |   |  |  ||  |               │
│ R ████████████████████████████  │   VU Right (18px tall)
└─────────────────────────────────┘ ← Y=142
```

### VU Meter Features
- **Color-coded bars**: Green (safe) → Yellow (loud) → Orange (hot) → Red (peak)
- **Professional dB scale**: Centered between bars with color-coded bold labels
  - **-20/-10 dB**: Green (safe levels)
  - **-3 dB**: Yellow (caution level)
  - **0 dB**: White (reference level)
  - **+3 dB**: Red (peak/overload warning)
- **Peak markers**: White bars showing recent maximum levels (1-second hold)
- **Smooth ballistics**: Fast attack, slow release for natural VU behavior

## MRDS1322 Protocol

### Memory Map
- **PS (Program Service)**: Address 0xC8, Length 8 bytes
- **RT (Radio Text)**: Address 0x20, Length 64 bytes

### Protocol Frame Structure
```
Read Command:  FE D0 [ADDR] [LEN] FF
Response:      FE [DATA...] FF

Byte Stuffing Rules:
- Data byte 0xFD → FD 00
- Data byte 0xFE → FD 01
- Data byte 0xFF → FD 02
```

## Configuration

### VU Meter Parameters
```cpp
#define SAMPLE_WINDOW 2       // Samples per level calculation
#define ATTACK_STEP 3         // Fast attack (pixels/step)
#define RELEASE_STEP 4        // Slow release (pixels/step)
#define DECAY_INTERVAL_MS 20  // Decay timing (20ms)
#define PEAK_HOLD_MS 1000     // Peak hold time (1 second)
```

### Text Scrolling
```cpp
#define SCROLL_DELAY_MS 50    // Base scroll timing (multiplied by 3 = 150ms actual)
#define SCROLL_STEP_SIZE 2    // Pixels to advance per step (smooth motion)
```

### Display Features
- **Station ID**: Auto-centers after trimming trailing spaces
- **Radio Text**: Sliding window scrolling for long text, centered for short text
- **VU Scale**: Professional dB scale with bold, color-coded labels

## Performance Specifications

- **VU Meter Update Rate**: ~60 FPS (16ms loop time)
- **Text Scroll Speed**: Character-by-character at ~6.7 FPS (150ms per step)
- **MRDS1322 Polling**: Every 2 seconds
- **Peak Marker Hold**: 1 second
- **Audio Sampling**: 12-bit ADC (0-4095 range)
- **Display Updates**: Change detection prevents unnecessary redraws

## Build Dependencies

### Required Libraries
```cpp
#include <Arduino.h>
#include <Arduino_GFX_Library.h>    // TFT display driver (NV3007)
#include "freertos/FreeRTOS.h"      // Real-time OS
#include "freertos/queue.h"         // Inter-task communication
#include "freertos/task.h"          // Task management
```

## File Structure

```
RDS_STEREO_ENCODER_CLEAN/
├── RDS_STEREO_ENCODER_CLEAN.ino  # Main Arduino sketch
├── CommunicationManager.h        # PC-MRDS1322 bridge header
├── CommunicationManager.cpp      # Communication implementation
├── DisplayManager.h              # Display management header
├── DisplayManager.cpp            # TFT/VU meter implementation
├── SharedTypes.h                 # Pin configuration & data structures
├── splashscreen.h                # Splash screen data header
├── splashscreen.cpp              # Splash screen bitmap data
└── README.md                     # This documentation
```

## Usage

1. **Hardware Setup**: Connect all components according to pin assignments
2. **Software Setup**: Install required libraries in Arduino IDE
3. **Configuration**: Adjust compile-time options in SharedTypes.h
4. **Upload**: Flash the code to ESP32 DevKit
5. **PC Software**: Connect RDS software to ESP32's Serial port at 19200 baud
6. **MRDS1322**: Connect RDS encoder chip to Serial1 at 19200 baud

## License

Created by Claude Code Assistant, 2024
Version 2.0

## Technical Notes

- The system uses ESP32's dual-core architecture for optimal performance
- Core 0 handles communication with lower priority for smooth graphics
- Core 1 handles display updates with higher priority for responsive VU meters
- Queue-based communication ensures thread-safe data exchange
- Change detection prevents unnecessary screen updates and reduces flicker
- Logarithmic audio scaling provides perceptual accuracy for VU meters

## Pin Configuration for PCB Design

All hardware pin assignments are centralized in **`SharedTypes.h`** (lines 136-151) under the section "ESP32 HARDWARE PIN CONFIGURATION". This design makes it easy to modify pins for custom PCB layouts:

1. **Serial Communication**: `SERIAL1_RX`, `SERIAL1_TX` (GPIO21/22)
2. **TFT Display**: `TFT_CS`, `TFT_RST`, `TFT_DC`, `TFT_MOSI`, `TFT_SCLK`, `TFT_BL` (GPIO16/5/17/18/19/4)
3. **VU Meter ADC**: `PIN_L`, `PIN_R` (GPIO39/36)

**To modify pins**: Simply edit the `#define` values in `SharedTypes.h`. The code automatically uses these definitions throughout - no need to search and replace in multiple files.