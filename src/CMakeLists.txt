# PlatformIO's ESP-IDF integration builds this directory as the project's main
# component.
if(USB_HUB_DIAGNOSTIC)
    idf_component_register(
        SRCS "usb_hub_diagnostic/main.c"
             "usb_hub_diagnostic/gp5_protocol_bridge.cpp"
             "gp5_protocol.cpp"
        INCLUDE_DIRS "." "../include"
        PRIV_REQUIRES esp_driver_gpio
    )
else()
    idf_component_register(
        SRCS "main.cpp"
             "ble_midi_transport.cpp"
             "gp5_models.cpp"
             "gp5_protocol.cpp"
             "gp5_plugin.cpp"
             "pocket_master_plugin.cpp"
             "pedal_plugins.cpp"
             "usb_midi_transport.cpp"
             "web_ui.cpp"
        INCLUDE_DIRS "." "../include"
        PRIV_REQUIRES framework-arduinoespressif32 esp_driver_gpio esp_netif esp_wifi nimble_arduino usb
    )
endif()
