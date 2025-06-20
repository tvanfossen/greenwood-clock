# main/CMakeLists.txt

idf_component_register(
    SRCS    main.cpp
    INCLUDE_DIRS .
    REQUIRES
        esp32_p4_function_ev_board   
        esp_lvgl_port                
        esp_hosted                   
        esp_wifi_remote    
        esp_lcd_ek79007          
        nvs_flash                    
        esp_netif                    
        esp_event                    
        esp_wifi                    
        lwip                         
        lvgl                                    
        spiffs
        images
        fonts
)

# Keep your SPIFFS partition for assets
spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
