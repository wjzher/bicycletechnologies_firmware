/*
* Copyright (c) 2016 Shifty: Automatic Gear Selection System. All Rights Reserved.
 *
 * The code developed in this file is for the 4th year project: Shifty. The use,
 * copying, transfer or disclosure of such information is prohibited except by express written
 * agreement with the Shifty team.
 * Created by: Ali Ba Wazir
 * Nov 2016
 */

/**
 * @brief BLE Connection Manager application file.
 *
 */
 
/**********************************************************************************************
* INCLUDES
***********************************************************************************************/
#include <string.h>
#include "nrf_delay.h"
#include "app_error.h"
#include "fstorage.h"
#include "peer_manager.h"
#include "bsp.h" /*TODO: move all bsp code from main file to this file*/
#define NRF_LOG_MODULE_NAME "CONN_MANAGER APP"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"

#include "connection_manager_app.h"

/**********************************************************************************************
* MACRO DEFINITIONS
***********************************************************************************************/
#define ADVERTISED_DEVICES_COUNT_MAX   10          //max of advertised devices to store data of
#define MAX_CONNECTIONS_COUNT          2           //same as CENTRAL_LINK_COUNT

#define SCAN_INTERVAL                  0x00A0      /**< Determines scan interval in units of 0.625 millisecond. */
#define SCAN_WINDOW                    0x0050      /**< Determines scan window in units of 0.625 millisecond. */

#define MIN_CONNECTION_INTERVAL   MSEC_TO_UNITS(7.5, UNIT_1_25_MS)   /**< Determines minimum connection interval in millisecond. */
#define MAX_CONNECTION_INTERVAL   MSEC_TO_UNITS(30, UNIT_1_25_MS)    /**< Determines maximum connection interval in millisecond. */
#define SLAVE_LATENCY             0                                  /**< Determines slave latency in counts of connection events. */
#define SUPERVISION_TIMEOUT       MSEC_TO_UNITS(4000, UNIT_10_MS)    /**< Determines supervision time-out in units of 10 millisecond. */

#define SCANNING_WAITING_PERIOD_MS     10000       //during this period central will continue scanning for any advertising peripherals
#define BETWEEN_CONNECTIONS_DELAY_MS   5000        //sequent connections will be performed with a delay between them to allow handling connections

#define CONN_MANAGER_APP_STANDALONE_MODE      1           /*This boolean is set to true only if connection_manager_app
													*is in standalone mode. That means no interaction with SPI will be made
													**/

/**********************************************************************************************
* TYPE DEFINITIONS
***********************************************************************************************/

typedef struct{
	advertised_device_type_e   device_type;
	int8_t                     rssi;
	ble_gap_addr_t             peer_addr;
} advertised_devices_data_t;

typedef struct{
	uint8_t                    count;
	advertised_devices_data_t  advertised_devices_data [ADVERTISED_DEVICES_COUNT_MAX];
} advertised_devices_t;

typedef struct{
	uint16_t              conn_handle;
	ble_gap_conn_params_t conn_params;
} periph_set_conn_params_t;

typedef enum
{
    BLE_NO_SCAN,        /**< No advertising running. */
    BLE_WHITELIST_SCAN, /**< Advertising with whitelist. */
    BLE_FAST_SCAN,      /**< Fast advertising running. */
} ble_scan_mode_t;

/**********************************************************************************************
* STATIC VARIABLES
***********************************************************************************************/
static bool                      m_memory_access_in_progress      = false; /**< Flag to keep track of ongoing operations on persistent memory. */
static ble_gap_scan_params_t     m_scan_param;                             /**< Scan parameters requested for scanning and connection. */
static ble_scan_mode_t           m_scan_mode = BLE_FAST_SCAN;              /**< Scan mode used by application. */
static volatile bool             m_whitelist_temporarily_disabled = false; /**< True if whitelist has been temporarily disabled. */

/**
 * @brief Connection parameters requested for connection.
 */
static ble_gap_conn_params_t m_connection_param =
{
    (uint16_t)MIN_CONNECTION_INTERVAL, // Minimum connection.
    (uint16_t)MAX_CONNECTION_INTERVAL, // Maximum connection.
    (uint16_t)SLAVE_LATENCY,           // Slave latency.
    (uint16_t)SUPERVISION_TIMEOUT      // Supervision time-out.
};

static advertised_devices_t      advertised_devices;                                 /* Structure used to contain data for advertised sensors results after ble scan */
static advertised_devices_t*     advertised_devices_p       = &advertised_devices;   //for debugging
static periph_set_conn_params_t  periph_set_conn_params[MAX_CONNECTIONS_COUNT];

/**********************************************************************************************
* STATIC FUCNCTIONS
***********************************************************************************************/
static ble_gap_addr_t* connManagerApp_get_peer_addr(uint8_t advertised_device_index){
	
	ble_gap_addr_t* ble_gap_addr = NULL;
	
	if (advertised_device_index<ADVERTISED_DEVICES_COUNT_MAX){
		ble_gap_addr = &(advertised_devices.advertised_devices_data[advertised_device_index].peer_addr);
	}
	
	return ble_gap_addr;
}

static bool connManagerApp_is_peer_address_matching(const uint8_t* existing_peer_address, const uint8_t* new_peer_address){
	
	/*TODO: create a generic function to compare arrays given the length*/
	uint8_t i = 0;
	bool    is_matching = true;
	
	for (i=0; (i< BLE_GAP_ADDR_LEN) && (is_matching); i++){
		if (existing_peer_address[i] != new_peer_address[i]){
			is_matching = false;
		}
	}
		
	return is_matching;
}

static bool connManagerApp_connect_all(){
	
	uint8_t  i           = 0;
	bool     ret_code    = true;
	
	//connect to all advertised devices
	for (i=0; (i<(advertised_devices.count)) && (ret_code); i++){
		if (!connManagerApp_advertised_device_connect(i)){
			NRF_LOG_ERROR("connManagerApp_init: failed to connect to advertised device with index= %d\r\n",i);
			ret_code = false;
		} else{
			//delay to allow handling the connection event
			nrf_delay_ms(BETWEEN_CONNECTIONS_DELAY_MS);
		}
	}
	
	return ret_code;
}

/**********************************************************************************************
* PUBLIC FUCNCTIONS
***********************************************************************************************/

void connManagerApp_debug_print_conn_params(const ble_gap_conn_params_t* conn_params){
	NRF_LOG_DEBUG("connManagerApp_debug_print_conn_params: -----------------------------------------------------------\r\n");
	NRF_LOG_DEBUG("connManagerApp_debug_print_conn_params: min conn interval =0x%x\r\n", conn_params->min_conn_interval);
	NRF_LOG_DEBUG("connManagerApp_debug_print_conn_params: max conn interval =0x%x\r\n", conn_params->max_conn_interval);
	NRF_LOG_DEBUG("connManagerApp_debug_print_conn_params: slave latency =0x%x\r\n", conn_params->slave_latency);
	NRF_LOG_DEBUG("connManagerApp_debug_print_conn_params: conn supervision timeout =0x%x\r\n", conn_params->conn_sup_timeout);
	NRF_LOG_DEBUG("connManagerApp_debug_print_conn_params: -----------------------------------------------------------\r\n");
}

void connManagerApp_conn_params_update (const ble_gap_conn_params_t* conn_params){
	m_connection_param.min_conn_interval= conn_params->min_conn_interval;
	m_connection_param.max_conn_interval= conn_params->max_conn_interval;
	m_connection_param.slave_latency    = conn_params->slave_latency;
	m_connection_param.conn_sup_timeout = conn_params->conn_sup_timeout;
}

bool connManagerApp_get_memory_access_in_progress (void){
	return m_memory_access_in_progress;
}

void connManagerApp_set_memory_access_in_progress (bool is_in_progress){
	m_memory_access_in_progress = is_in_progress;
}
/**@brief Function to start scanning.
 */
bool connManagerApp_scan_start(void){
	
    uint32_t flash_busy;
	bool     ret_code = true;

    // If there is any pending write to flash, defer scanning until it completes.
    (void) fs_queued_op_count_get(&flash_busy);

    if (flash_busy != 0)
    {
        m_memory_access_in_progress = true;
        return false;
    }

    // Whitelist buffers.
    ble_gap_addr_t whitelist_addrs[8];
    ble_gap_irk_t  whitelist_irks[8];

    memset(whitelist_addrs, 0x00, sizeof(whitelist_addrs));
    memset(whitelist_irks,  0x00, sizeof(whitelist_irks));

    uint32_t addr_cnt = (sizeof(whitelist_addrs) / sizeof(ble_gap_addr_t));
    uint32_t irk_cnt  = (sizeof(whitelist_irks)  / sizeof(ble_gap_irk_t));

    #if (NRF_SD_BLE_API_VERSION == 2)

        ble_gap_addr_t * p_whitelist_addrs[8];
        ble_gap_irk_t  * p_whitelist_irks[8];

        for (uint32_t i = 0; i < 8; i++)
        {
            p_whitelist_addrs[i] = &whitelist_addrs[i];
            p_whitelist_irks[i]  = &whitelist_irks[i];
        }

        ble_gap_whitelist_t whitelist =
        {
            .pp_addrs = p_whitelist_addrs,
            .pp_irks  = p_whitelist_irks,
        };

    #endif

    ret_code_t ret;

    // Get the whitelist previously set using pm_whitelist_set().
    ret = pm_whitelist_get(whitelist_addrs, &addr_cnt,
                           whitelist_irks,  &irk_cnt);

    m_scan_param.active   = 0;
    m_scan_param.interval = SCAN_INTERVAL;
    m_scan_param.window   = SCAN_WINDOW;

    if (((addr_cnt == 0) && (irk_cnt == 0)) ||
        (m_scan_mode != BLE_WHITELIST_SCAN) ||
        (m_whitelist_temporarily_disabled))
    {
        // Don't use whitelist.

        m_scan_param.timeout  = 0x0000; // No timeout.

        #if (NRF_SD_BLE_API_VERSION == 2)
            m_scan_param.selective   = 0;
            m_scan_param.p_whitelist = NULL;
        #endif

        #if (NRF_SD_BLE_API_VERSION == 3)
            m_scan_param.use_whitelist  = 0;
            m_scan_param.adv_dir_report = 0;
        #endif
    }
    else
    {
        // Use whitelist.

        m_scan_param.timeout  = 0x001E; // 30 seconds.

        #if (NRF_SD_BLE_API_VERSION == 2)
            whitelist.addr_count     = addr_cnt;
            whitelist.irk_count      = irk_cnt;
            m_scan_param.selective   = 1;
            m_scan_param.p_whitelist = &whitelist;
        #endif

        #if (NRF_SD_BLE_API_VERSION == 3)
            m_scan_param.use_whitelist  = 1;
            m_scan_param.adv_dir_report = 0;
        #endif
    }

    NRF_LOG_DEBUG("connManagerApp_scan_start: starting scan.\r\n");

    ret = sd_ble_gap_scan_start(&m_scan_param);
	/*TODO: figure out why sd_ble_gap_scan_start returns NRF_ERROR_INVALID_STATE*/
    if(ret == NRF_ERROR_INVALID_STATE){
		    NRF_LOG_WARNING("sd_ble_gap_scan_start returned NRF_ERROR_INVALID_STATE but will skip it for now.\r\n");
	} else{
		
		APP_ERROR_CHECK(ret);

		ret = bsp_indication_set(BSP_INDICATE_SCANNING);
		APP_ERROR_CHECK(ret);
		
		//wait 10 seconds
		nrf_delay_ms(SCANNING_WAITING_PERIOD_MS);
		
		NRF_LOG_DEBUG("connManagerApp_scan_start: stopping scan.\r\n");
		// Stop scanning.
		ret = sd_ble_gap_scan_stop();

		if (ret != NRF_SUCCESS){
			NRF_LOG_ERROR("connManagerApp_scan_start: scan stop failed, reason %d\r\n", ret);
		}
    
		/*TODO: figure out whether to cintinue or not if err_code != NRF_SUCCESS*/
		ret = bsp_indication_set(BSP_INDICATE_IDLE);
		APP_ERROR_CHECK(ret);
		
	}
	
	if(ret != NRF_SUCCESS){
		ret_code = false;
	}
	
	if (CONN_MANAGER_APP_STANDALONE_MODE){
		/*if in simulation mode, connect to all advertized devices once scannig is finished*/
		ret_code= connManagerApp_connect_all();
	}
	
	return ret_code;
}

/**@brief Function for disabling the use of whitelist for scanning.
 */
void connManagerApp_whitelist_disable(void){
    
	uint32_t err_code;

    if ((m_scan_mode == BLE_WHITELIST_SCAN) && !m_whitelist_temporarily_disabled)
    {
        m_whitelist_temporarily_disabled = true;

        err_code = sd_ble_gap_scan_stop();
        if (err_code == NRF_SUCCESS)
        {
            connManagerApp_scan_start();
        }
        else if (err_code != NRF_ERROR_INVALID_STATE)
        {
            APP_ERROR_CHECK(err_code);
        }
    }
    m_whitelist_temporarily_disabled = true;
}


bool connManagerApp_advertised_device_connect(uint8_t advertised_device_id){
	
	bool               ret_code                   = true;
	uint32_t           err_code                   = NRF_SUCCESS;
	ble_gap_addr_t    *peer_addr                  = NULL;
	uint8_t            advertised_device_index    = advertised_device_id;  /* for now the id is the same index inside array
														                    * advertised_devices.advertised_devices_data[]
														                    **/

	// Stop scanning.
    err_code = sd_ble_gap_scan_stop();

    if (err_code != NRF_SUCCESS){
		NRF_LOG_ERROR("connManagerApp_advertised_device_connect: scan stop failed, reason %d\r\n", err_code);
    }
    
	/*TODO: figure out whether to cintinue or not if err_code != NRF_SUCCESS*/
	err_code = bsp_indication_set(BSP_INDICATE_IDLE);
    APP_ERROR_CHECK(err_code);

    // Initiate connection.
    #if (NRF_SD_BLE_API_VERSION == 2)
	m_scan_param.selective = 0;
    #endif
	
	peer_addr = connManagerApp_get_peer_addr(advertised_device_index);
	if (peer_addr == NULL){
		NRF_LOG_ERROR("connManagerApp_advertised_device_connect: connManagerApp_get_peer_addr for device index= %d returned NULL\r\n", advertised_device_index);
		ret_code = false;
	} else{
		NRF_LOG_DEBUG("connManagerApp_advertised_device_connect: debugging conn params set by central before connecting\r\n");
		connManagerApp_debug_print_conn_params(&m_connection_param);
		err_code = sd_ble_gap_connect(peer_addr,
                                      &m_scan_param,
                                      &m_connection_param);

		m_whitelist_temporarily_disabled = false;

		if (err_code != NRF_SUCCESS){
			NRF_LOG_ERROR("Connection Request Failed, reason %d\r\n", err_code);
		}
		
	}	
		
	return ret_code;
}

bool connManagerApp_advertised_device_store(advertised_device_type_e device_type, const ble_gap_evt_adv_report_t* adv_report){
	
	bool     ret_code          = true;
	uint8_t  i                 = 0;
	uint8_t  last_count        = advertised_devices.count;
	bool     device_stored     = false;
	
	for (i=0; (i<last_count) && (!device_stored); i++){
		if (advertised_devices.advertised_devices_data[i].device_type == device_type
			&& connManagerApp_is_peer_address_matching(advertised_devices.advertised_devices_data[i].peer_addr.addr, adv_report->peer_addr.addr)
		   ){
			
			// defice info is already stored
			device_stored = true;
		}
	}
	
	if (device_stored){
		NRF_LOG_DEBUG("connManagerApp_store_advertised_device: device type=%d is already stored\r\n", device_type);
	} else if(last_count >=ADVERTISED_DEVICES_COUNT_MAX){
		
		NRF_LOG_DEBUG("connManagerApp_store_advertised_device: stored advertised devices reached max. last_count=%d \r\n", last_count);
	
	}
	else{
		//this is a new advertised device. Store its connection info  and increase count
		advertised_devices.advertised_devices_data[last_count].device_type = device_type;
		advertised_devices.advertised_devices_data[last_count].peer_addr = adv_report->peer_addr;
		advertised_devices.advertised_devices_data[last_count].rssi = adv_report->rssi;
		
		advertised_devices.count ++;
	}
	
	return ret_code;
}	


/**
 * @brief Connection Manager App initialization.
 */
bool connManagerApp_init(void){
	
	bool     ret_code          = true;
	
	memset(&advertised_devices, 0, sizeof(advertised_devices_t));
	
	if (CONN_MANAGER_APP_STANDALONE_MODE){
		/*if in simulation mode, scan for all devices at initialization and store results of up to 
		 *ADVERTISED_DEVICES_COUNT_MAX
		 **/
		ret_code = connManagerApp_scan_start();
	}
		
	return ret_code;	
}
