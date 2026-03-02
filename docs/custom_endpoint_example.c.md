/********************* Custom Endpoint Usage Example *********************
 * File Name          : custom_endpoint_example.c
 * Description        : Example of how to use the custom endpoint in app.c
 *
 * Integration Instructions:
 * 1. Add #include "custom_endpoint.h" to your app.c file
 * 2. Call custom_endpoint_init() in your app_init() function
 * 3. Call custom_endpoint_process() in your main loop (app_run())
 * 4. Use custom_endpoint_send() to send data to PC
 *************************************************************************/

/* 
 * Add this to your app.c includes section:
 * #include "custom_endpoint.h"
 */

/*
 * Add this to your app_init() function:
 */
void app_init_example(void)
{
    // ... your existing initialization code ...
    
    // Initialize custom endpoint
    custom_endpoint_init();
    
    PRINT("App with Custom Endpoint Init OK!\r\n");
}

/*
 * Add this to your app_run() function:
 */
void app_run_example(void)
{
    // ... your existing app logic ...
    
    // Process custom endpoint data 
    custom_endpoint_process();
    
    // Example: Send periodic status data
    static uint32_t last_send_time = 0;
    if (system_tick - last_send_time > 5000) { // Every 5 seconds
        uint8_t status_data[16];
        status_data[0] = 0xAA; // Status header
        status_data[1] = key_pressed_count; // Number of keys pressed
        status_data[2] = (uint8_t)(system_tick & 0xFF); // Low byte of system tick
        status_data[3] = 0x55; // Footer
        
        custom_endpoint_send(status_data, 4);
        last_send_time = system_tick;
    }
}

/*
 * Optional: Implement this callback to handle received data
 */
void custom_data_received_callback(custom_packet_t *packet)
{
    // This function will be called when data is received from PC
    PRINT("App: Custom data received, cmd=0x%02X, len=%d\r\n", 
          packet->cmd, packet->len);
    
    // Handle your custom commands here
    if (packet->cmd == 0x10) { // Custom app command
        // Process your specific command
    }
}

/*
 * Example usage functions you can call from anywhere:
 */

// Send keyboard state to PC
void send_keyboard_state_to_pc(void)
{
    uint8_t kb_state[8];
    // Fill with your keyboard state data
    kb_state[0] = 0x01; // State ID
    // ... fill other bytes ...
    
    if (custom_endpoint_send(kb_state, sizeof(kb_state)) != 0) {
        PRINT("Failed to send keyboard state\r\n");
    }
}

// Send debug information to PC
void send_debug_info_to_pc(uint16_t error_code, uint8_t module_id)
{
    uint8_t debug_data[6];
    debug_data[0] = 0xDB; // Debug header
    debug_data[1] = module_id;
    debug_data[2] = (error_code >> 8) & 0xFF; // High byte
    debug_data[3] = error_code & 0xFF;        // Low byte
    debug_data[4] = 0; // Reserved
    debug_data[5] = 0xED; // Debug footer
    
    custom_endpoint_send(debug_data, sizeof(debug_data));
}

// Example PC application communication protocol:
/*
 * PC -> Device Commands:
 * 0x01 - Get Version
 * 0x02 - Set Config  
 * 0x03 - Get Status
 * 0x10 - App specific command
 * 
 * Device -> PC Data:
 * 0xAA - Status data
 * 0xDB - Debug data
 * 0xKB - Keyboard state data
 */