# PlatformIO's ESP-IDF integration builds this directory as the project's main
# component. The normal Arduino environment ignores this file.
idf_component_register(
    SRCS "usb_hub_diagnostic/main.c"
         "usb_hub_diagnostic/gp5_protocol_bridge.cpp"
         "gp5_protocol.cpp"
    INCLUDE_DIRS "." "../include"
    PRIV_REQUIRES esp_driver_gpio
)
