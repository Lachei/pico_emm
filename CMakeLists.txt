# ----------------------------------------------------------------------------
# General settings/includes
# ----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)
if (NOT FREERTOS_KERNEL_PATH AND NOT DEFINED ENV{FREERTOS_KERNEL_PATH})
        message(FATAL_ERROR "Missing FreeRTOS kernel source path. Set by declaring environment variable FREERTOS_KERNEL_PATH or add -DFREERTOS_KERNEL_PATH=<path> to the cmake call")
endif()
if (NOT FREERTOS_KERNEL_PATH AND DEFINED ENV{FREERTOS_KERNEL_PATH})
        set(FREERTOS_KERNEL_PATH ENV{FREERTOS_KERNEL_PATH})
endif()
if (NOT LIBMODBUS_STATIC_PATH AND NOT DEFINED ENV{LIBMODBUS_STATIC_PATH})
        message(FATAL_ERROR "Missing LIBMODBUS_STATIC_PATH source path. Set by declaring environment variable LIBMODBUS_STATIC_PATH or add -DLIBMODBUS_STATIC_PATH=<path> to the cmake call. Repo link: https://github.com/Lachei/libmodbus-static.git")
endif()
if (NOT LIBMODBUS_STATIC_PATH AND DEFINED ENV{LIBMODBUS_STATIC_PATH})
        set(LIBMODBUS_STATIC_PATH ENV{LIBMODBUS_STATIC_PATH})
endif()
if (NOT LIBPIMORONI_PICO_PATH AND NOT DEFINED ENV{LIBPIMORONI_PICO_PATH})
        message(FATAL_ERROR "Missing LIBPIMORONI_PICO_PATH source path. Set by declaring environment variable LIBPIMORONI_PICO_PATH or add -DLIBPIMORONI_PICO_PATH=<path> to the cmake call. Repo link: https://github.com/pimoroni/pimoroni-pico.git")
endif()
if (NOT LIBPIMORONI_PICO_PATH AND DEFINED ENV{LIBPIMORONI_PICO_PATH})
        set(LIBPIMORONI_PICO_PATH ENV{LIBPIMORONI_PICO_PATH})
endif()
if (NOT PRESTO_PATH AND NOT DEFINED ENV{PRESTO_PATH})
        message(FATAL_ERROR "Missing PRESTO_PATH source path. Set by declaring environment variable PRESTO_PATH or add -DPRESTO_PATH=<path> to the cmake call. Repo link: https://github.com/pimoroni/presto.git")
endif()
if (NOT PRESTO_PATH AND DEFINED ENV{PRESTO_PATH})
        set(PRESTO_PATH ENV{PRESTO_PATH})
endif()

if (NOT PICO_BOARD)
        message("No board set, defaulting to pimoroni_pico_plus2_w_rp2350")
        set(PICO_BOARD pimoroni_pico_plus2_w_rp2350)
endif()

include(pico_sdk_import.cmake)
include(pico_extras_import_optional.cmake)
include(FreeRTOS_Kernel_import.cmake)
include(cmake/html_pages_library.cmake)
add_subdirectory(${LIBMODBUS_STATIC_PATH} libmodbus-static)
add_subdirectory(${LIBPIMORONI_PICO_PATH} libpimoroni-pico)
add_subdirectory(${PRESTO_PATH}/drivers/st7701 presto-driver)

add_compile_options(-Wall)

project(pico-emm C CXX)

pico_sdk_init()

set(CMAKE_BUILD_TYPE "MinSizeRel")

if (NOT PICO_CYW43_SUPPORTED)
        message(FATAL_ERROR "Pico w not supported by your sdk. Make sure to have PICO_CYW43_SUPPORTED set by your sdk installation")
endif()


# ----------------------------------------------------------------------------
# Website content
# ----------------------------------------------------------------------------
add_html_pages_library(NAME pico-emm-html SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/http_content/404.html
        ${CMAKE_CURRENT_SOURCE_DIR}/http_content/index.html
        ${CMAKE_CURRENT_SOURCE_DIR}/http_content/style.css
        ${CMAKE_CURRENT_SOURCE_DIR}/http_content/internet.html
        ${CMAKE_CURRENT_SOURCE_DIR}/http_content/overview.html
        ${CMAKE_CURRENT_SOURCE_DIR}/http_content/settings.html
)


# ----------------------------------------------------------------------------
# Executable
# ----------------------------------------------------------------------------
add_executable(pico-emm
        src/main.cpp
        src/dhcpserver.c
        src/dnsserver.c
        src/log_storage.cpp
        src/ntp_client.cpp
        src/font_file.cpp
        src/draw.cpp
        src/FT6236.cpp
)
set_property(TARGET pico-emm PROPERTY CXX_STANDARD 23)
target_include_directories(pico-emm PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${LIBPIMORONI_PICO_PATH}
        ${PRESTO_PATH}
)
target_link_libraries(pico-emm
        pico_stdlib
        pico_mbedtls
        hardware_flash
        hardware_i2c
        pico_cyw43_arch_lwip_sys_freertos
        pico_lwip_mdns
        FreeRTOS-Kernel-Heap4 # FreeRTOS kernel and dynamic heap
        pico-emm-html
        st7701_presto
        pico_graphics
        pico_vector
)
pico_add_extra_outputs(pico-emm)
pico_enable_stdio_usb(pico-emm 1)
pico_enable_stdio_uart(pico-emm 0)

