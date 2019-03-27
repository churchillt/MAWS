/**
  Copyright(C) 2011-2014 Summit Semiconductor, LLC.  All Rights Reserved
 
 
  @file  Message.h

  @brief  This file contains the data and data type definitions that comprise
     an Olympus command/response message. 
 
  @date  Mar 03, 11  -  Created.
  @authors  Unknown
*/

#ifndef MESSAGE_H_
#define MESSAGE_H_



/******************************************************************************
 *    Includes
 *****************************************************************************/
#include <stdint.h>



/******************************************************************************
 *    Miscellaneous Command Definitions
 *****************************************************************************/
/**
 * @defgroup AH Misc SWM Command Definitions
 * @brief  Set up defines for data used with the SWM Messages.  The parser for SWM messages is in the Glenwood firmware.
 * @{ */
#define MESSAGE_PROTOCOL              (uint16_t)0x0101   ///< Message protocol ID used for SWM messages
#define MAX_PAYLOAD_LENGTH             300
#define MAX_HEADER_LENGTH              9      ///< 7 for header, 2 for payload length
#define MAX_MESSAGE_LENGTH             (MAX_PAYLOAD_LENGTH + MAX_HEADER_LENGTH)
#define MSG_WRITE                      0      ///< 0 to write a message to the SWM
#define MSG_READ                       1      ///< 1 to read a message to the SWM


#define MESSAGE_PAYLOAD_STATUS_INDEX    0x00  ///< Status is always returned in the
                                              ///< first byte of the payload
#define MESSAGE_PAYLOAD_DATA_INDEX      0x01  ///< If data is returned, it will be
                                              ///< placed at index 1 of the payload.
#define MESSAGE_STATUS_LENGTH           0x03  ///< Status payload is 3 bytes; the
                                              ///< first is the status code, the 
                                              ///< remaining two are the number of
                                              ///< bytes available to read from the
                                              ///< data port.
/** @} */



/******************************************************************************
 *    Message Structure Definition
 *****************************************************************************/
#pragma pack(1)
/**
 * @defgroup AI  SWM header, payload and message struct
 * @brief  Define structs for the SWM header, payload, and message.
 *   The message structure is used by the messaging system to execute
 *   functionality and get requested system information. The parser is 
 *   in the SWM firmware.
 * @{ */
typedef struct _msg_header_struct_
{
    uint16_t protocol;
    uint16_t checksum;
    uint8_t  readWrite;
    uint8_t  opcode; 
    uint8_t  secondaryOpcode;
} MESSAGE_HEADER;
/** @} */



/**
 * @addtogroup AI  SWM header, payload and message struct
 * @{
 */
typedef struct _msg_payload_struct_
{
    uint16_t length;
    uint8_t  data[MAX_PAYLOAD_LENGTH];
} MESSAGE_PAYLOAD;
/** @} */



/**
 * @addtogroup AI  SWM header, payload and message struct
 * @{ */
typedef struct _msg_struct_
{
    MESSAGE_HEADER header;
    MESSAGE_PAYLOAD payload;
} MESSAGE;
#pragma pack()
/** @} */



#endif    /* MESSAGE_H_ */

