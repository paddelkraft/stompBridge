# PlatformIO's ESP-IDF integration builds this directory as the project's main
# component.
if(STOMPBRIDGE_UI_SERIAL)
    idf_component_register(
        SRCS "next_usb_main.cpp"
             "usb_midi_transport.cpp"
             "platform/nimble_owner.cpp"
             "platform/ui_ble_frames.cpp"
             "platform/ui_ble_server.cpp"
             "ui/live_ingress.cpp"
             "ui/live_json.cpp"
             "core/connection.cpp"
             "core/read_model.cpp"
             "core/session_output.cpp"
             "core/application.cpp"
             "core/switching.cpp"
             "core/usb_midi_decoder.cpp"
             "devices/pocket_master/protocol.cpp"
             "devices/pocket_master/plugin.cpp"
             "devices/gp5/readback.cpp"
             "devices/gp5/plugin.cpp"
             "devices/registry.cpp"
             "gp5_protocol.cpp"
             "gp5_models.cpp"
        INCLUDE_DIRS "." "../include"
        PRIV_REQUIRES framework-arduinoespressif32 esp_driver_gpio usb nimble_arduino
    )
    target_compile_definitions(${COMPONENT_LIB} PRIVATE STOMPBRIDGE_BLE_UI=1 STOMPBRIDGE_UI_SERIAL=1)
elseif(STOMPBRIDGE_BLE_UI)
    idf_component_register(
        SRCS "next_usb_main.cpp"
             "usb_midi_transport.cpp"
             "platform/nimble_owner.cpp"
             "platform/ui_ble_frames.cpp"
             "platform/ui_ble_server.cpp"
             "ui/live_ingress.cpp"
             "ui/live_json.cpp"
             "core/connection.cpp"
             "core/read_model.cpp"
             "core/session_output.cpp"
             "core/application.cpp"
             "core/switching.cpp"
             "core/usb_midi_decoder.cpp"
             "devices/pocket_master/protocol.cpp"
             "devices/pocket_master/plugin.cpp"
             "devices/gp5/readback.cpp"
             "devices/gp5/plugin.cpp"
             "devices/registry.cpp"
             "gp5_protocol.cpp"
             "gp5_models.cpp"
        INCLUDE_DIRS "." "../include"
        PRIV_REQUIRES framework-arduinoespressif32 esp_driver_gpio usb nimble_arduino
    )
    target_compile_definitions(${COMPONENT_LIB} PRIVATE STOMPBRIDGE_BLE_UI=1)
elseif(STOMPBRIDGE_NEXT_USB)
    idf_component_register(
        SRCS "next_usb_main.cpp"
             "usb_midi_transport.cpp"
             "core/connection.cpp"
             "core/read_model.cpp"
             "core/session_output.cpp"
             "core/application.cpp"
             "core/switching.cpp"
             "core/usb_midi_decoder.cpp"
             "devices/pocket_master/protocol.cpp"
             "devices/pocket_master/plugin.cpp"
             "devices/gp5/readback.cpp"
             "devices/gp5/plugin.cpp"
             "devices/registry.cpp"
             "gp5_protocol.cpp"
             "gp5_models.cpp"
        INCLUDE_DIRS "." "../include"
        PRIV_REQUIRES framework-arduinoespressif32 esp_driver_gpio usb
    )
elseif(STOMPBRIDGE_NEXT_INPUT)
    idf_component_register(
        SRCS "next_input_main.cpp"
             "core/connection.cpp"
             "core/read_model.cpp"
             "core/session_output.cpp"
             "core/application.cpp"
             "core/switching.cpp"
             "core/usb_midi_decoder.cpp"
             "devices/pocket_master/protocol.cpp"
             "devices/pocket_master/plugin.cpp"
             "devices/gp5/readback.cpp"
             "devices/gp5/plugin.cpp"
             "devices/registry.cpp"
             "gp5_models.cpp"
        INCLUDE_DIRS "." "../include"
        PRIV_REQUIRES framework-arduinoespressif32 esp_driver_gpio
    )
elseif(USB_HUB_DIAGNOSTIC)
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
             "platform/nimble_owner.cpp"
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
