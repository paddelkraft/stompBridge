# PlatformIO installs NimBLE-Arduino as a library, but the mixed Arduino/IDF
# build compiles project sources as IDF components. Register the same library
# here so its headers and implementation are available to those components.
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(NIMBLE_ARDUINO_SRC
    "${PROJECT_ROOT}/.pio/libdeps/esp32-s3-n16r8/NimBLE-Arduino/src")
if(NOT EXISTS "${NIMBLE_ARDUINO_SRC}/NimBLEDevice.h")
    message(FATAL_ERROR "NimBLE-Arduino was not installed by PlatformIO")
endif()

file(GLOB NIMBLE_ARDUINO_SOURCES
    "${NIMBLE_ARDUINO_SRC}/*.cpp")
idf_component_register(
    SRCS ${NIMBLE_ARDUINO_SOURCES}
    INCLUDE_DIRS
        "${NIMBLE_ARDUINO_SRC}"
        "$ENV{IDF_PATH}/components/nvs_flash/include"
    REQUIRES bt framework-arduinoespressif32 nvs_flash
)
