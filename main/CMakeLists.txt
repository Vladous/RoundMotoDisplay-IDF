idf_component_register(
    SRCS "main.cpp" "connect.cpp" "display.cpp" "sensors.cpp" "uloziste.cpp" "vstupy.cpp"
    INCLUDE_DIRS "."
    REQUIRES bt esp_event esp_netif nvs_flash esp_adc esp_driver_gpio esp_driver_i2c
)
