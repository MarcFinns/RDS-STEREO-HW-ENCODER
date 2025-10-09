/*
 * =====================================================================================
 *
 *                             ESP32 RDS STEREO ENCODER
 *                           Main Arduino Application
 *
 * =====================================================================================
 *
 * File:         RDS_Multitask_Shell.ino
 * Description:  Main Arduino sketch for ESP32 RDS encoder bridge system with
 *               sophisticated dual-task architecture, queue-based
 * communication, and comprehensive display management.
 *
 * System Overview:
 *   This project creates a transparent communication bridge between PC RDS
 * software and MRDS1322 RDS encoder chip, while simultaneously providing
 * real-time visual display of RDS data (PS/RT) and audio levels (VU meters) on
 * TFT display.
 *
 * Architecture Highlights:
 *   • Dual-core ESP32 processing for optimal performance
 *   • FreeRTOS task-based design with priority optimization
 *   • Queue-based inter-task communication for thread safety
 *   • Collision avoidance system for seamless PC-MRDS1322 bridging
 *   • Real-time graphics with ~60 FPS VU meter updates
 *
 * Hardware Integration:
 *   • ESP32 DevKit (dual-core processing)
 *   • MRDS1322 RDS encoder chip (Serial1, GPIO21/22, 19200 baud)
 *   • NV3007 428x142 TFT display (SPI, GPIO16/17/18/19, landscape mode)
 *   • Dual-channel audio input for VU meters (GPIO36/39, ADC pins A0/A3)
 *   • PC connection via USB Serial (19200 baud)
 *
 * Communication Flow:
 *   PC Software → Serial USB → ESP32 Core 0 → Serial1 → MRDS1322
 *                    ↓              ↓
 *             Response Routing  PS/RT Monitoring
 *                    ↓              ↓
 *                PC Software    Core 1 → TFT Display
 *                                   ↓
 *                               VU Meters
 *
 * Task Architecture:
 *   • CommunicationManager (Core 0, Priority 1): PC-MRDS1322 bridging
 *   • DisplayManager (Core 1, Priority 2): TFT graphics + VU meters
 *   • Queue-based data exchange for thread-safe operation
 *
 * Author:       Claude Code Assistant
 * Created:      2024
 * Version:      2.0
 *
 * =====================================================================================
 */

// System Component Headers
#include "CommunicationManager.h" // PC-ESP32-MRDS1322 communication bridge
#include "DisplayManager.h"       // TFT display, VU meters
#include "SharedTypes.h"          // Common data structures and constants

// FreeRTOS Headers
#include "freertos/FreeRTOS.h" // FreeRTOS core functionality
#include "freertos/queue.h"    // Queue-based inter-task communication
#include "freertos/task.h"     // Task creation and management

/* =====================================================================================
 *                          FREERTOS QUEUE HANDLE DEFINITIONS
 * =====================================================================================
 */

// Inter-task Communication Queues (declared in SharedTypes.h)
QueueHandle_t displayQueue; // PS/RT data: CommunicationManager → DisplayManager
QueueHandle_t pcCommandQueue; // PC command buffer for collision avoidance

/* =====================================================================================
 *                           FREERTOS TASK HANDLE DEFINITIONS
 * =====================================================================================
 */

// Task Management Handles for Runtime Control
TaskHandle_t displayTaskHandle;       // Handle for DisplayManager task
TaskHandle_t communicationTaskHandle; // Handle for CommunicationManager task

/* =====================================================================================
 *                           ARDUINO FRAMEWORK FUNCTIONS
 * =====================================================================================
 */

/**
 * @brief Arduino setup function - system initialization and task creation
 *
 * Complete system initialization with robust error handling:
 *   • FreeRTOS queue creation with failure detection
 *   • Hardware initialization for both managers
 *   • Dual-task creation with core affinity and priority assignment
 *   • Infinite loop on initialization failure (system halt)
 *
 * Initialization Sequence:
 *   1. Create inter-task communication queues
 *   2. Initialize display hardware (TFT + ADC)
 *   3. Initialize communication hardware (Serial + Serial1)
 *   4. Create high-priority DisplayManager task on Core 1
 *   5. Create lower-priority CommunicationManager task on Core 0
 *   6. Transfer control to FreeRTOS scheduler
 */
void setup()
{
  // ===== INTER-TASK COMMUNICATION QUEUE CREATION =====

  /* ===== DISPLAY DATA QUEUE =====
   * Purpose: PS/RT data transfer from CommunicationManager to DisplayManager
   * Size: 10 messages (sufficient buffer for smooth updates)
   * Data: DisplayData structure (PS + RT strings)
   */
  displayQueue = xQueueCreate(10, sizeof(DisplayData));
  if (displayQueue == NULL)
  {
    // ===== CRITICAL FAILURE: DISPLAY QUEUE =====
    // System cannot function without display data communication
    while (1)
    {
      delay(1000); // Infinite loop indicates queue creation failure
    }
  }

  /* ===== PC COMMAND QUEUE =====
   * Purpose: PC command buffering during MRDS1322 polling (collision avoidance)
   * Size: 5 messages (sufficient for PC command buffering)
   * Data: PCCommand structure (raw command bytes + length)
   */
  pcCommandQueue = xQueueCreate(5, sizeof(PCCommand));
  if (pcCommandQueue == NULL)
  {
    // ===== CRITICAL FAILURE: PC COMMAND QUEUE =====
    // System cannot provide transparent bridging without command queue
    while (1)
    {
      delay(1000); // Infinite loop indicates queue creation failure
    }
  }

  // ===== HARDWARE SUBSYSTEM INITIALIZATION =====

  /* ===== DISPLAY SUBSYSTEM INITIALIZATION =====
   * Initialize TFT display and VU meter ADCs
   * Sets up graphics hardware and displays initial content
   */
  DisplayManager::init();

  /* ===== COMMUNICATION SUBSYSTEM INITIALIZATION =====
   * Initialize Serial (PC) and Serial1 (MRDS1322) interfaces
   * Prepares for transparent PC-MRDS1322 bridging operation
   */
  CommunicationManager::init();

  // ===== FREERTOS TASK CREATION =====

  /* ===== HIGH-PRIORITY DISPLAY TASK (CORE 1) =====
   * Task: DisplayManager::taskFunction
   * Core: 1 (dedicated to display processing for smooth graphics)
   * Priority: 2 (high priority for responsive VU meters and text updates)
   * Stack: 4096 bytes (sufficient for display operations and buffers)
   * Function: ~60 FPS VU meters, text scrolling, PS/RT display updates
   */
  xTaskCreatePinnedToCore(DisplayManager::taskFunction, // Task function
                          "DisplayTask",      // Task name (for debugging)
                          4096,               // Stack size in words
                          NULL,               // Task parameter (unused)
                          2,                  // Task priority (high)
                          &displayTaskHandle, // Task handle (for control)
                          1);                 // CPU core (Core 1)

  /* ===== LOWER-PRIORITY COMMUNICATION TASK (CORE 0) =====
   * Task: CommunicationManager::taskFunction
   * Core: 0 (communication processing core, shared with Arduino framework)
   * Priority: 1 (lower than display to ensure smooth graphics)
   * Stack: 4096 bytes (sufficient for protocol buffers and processing)
   * Function: PC-MRDS1322 bridging, PS/RT polling, collision avoidance
   */
  xTaskCreatePinnedToCore(CommunicationManager::taskFunction, // Task function
                          "CommunicationTask",      // Task name (for debugging)
                          4096,                     // Stack size in words
                          NULL,                     // Task parameter (unused)
                          1,                        // Task priority (lower)
                          &communicationTaskHandle, // Task handle (for control)
                          0);                       // CPU core (Core 0)

  // ===== INITIALIZATION COMPLETE =====
  // Control now transfers to FreeRTOS scheduler
  // Tasks will begin execution based on priority and core assignment
}

/**
 * @brief Arduino loop function - FreeRTOS idle task placeholder
 *
 * In this FreeRTOS-based design, the main Arduino loop() function serves only
 * as an idle task placeholder. All actual work is performed by the dedicated
 * FreeRTOS tasks (DisplayManager and CommunicationManager).
 *
 * Loop Behavior:
 *   • 1-second delay to minimize CPU usage
 *   • Allows FreeRTOS scheduler to manage task execution
 *   • Provides low-priority background operation if needed
 *   • Maintains Arduino framework compatibility
 */
void loop()
{
  // ===== IDLE TASK OPERATION =====
  // Long delay allows FreeRTOS tasks to handle all system operations
  vTaskDelay(pdMS_TO_TICKS(1000)); // 1-second delay for minimal CPU usage
}

/* =====================================================================================
 *                              END OF APPLICATION
 * =====================================================================================
 */