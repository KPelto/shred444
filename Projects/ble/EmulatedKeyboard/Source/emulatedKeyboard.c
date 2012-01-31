/**************************************************************************************************
  Filename:       emulatedKeyboard.c
  Revised:        $Date: 2010-08-06 08:56:11 -0700 (Fri, 06 Aug 2010) $
  Revision:       $Revision: 23333 $

  Description: Emulated Keyboard application source code.

  Copyright 2011 Texas Instruments Incorporated. All rights reserved.

  IMPORTANT: Your use of this Software is limited to those specific rights
  granted under the terms of a software license agreement between the user
  who downloaded the software, his/her employer (which must be your employer)
  and Texas Instruments Incorporated (the "License").  You may not use this
  Software unless you agree to abide by the terms of the License. The License
  limits your use, and you acknowledge, that the Software may not be modified,
  copied or distributed unless embedded on a Texas Instruments microcontroller
  or used solely and exclusively in conjunction with a Texas Instruments radio
  frequency transceiver, which is integrated into your product.  Other than for
  the foregoing purpose, you may not use, reproduce, copy, prepare derivative
  works of, modify, distribute, perform, display or sell this Software and/or
  its documentation for any purpose.

  YOU FURTHER ACKNOWLEDGE AND AGREE THAT THE SOFTWARE AND DOCUMENTATION ARE
  PROVIDED �AS IS� WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED,
  INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY, TITLE,
  NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL
  TEXAS INSTRUMENTS OR ITS LICENSORS BE LIABLE OR OBLIGATED UNDER CONTRACT,
  NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR OTHER
  LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES
  INCLUDING BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT, PUNITIVE
  OR CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF PROCUREMENT
  OF SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY CLAIMS BY THIRD PARTIES
  (INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), OR OTHER SIMILAR COSTS.

  Should you have any questions regarding your right to use this Software,
  contact Texas Instruments Incorporated at www.TI.com.
**************************************************************************************************/

/*********************************************************************
 * INCLUDES
 */

#include "bcomdef.h"
#include "OSAL.h"
#include "OSAL_PwrMgr.h"

#include "OnBoard.h"
#include "hal_adc.h"
#include "hal_led.h"
#include "hal_key.h"

#include "gatt.h"
#include "gap.h"
#include "hci.h"

#include "gapgattserver.h"
#include "gattservapp.h"
#include "hidKeyboardProfile.h"

#include "peripheral.h"

#include "hal_scancodes.h"

#include "emulatedKeyboard.h"

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */

#define DEVICE_ADDRESS { 0x22, 0x22, 0x22, 0x22, 0x22, 0x22 }

// How often to perform periodic event
#define PERIODIC_EVT_PERIOD                  5000

// Whether to enable automatic parameter update request when a connection is formed
#define DEFAULT_ENABLE_UPDATE_REQUEST         FALSE

// Minimum connection interval (units of 1.25ms, 80=100ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL     8

// Maximum connection interval (units of 1.25ms, 800=1000ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL     8

// Slave latency to use if automatic parameter update request is enabled
#define DEFAULT_DESIRED_SLAVE_LATENCY         0

// Supervision timeout value (units of 10ms, 1000=10s) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_CONN_TIMEOUT          500

// Company Identifier: Texas Instruments Inc. (13)
#define TI_COMPANY_ID                         0x000D

#define INVALID_CONNHANDLE                    0xFFFF


/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static uint8 emulatedKeyboard_TaskID;   // Task ID for internal task/event processing

static uint8 bdAddr[B_ADDR_LEN] = DEVICE_ADDRESS;

// GAP Profile - Name attribute for SCAN RSP data
static uint8 scanRspData[] =
{
  0x12,   // length of this data
  GAP_ADTYPE_LOCAL_NAME_COMPLETE,   // AD Type = Complete local name
  0x45,   // 'E'
  0x6d,   // 'm' 
  0x75,   // 'u'
  0x6c,   // 'l'
  0x61,   // 'a'
  0x74,   // 't'
  0x65,   // 'e'
  0x64,   // 'd'
  0x20,   // ' '
  0x4b,   // 'K'
  0x65,   // 'e'
  0x79,   // 'y'
  0x62,   // 'b'
  0x6f,   // 'o'
  0x61,   // 'a'
  0x72,   // 'r'
  0x64    // 'd'
};

static uint8 advertData[] = 
{ 
  0x02,   // length of this data
  GAP_ADTYPE_FLAGS,
  GAP_ADTYPE_FLAGS_LIMITED | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
};

// GAP GATT Attributes
static uint8 attDeviceName[] = "Emulated Keyboard";

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void emulatedKeyboard_ProcessOSALMsg( osal_event_hdr_t *pMsg );
static void peripheralStateNotificationCB( gaprole_States_t newState );
static void performPeriodicTask( void );

#if defined( CC2540_MINIDK )
static void emulatedKeyboard_HandleKeys( uint8 shift, uint8 keys );
#endif

/*********************************************************************
 * PROFILE CALLBACKS
 */

// GAP Role Callbacks
static gapRolesCBs_t emulatedKeyboard_PeripheralCBs =
{
  peripheralStateNotificationCB,  // Profile State Change Callbacks
  NULL                // When a valid RSSI is read from controller
};

/*********************************************************************
 * PUBLIC FUNCTIONS
 */

/*********************************************************************
 * @fn      EmulatedKeyboard_Init
 *
 * @brief   Initialization function for the Emulated Keyboard App Task.
 *          This is called during initialization and should contain
 *          any application specific initialization (ie. hardware
 *          initialization/setup, table initialization, power up
 *          notificaiton ... ).
 *
 * @param   task_id - the ID assigned by OSAL.  This ID should be
 *                    used to send messages and set timers.
 *
 * @return  none
 */
void EmulatedKeyboard_Init( uint8 task_id )
{
  emulatedKeyboard_TaskID = task_id;

  
  HCI_EXT_SetBDADDRCmd( bdAddr );
  
  // Setup the GAP Peripheral Role Profile
  {
    uint8 initial_advertising_enable = TRUE;


    // By setting this to zero, the device will go into the waiting state after
    // being discoverable for 30.72 second, and will not being advertising again
    // until the enabler is set back to TRUE
    uint16 gapRole_AdvertOffTime = 0;
      
    uint8 enable_update_request = DEFAULT_ENABLE_UPDATE_REQUEST;
    uint16 desired_min_interval = DEFAULT_DESIRED_MIN_CONN_INTERVAL;
    uint16 desired_max_interval = DEFAULT_DESIRED_MAX_CONN_INTERVAL;
    uint16 desired_slave_latency = DEFAULT_DESIRED_SLAVE_LATENCY;
    uint16 desired_conn_timeout = DEFAULT_DESIRED_CONN_TIMEOUT;

    // Set the GAP Role Parameters
    GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &initial_advertising_enable );
    GAPRole_SetParameter( GAPROLE_ADVERT_OFF_TIME, sizeof( uint16 ), &gapRole_AdvertOffTime );
    
    GAPRole_SetParameter( GAPROLE_SCAN_RSP_DATA, sizeof ( scanRspData ), scanRspData );
    GAPRole_SetParameter( GAPROLE_ADVERT_DATA, sizeof( advertData ), advertData );
    
    GAPRole_SetParameter( GAPROLE_PARAM_UPDATE_ENABLE, sizeof( uint8 ), &enable_update_request );
    GAPRole_SetParameter( GAPROLE_MIN_CONN_INTERVAL, sizeof( uint16 ), &desired_min_interval );
    GAPRole_SetParameter( GAPROLE_MAX_CONN_INTERVAL, sizeof( uint16 ), &desired_max_interval );
    GAPRole_SetParameter( GAPROLE_SLAVE_LATENCY, sizeof( uint16 ), &desired_slave_latency );
    GAPRole_SetParameter( GAPROLE_TIMEOUT_MULTIPLIER, sizeof( uint16 ), &desired_conn_timeout );
  }
  
  // Set the GAP Characteristics
  GGS_SetParameter( GGS_DEVICE_NAME_ATT, sizeof ( attDeviceName ) , attDeviceName );

  // Initialize GATT attributes
  GGS_AddService( GATT_ALL_SERVICES );                  // GAP
  GATTServApp_AddService( GATT_ALL_SERVICES );          // GATT attributes
  HidKeyboard_AddService( GATT_ALL_SERVICES );          // HIDKeyboard Service

#if defined( CC2540_MINIDK )
 
  // Register for all key events - This app will handle all key events
  RegisterForKeys( emulatedKeyboard_TaskID );
  
  // makes sure LEDs are off
  HalLedSet( (HAL_LED_1 | HAL_LED_2), HAL_LED_MODE_OFF );
  
  // For keyfob board set GPIO pins into a power-optimized state
  // Note that there is still some leakage current from the buzzer,
  // accelerometer, LEDs, and buttons on the PCB.
  
  P0SEL = 0; // Configure Port 0 as GPIO
  P1SEL = 0; // Configure Port 1 as GPIO
  P2SEL = 0; // Configure Port 2 as GPIO

  P0DIR = 0xFC; // Port 0 pins P0.0 and P0.1 as input (buttons),
                // all others (P0.2-P0.7) as output
  P1DIR = 0xFF; // All port 1 pins (P1.0-P1.7) as output
  P2DIR = 0x1F; // All port 1 pins (P2.0-P2.4) as output
  
  P0 = 0x03; // All pins on port 0 to low except for P0.0 and P0.1 (buttons)
  P1 = 0;   // All pins on port 1 to low
  P2 = 0;   // All pins on port 2 to low  
  
#endif // #if defined( CC2540_MINIDK )
  
  // Setup a delayed profile startup
  osal_set_event( emulatedKeyboard_TaskID, EK_START_DEVICE_EVT );
}

/*********************************************************************
 * @fn      EmulatedKeyboard_ProcessEvent
 *
 * @brief   Simple BLE Peripheral Application Task event processor.  This function
 *          is called to process all events for the task.  Events
 *          include timers, messages and any other user defined events.
 *
 * @param   task_id  - The OSAL assigned task ID.
 * @param   events - events to process.  This is a bit map and can
 *                   contain more than one event.
 *
 * @return  events not processed
 */
uint16 EmulatedKeyboard_ProcessEvent( uint8 task_id, uint16 events )
{
  
  VOID task_id; // OSAL required parameter that isn't used in this function
  
  if ( events & SYS_EVENT_MSG )
  {
    uint8 *pMsg;

    if ( (pMsg = osal_msg_receive( emulatedKeyboard_TaskID )) != NULL )
    {
      emulatedKeyboard_ProcessOSALMsg( (osal_event_hdr_t *)pMsg );

      // Release the OSAL message
      VOID osal_msg_deallocate( pMsg );
    }

    // return unprocessed events
    return (events ^ SYS_EVENT_MSG);
  }

  if ( events & EK_START_DEVICE_EVT )
  {
    // Start the Device
    VOID GAPRole_StartDevice( &emulatedKeyboard_PeripheralCBs );

    // Set timer for first periodic event
    osal_start_timerEx( emulatedKeyboard_TaskID, EK_PERIODIC_EVT, PERIODIC_EVT_PERIOD );    
    
    return ( events ^ EK_START_DEVICE_EVT );
  }

  if ( events & EK_PERIODIC_EVT )
  {
    // Restart timer
    if ( PERIODIC_EVT_PERIOD )
      osal_start_timerEx( emulatedKeyboard_TaskID, EK_PERIODIC_EVT, PERIODIC_EVT_PERIOD );
  
    // Perform periodic application task
    performPeriodicTask();
    
    return ( events ^ EK_PERIODIC_EVT );
  }  
  
  // Discard unknown events
  return 0;
}

/*********************************************************************
 * @fn      emulatedKeyboard_ProcessOSALMsg
 *
 * @brief   Process an incoming task message.
 *
 * @param   pMsg - message to process
 *
 * @return  none
 */
static void emulatedKeyboard_ProcessOSALMsg( osal_event_hdr_t *pMsg )
{
  switch ( pMsg->event )
  {
    #if defined( CC2540_MINIDK )
    case KEY_CHANGE:
      emulatedKeyboard_HandleKeys( ((keyChange_t *)pMsg)->state, ((keyChange_t *)pMsg)->keys );
      break;
    #endif // #if defined( CC2540_MINIDK )
  }
}

#if defined( CC2540_MINIDK )
/*********************************************************************
 * @fn      emulatedKeyboard_HandleKeys
 *
 * @brief   Handles all key events for this device.
 *
 * @param   shift - true if in shift/alt.
 * @param   keys - bit field for key events. Valid entries:
 *                 HAL_KEY_SW_2
 *                 HAL_KEY_SW_1
 *
 * @return  none
 */
static void emulatedKeyboard_HandleKeys( uint8 shift, uint8 keys )
{
  static uint8 prevKey1 = 0;
  static uint8 prevKey2 = 0;
  
  (void)shift;  // Intentionally unreferenced parameter

  if ( (keys & HAL_KEY_SW_1) && (prevKey1 == 0) )
  {
    uint8 keyData[3];
    keyData[0] = 0x01;  // pressed
    keyData[1] = KEY_UP_ARROW;
    keyData[2] = 0x00;  // no modifiers
    HidKeyboard_SetParameter( HIDKEYBOARD_DATA, 3, keyData );
    prevKey1 = 1;
  }
  else if ( !(keys & HAL_KEY_SW_1) && (prevKey1 == 1) )
  {
    uint8 keyData[3];
    keyData[0] = 0x00;  // released
    keyData[1] = 0x00;  // 
    keyData[2] = 0x00;  // no modifiers
    HidKeyboard_SetParameter( HIDKEYBOARD_DATA, 3, keyData );
    prevKey1 = 0;
  }

  if ( (keys & HAL_KEY_SW_2) && (prevKey2 == 0) )
  {
    uint8 keyData[3];
    keyData[0] = 0x01;  // pressed
    keyData[1] = KEY_DOWN_ARROW;
    keyData[2] = 0x00;  // no modifiers
    HidKeyboard_SetParameter( HIDKEYBOARD_DATA, 3, keyData );
    prevKey2 = 1;
  }
  else if ( !(keys & HAL_KEY_SW_2) && (prevKey2 == 1) )
  {
    uint8 keyData[3];
    keyData[0] = 0x00;  // released
    keyData[1] = 0x00;  // 
    keyData[2] = 0x00;  // no modifiers
    HidKeyboard_SetParameter( HIDKEYBOARD_DATA, 3, keyData );
    prevKey2 = 0;
  }

}
#endif // #if defined( CC2540_MINIDK )

/*********************************************************************
 * @fn      peripheralStateNotificationCB
 *
 * @brief   Notification from the profile of a state change.
 *
 * @param   newState - new state
 *
 * @return  none
 */
static void peripheralStateNotificationCB( gaprole_States_t newState )
{
}

/*********************************************************************
 * @fn      performPeriodicTask
 *
 * @brief   Perform a periodic application task. This function gets
 *          called every five seconds as a result of the EK_PERIODIC_EVT
 *          OSAL event.
 *
 * @param   none
 *
 * @return  none
 */
static void performPeriodicTask( void )
{
}


/*********************************************************************
*********************************************************************/