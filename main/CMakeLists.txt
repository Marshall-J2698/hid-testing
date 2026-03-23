idf_component_register(SRCS "hid_host_example.c" "eth_req.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES usb esp_driver_gpio esp_eth esp_http_client esp_netif nvs_flash esp_event
                       )
