/*
 * =====================================================================================
 *
 *                             ESP32 RDS STEREO ENCODER
 *                         Communication Manager Header
 *
 * =====================================================================================
 */

#ifndef COMMUNICATION_MANAGER_H
#define COMMUNICATION_MANAGER_H

#include "SharedTypes.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <Arduino.h>

/* =====================================================================================
 *                           COMMUNICATION MANAGER CLASS
 * =====================================================================================
 */

class CommunicationManager
{
public:
  /* =================================================================================
   *                               PUBLIC INTERFACE
   * =================================================================================
   */

  /**
   * @brief Initialize communication hardware and prepare for operation
   */
  static void init();

  /**
   * @brief Main FreeRTOS task function - runs continuously
   * @param parameter Unused FreeRTOS parameter (required by API)
   */
  static void taskFunction(void *parameter);

  /**
   * @brief Send PS/RT data to DisplayManager via queue
   * @param shortStr Station ID (PS) string (8 characters max)
   * @param longStr Radio text (RT) string (64 characters max)
   */
  static void sendDisplayUpdate(const char *shortStr, const char *longStr);

private:
  /* =================================================================================
   *                              COMMUNICATION SETUP
   * =================================================================================
   */

  /**
   * @brief Initialize Serial and Serial1 communication interfaces
   */
  static void initializeCommunication();

  /* =================================================================================
   *                            DATA FLOW MANAGEMENT
   * =================================================================================
   */

  /**
   * @brief Handle PC to MRDS1322 command bridging with collision avoidance
   */
  static void handlePCToMRDS();

  /**
   * @brief Handle automatic MRDS1322 polling for PS/RT data
   */
  static void handleMRDSPolling();

  /**
   * @brief Process all queued PC commands after polling
   */
  static void processQueuedCommands();

  /* =================================================================================
   *                          MRDS1322 PROTOCOL METHODS
   * =================================================================================
   */

  /**
   * @brief Read data from MRDS1322 memory using protocol commands
   * @param startAddr MRDS1322 memory address (e.g., 0xC8 for PS, 0x20 for RT)
   * @param len Number of bytes to read (8 for PS, 64 for RT)
   * @param buffer Output buffer for received data
   * @return true if read successful, false if timeout or error
   */
  static bool readMRDS1322(uint8_t startAddr, uint8_t len, uint8_t *buffer);

  /**
   * @brief Send read command to MRDS1322 with proper framing
   * @param startAddr Memory address to read from
   * @param len Number of bytes to read
   */
  static void sendMRDS1322Command(uint8_t startAddr, uint8_t len);

  /**
   * @brief Receive and validate response from MRDS1322
   * @param buffer Output buffer for payload data
   * @param maxLen Maximum buffer size
   * @param actualLen Actual bytes received (output parameter)
   * @return true if valid response received, false if timeout/invalid
   */
  static bool receiveMRDS1322Response(uint8_t *buffer, size_t maxLen,
                                      size_t *actualLen);

  /**
   * @brief Apply byte stuffing to command data for MRDS1322
   * @param input Raw command data
   * @param inputLen Input data length
   * @param output Buffer for stuffed data
   * @param outputLen Output buffer size (input), actual length (output)
   * @return true if stuffing successful, false if output buffer too small
   */
  static bool stuffData(const uint8_t *input, size_t inputLen, uint8_t *output, size_t *outputLen);

  /**
   * @brief Remove byte stuffing from MRDS1322 response data
   * @param input Stuffed response data
   * @param inputLen Input data length
   * @param output Buffer for unstuffed data
   * @param outputLen Output buffer size (input), actual length (output)
   * @return true if unstuffing successful, false if invalid stuffing
   */
  static bool unstuffData(const uint8_t *input, size_t inputLen, uint8_t *output, size_t *outputLen);

  /* =================================================================================
   *                             PRIVATE STATE VARIABLES
   * =================================================================================
   */

  static bool mrdsPollingActive;   // Collision avoidance flag
  static uint32_t lastPollingTime; // Timestamp of last MRDS1322 poll
  static const uint32_t POLLING_INTERVAL_MS = 2000; // 2-second polling interval

  // PC Interaction Tracking (for 3-second delay after PC commands)
  static uint32_t lastPCInteractionTime; // Timestamp of last PC command

  // String Change Detection (to avoid unnecessary display updates)
  static String previousPS; // Previous PS string for change detection
  static String previousRT; // Previous RT string for change detection
};

#endif // COMMUNICATION_MANAGER_H