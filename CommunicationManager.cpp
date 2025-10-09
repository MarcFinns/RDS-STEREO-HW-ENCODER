/*
 * =====================================================================================
 *
 *                             ESP32 RDS STEREO ENCODER
 *                      Communication Manager Implementation
 *
 * =====================================================================================
 */

#include "CommunicationManager.h"

/* =====================================================================================
 *                          STATIC MEMBER VARIABLE DEFINITIONS
 * =====================================================================================
 */

// Collision Avoidance State Management
bool CommunicationManager::mrdsPollingActive = false;
uint32_t CommunicationManager::lastPollingTime = 0;

// PC Interaction Tracking (for 3-second delay)
uint32_t CommunicationManager::lastPCInteractionTime = 0;

// String Change Detection (to avoid unnecessary display updates)
String CommunicationManager::previousPS = "";
String CommunicationManager::previousRT = "";

/* =====================================================================================
 *                               PUBLIC INTERFACE
 * =====================================================================================
 */

void CommunicationManager::init() { initializeCommunication(); }

void CommunicationManager::taskFunction(void *parameter)
{
  while (true)
  {
    // ===== PC COMMAND PROCESSING =====
    handlePCToMRDS();

    // ===== MRDS1322 POLLING =====
    if (!mrdsPollingActive)
    {
      handleMRDSPolling();
    }

    // ===== TASK TIMING =====
    vTaskDelay(pdMS_TO_TICKS(10)); // 10ms loop
  }
}

/* =====================================================================================
 *                            SIMPLE COMMAND HANDLING
 * =====================================================================================
 */

void CommunicationManager::handlePCToMRDS()
{
  if (Serial.available())
  {
    // ===== SIMPLE BYTE COLLECTION =====
    uint8_t commandData[128];
    size_t commandLength = 0;

    // Read all available bytes
    while (Serial.available() && commandLength < sizeof(commandData))
    {
      commandData[commandLength] = Serial.read();
      commandLength++;
    }

    if (commandLength == 0)
      return;

    // ===== RECORD PC INTERACTION =====
    lastPCInteractionTime = millis();

    // ===== COLLISION AVOIDANCE =====
    if (mrdsPollingActive)
    {
      // Queue during polling
      PCCommand cmd;
      memcpy(cmd.data, commandData, commandLength);
      cmd.length = commandLength;
      cmd.timestamp = millis();

      xQueueSend(pcCommandQueue, &cmd, 0);
    }
    else
    {
      // Forward immediately

      Serial1.write(commandData, commandLength);

      // Simple response collection
      uint8_t response[128];
      size_t responseLen = 0;
      uint32_t timeout = millis() + 1500;

      while (millis() < timeout && responseLen < sizeof(response))
      {
        if (Serial1.available())
        {
          response[responseLen++] = Serial1.read();

          // Stop on FF (frame end)
          if (response[responseLen - 1] == 0xFF)
          {
            break;
          }
        }
      }

      // Forward response to PC
      if (responseLen > 0)
      {
        Serial.write(response, responseLen);
      }
    }
  }
}

/* =====================================================================================
 *                               SIMPLE POLLING
 * =====================================================================================
 */

void CommunicationManager::handleMRDSPolling()
{
  uint32_t currentTime = millis();

  // ===== 3-SECOND DELAY AFTER PC INTERACTION =====
  if (currentTime - lastPCInteractionTime < 3000)
  {
    return; // Wait 3 seconds after last PC command
  }

  // ===== 2-SECOND POLLING INTERVAL =====
  if (currentTime - lastPollingTime >= POLLING_INTERVAL_MS)
  {
    // ===== START COLLISION AVOIDANCE =====
    mrdsPollingActive = true;

    // ===== READ PS DATA =====
    uint8_t psBuffer[8]; // 8 characters (data only)
    bool psSuccess = readMRDS1322(MRDS1322_PS_ADDR, MRDS1322_PS_LEN, psBuffer);

    taskYIELD(); // Allow other tasks to run

    // ===== READ RT DATA =====
    uint8_t rtBuffer[64]; // 64 characters (data only)
    bool rtSuccess = readMRDS1322(MRDS1322_RT_ADDR, MRDS1322_RT_LEN, rtBuffer);

    // ===== ADDITIONAL DATA VALIDATION =====
    // Sanity check: Ensure received data contains at least one printable ASCII
    // character. This prevents garbage data from being displayed if MRDS1322
    // returns invalid or uninitialized memory.

    // Validate PS data (8 bytes)
    if (psSuccess)
    {
      bool validPS = false;
      for (int i = 0; i < 8; i++)
      {
        // Check for printable ASCII (space to tilde: 32-126) or null terminator
        if ((psBuffer[i] >= 32 && psBuffer[i] <= 126) || psBuffer[i] == 0)
        {
          validPS = true; // Found at least one valid character
          break;
        }
      }
      if (!validPS) // All bytes were invalid
      {
        psSuccess = false; // Reject entire PS data
      }
    }

    // Validate RT data (64 bytes)
    if (rtSuccess)
    {
      bool validRT = false;
      for (int i = 0; i < 64; i++)
      {
        // Check for printable ASCII (space to tilde: 32-126) or null terminator
        if ((rtBuffer[i] >= 32 && rtBuffer[i] <= 126) || rtBuffer[i] == 0)
        {
          validRT = true; // Found at least one valid character
          break;
        }
      }
      if (!validRT) // All bytes were invalid
      {
        rtSuccess = false; // Reject entire RT data
      }
    }

    // ===== END COLLISION AVOIDANCE =====
    mrdsPollingActive = false;

    // ===== PROCESS DATA =====
    if (psSuccess && rtSuccess)
    {
      char psString[9];
      char rtString[65];

      // Copy PS data (already just the 8 characters)
      memcpy(psString, psBuffer, 8);
      psString[8] = '\0';

      // Copy RT data (already just the 64 characters)
      memcpy(rtString, rtBuffer, 64);
      rtString[64] = '\0';

      // Remove trailing spaces from RT
      for (int i = 63; i >= 0; i--)
      {
        if (rtString[i] != ' ')
        {
          rtString[i + 1] = '\0';
          break;
        }
      }

      // ===== CHECK FOR CHANGES =====
      String currentPS = String(psString);
      String currentRT = String(rtString);

      if (currentPS != previousPS || currentRT != previousRT)
      {
        // ===== SEND TO DISPLAY MANAGER =====
        sendDisplayUpdate(psString, rtString);

        // ===== UPDATE CACHE =====
        previousPS = currentPS;
        previousRT = currentRT;
      }
    }

    // ===== PROCESS QUEUED COMMANDS =====
    processQueuedCommands();

    // ===== UPDATE POLLING TIMER =====
    // Only update timer AFTER polling attempt completes (success or failure)
    lastPollingTime = currentTime;
  }
}

/* =====================================================================================
 *                          SIMPLE MRDS1322 PROTOCOL
 * =====================================================================================
 */

bool CommunicationManager::readMRDS1322(uint8_t startAddr, uint8_t len,
                                        uint8_t *buffer)
{
  // Clear any stale data from previous communications
  while (Serial1.available())
  {
    Serial1.read();
  }

  // Send simple read command
  sendMRDS1322Command(startAddr, len);

  // Receive response
  size_t responseLen;
  if (receiveMRDS1322Response(buffer, len, &responseLen))
  {
    // Validate that we received the expected amount of data
    if (responseLen == len)
    {
      return true; // Success
    }
    else
    {
      // Got a response but wrong length - likely not a real MRDS response
      return false;
    }
  }

  return false; // Failed
}

/**
 * @brief Send read command to MRDS1322 using protocol frame structure
 * @param startAddr MRDS1322 memory address to read from
 * @param len Number of bytes to read from that address
 *
 * Protocol Frame Format:
 *   FE <stuffed_params> FF
 *   where stuffed_params = byte-stuffed version of [D0, startAddr, len]
 *
 * The D0 command tells MRDS1322 to read from memory. Parameters are byte-stuffed
 * to ensure frame delimiters (FE, FF) don't appear in the data stream.
 */
void CommunicationManager::sendMRDS1322Command(uint8_t startAddr, uint8_t len)
{
  // ===== PREPARE COMMAND PARAMETERS =====
  // D0 = Read command, followed by start address and length
  uint8_t rawParams[] = {0xD0, startAddr, len};
  uint8_t stuffedParams[10]; // Max size after stuffing (3*2 + margin)
  size_t stuffedLen = sizeof(stuffedParams);

  // ===== APPLY BYTE STUFFING =====
  if (!stuffData(rawParams, sizeof(rawParams), stuffedParams, &stuffedLen))
  {
    return; // Stuffing failed (should never happen with this buffer size)
  }

  // ===== SEND PROTOCOL FRAME =====
  Serial1.write(0xFE);                    // Start byte (FE - never stuffed)
  Serial1.write(stuffedParams, stuffedLen); // Stuffed command parameters
  Serial1.write(0xFF);                    // End byte (FF - never stuffed)
}

/**
 * @brief Receive and parse MRDS1322 response frame with robust error handling
 * @param buffer Output buffer for unstuffed response data
 * @param maxLen Maximum size of output buffer
 * @param actualLen Pointer to store actual number of bytes received
 * @return true if valid response received and parsed, false on timeout or error
 *
 * Response Frame Format:
 *   FE <address> <stuffed_data> FF
 *
 * This function:
 *   1. Waits for frame start byte (FE) - discards any garbage before it
 *   2. Collects all bytes until frame end byte (FF) or timeout
 *   3. Validates frame structure (has both FE and FF)
 *   4. Extracts data payload (skips FE and address byte)
 *   5. Applies byte unstuffing to restore original data
 *   6. Returns unstuffed data in buffer with actual length
 *
 * Timeouts:
 *   - 1000ms overall timeout for complete response
 *   - 100ms timeout if no data received at all (MRDS1322 not connected)
 */
bool CommunicationManager::receiveMRDS1322Response(uint8_t *buffer,
                                                   size_t maxLen,
                                                   size_t *actualLen)
{
  uint8_t rawResponse[128];  // Raw frame buffer (includes FE, address, FF)
  size_t rawLen = 0;         // Current raw buffer position
  uint32_t timeout = millis() + 1000; // Overall timeout (1 second)
  bool startFound = false;   // Have we found the FE start byte?
  size_t frameStart = 0;     // Position where FE was found
  uint32_t noDataTimeout = millis() + 100; // Timeout if no data at all

  // ===== COLLECT RESPONSE FRAME WITH SYNCHRONIZATION =====
  while (millis() < timeout && rawLen < sizeof(rawResponse))
  {
    if (Serial1.available())
    {
      uint8_t byte = Serial1.read();
      noDataTimeout = millis() + 100; // Reset no-data timeout when we get data

      if (!startFound)
      {
        // Look for frame start byte, discarding any stale data
        if (byte == 0xFE)
        {
          startFound = true;
          frameStart = rawLen;
          rawResponse[rawLen++] = byte;
        }
        // Discard bytes until we find frame start
      }
      else
      {
        rawResponse[rawLen++] = byte;
        if (byte == 0xFF)
          break; // Frame complete
      }
    }
    else
    {
      // If we haven't received any data at all within the timeout, MRDS likely
      // not connected
      if (millis() > noDataTimeout && rawLen == 0)
      {
        return false;
      }
    }
  }

  // ===== VALIDATE FRAME STRUCTURE =====
  // Ensure we found a start byte (FE) - if not, communication failed
  if (!startFound)
  {
    return false;
  }

  // Verify minimum frame length and start byte position
  if (rawLen < frameStart + 2 || rawResponse[frameStart] != 0xFE)
  {
    return false; // Frame too short or invalid start byte
  }

  // ===== FIND FRAME END BYTE =====
  // Search for FF end byte from the frame start position
  size_t endPos = 0;
  for (size_t i = frameStart + 1; i < rawLen; i++)
  {
    if (rawResponse[i] == 0xFF)
    {
      endPos = i;
      break;
    }
  }

  if (endPos == 0)
  {
    return false; // No end byte found - incomplete frame
  }

  // ===== EXTRACT STUFFED PAYLOAD =====
  // Calculate payload position and length (between address byte and end byte)
  // Frame structure: FE <address> <payload> FF
  size_t stuffedPayloadLen =
      endPos - frameStart - 2; // Exclude FE and address byte
  uint8_t *stuffedPayload =
      rawResponse + frameStart + 2; // Skip FE and address byte

  // Validate minimum payload size - MRDS should always return some data
  if (stuffedPayloadLen == 0)
  {
    return false; // Empty payload - invalid response
  }

  // ===== APPLY BYTE UNSTUFFING =====
  // Convert stuffed payload back to original data
  size_t unstuffedLen = maxLen;
  if (!unstuffData(stuffedPayload, stuffedPayloadLen, buffer, &unstuffedLen))
  {
    return false; // Unstuffing failed - corrupted data
  }

  // ===== FINAL VALIDATION =====
  // Verify we got actual data (not just empty after unstuffing)
  // For PS data we expect 8 bytes, for RT data we expect 64 bytes
  if (unstuffedLen == 0)
  {
    return false; // No data after unstuffing
  }

  // ===== SUCCESS =====
  *actualLen = unstuffedLen;
  return true;
}

/* =====================================================================================
 *                            BYTE STUFFING/UNSTUFFING
 * =====================================================================================
 */

bool CommunicationManager::stuffData(const uint8_t *input, size_t inputLen,
                                     uint8_t *output, size_t *outputLen)
{
  size_t maxOutputLen = *outputLen;
  size_t outputPos = 0;

  // Byte stuffing rules:
  // Data byte FD → FD 00
  // Data byte FE → FD 01
  // Data byte FF → FD 02

  for (size_t i = 0; i < inputLen; i++)
  {
    uint8_t byte = input[i];

    if (byte == 0xFD || byte == 0xFE || byte == 0xFF)
    {
      // Need 2 bytes for stuffed output
      if (outputPos + 2 > maxOutputLen)
      {
        return false; // Output buffer too small
      }

      output[outputPos++] = 0xFD; // Escape byte

      if (byte == 0xFD)
        output[outputPos++] = 0x00;
      else if (byte == 0xFE)
        output[outputPos++] = 0x01;
      else // byte == 0xFF
        output[outputPos++] = 0x02;
    }
    else
    {
      // Normal byte - copy directly
      if (outputPos + 1 > maxOutputLen)
      {
        return false; // Output buffer too small
      }
      output[outputPos++] = byte;
    }
  }

  *outputLen = outputPos;
  return true;
}

bool CommunicationManager::unstuffData(const uint8_t *input, size_t inputLen,
                                       uint8_t *output, size_t *outputLen)
{
  size_t maxOutputLen = *outputLen;
  size_t outputPos = 0;
  size_t inputPos = 0;

  // Byte unstuffing rules:
  // FD 00 → FD
  // FD 01 → FE
  // FD 02 → FF

  while (inputPos < inputLen)
  {
    uint8_t byte = input[inputPos];

    if (byte == 0xFD)
    {
      // Check if we have escape sequence
      if (inputPos + 1 >= inputLen)
      {
        return false; // Invalid stuffing - escape byte at end
      }

      uint8_t escapedByte = input[inputPos + 1];
      if (outputPos >= maxOutputLen)
      {
        return false; // Output buffer too small
      }

      if (escapedByte == 0x00)
        output[outputPos++] = 0xFD;
      else if (escapedByte == 0x01)
        output[outputPos++] = 0xFE;
      else if (escapedByte == 0x02)
        output[outputPos++] = 0xFF;
      else
      {
        return false; // Invalid escape sequence
      }

      inputPos += 2; // Skip both escape and escaped bytes
    }
    else
    {
      // Normal byte - copy directly
      if (outputPos >= maxOutputLen)
      {
        return false; // Output buffer too small
      }
      output[outputPos++] = byte;
      inputPos++;
    }
  }

  *outputLen = outputPos;
  return true;
}

/* =====================================================================================
 *                            SIMPLE QUEUE PROCESSING
 * =====================================================================================
 */

/**
 * @brief Process all queued PC commands that were buffered during MRDS1322 polling
 *
 * Command Queue Processing Flow:
 *   1. Check queue for buffered PC commands (non-blocking)
 *   2. Forward each command to MRDS1322 via Serial1
 *   3. Wait for MRDS1322 response (up to 1 second timeout)
 *   4. Forward MRDS1322 response back to PC via Serial
 *   5. Repeat until queue is empty
 *
 * This function is called after MRDS1322 polling completes to ensure PC commands
 * that arrived during polling are not lost. This maintains transparent bridging
 * behavior even during automatic PS/RT data collection.
 */
void CommunicationManager::processQueuedCommands()
{
  PCCommand cmd;

  // Process all queued commands (non-blocking dequeue)
  while (xQueueReceive(pcCommandQueue, &cmd, 0) == pdTRUE)
  {
    // ===== FORWARD COMMAND TO MRDS1322 =====
    Serial1.write(cmd.data, cmd.length);

    // ===== COLLECT MRDS1322 RESPONSE =====
    uint8_t response[128];  // Response buffer
    size_t responseLen = 0; // Current response length
    uint32_t timeout = millis() + 1000; // 1 second timeout

    // Wait for complete response frame (ends with 0xFF)
    while (millis() < timeout && responseLen < sizeof(response))
    {
      if (Serial1.available())
      {
        response[responseLen++] = Serial1.read();
        if (response[responseLen - 1] == 0xFF) // Stop byte found
          break; // Frame complete
      }
    }

    // ===== FORWARD RESPONSE TO PC =====
    if (responseLen > 0)
    {
      Serial.write(response, responseLen);
    }
  }
}

/* =====================================================================================
 *                          INTER-TASK COMMUNICATION
 * =====================================================================================
 */

void CommunicationManager::sendDisplayUpdate(const char *shortStr,
                                             const char *longStr)
{
  DisplayData data;

  strncpy(data.shortString, shortStr, sizeof(data.shortString) - 1);
  data.shortString[sizeof(data.shortString) - 1] = '\0';

  strncpy(data.longString, longStr, sizeof(data.longString) - 1);
  data.longString[sizeof(data.longString) - 1] = '\0';

  xQueueSend(displayQueue, &data, pdMS_TO_TICKS(10));
}

/* =====================================================================================
 *                          HARDWARE INITIALIZATION
 * =====================================================================================
 */

void CommunicationManager::initializeCommunication()
{
  // PC Communication (19200 baud to match TinyRDS)
  Serial.begin(19200);

  // MRDS1322 Communication
  Serial1.begin(19200, SERIAL_8N1, SERIAL1_RX, SERIAL1_TX);
}