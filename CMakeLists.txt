cmake_minimum_required(VERSION 3.9)
project(canokey-f407 C ASM)
set(CMAKE_C_STANDARD 11)

# ---------------------------------------------------------------------------
# 板级选项：改板子时主要改这里
# ---------------------------------------------------------------------------
option(ENABLE_DEBUG_OUTPUT "Print debug messages over USART1" ON)
option(BTN_TEST "LED mirrors PA15 (button-level diagnostic, bypasses touch FSM)" OFF)
option(DIAG "LED blink-code diagnostic: OUT/WAIT/touch milestones (no debugger needed)" OFF)
set(HSE_VALUE "8000000" CACHE STRING "External crystal frequency in Hz (8000000 or 25000000)")
set(CANOKEY_CORE_DIR "${CMAKE_SOURCE_DIR}/canokey-core" CACHE PATH "Path to canokey-core")

# ---------------------------------------------------------------------------
# MCU / linker
# ---------------------------------------------------------------------------
set(STARTUP_ASM_FILE "startup_stm32f407xx.s")
set(CMAKE_SYSTEM_PROCESSOR "cortex-m4")
set(MCU_LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/STM32F407VETx_FLASH.ld")

set(CMAKE_C_FLAGS "-mcpu=${CMAKE_SYSTEM_PROCESSOR} -mthumb -Wall -fno-strict-aliasing -fdata-sections -ffunction-sections ${CMAKE_C_FLAGS}")
set(CMAKE_ASM_FLAGS "-mcpu=${CMAKE_SYSTEM_PROCESSOR} -mthumb -Wall -fdata-sections -ffunction-sections ${CMAKE_ASM_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "-specs=nano.specs -T${MCU_LINKER_SCRIPT} -Wl,-Map=layout.map,--cref -Wl,--gc-sections ${CMAKE_EXE_LINKER_FLAGS}")
set(CMAKE_C_FLAGS_RELEASE "-Os -g3")

# ---------------------------------------------------------------------------
# Crypto / core
# ---------------------------------------------------------------------------
set(TINYUSB_MCU "OPT_MCU_STM32F4" CACHE STRING "TinyUSB MCU name")
add_definitions(-DMBEDTLS_CONFIG_FILE="${CMAKE_SOURCE_DIR}/Inc/mbedtls-config.h")
add_definitions(-DDEVICE_CONFIG_FILE="${CMAKE_SOURCE_DIR}/Inc/device-config.h")

# --- tinycbor 需要两个由构建系统生成的头，这里手工生成到 build 目录 -------
set(TINYCBOR_GEN_DIR "${CMAKE_BINARY_DIR}/tinycbor-gen")
set(TINYCBOR_VERSION_MAJOR 7)
set(TINYCBOR_VERSION_MINOR 0)
configure_file(${CANOKEY_CORE_DIR}/tinycbor/src/tinycbor-export.h.in
        ${TINYCBOR_GEN_DIR}/tinycbor-export.h COPYONLY)
configure_file(${CANOKEY_CORE_DIR}/tinycbor/src/tinycbor-version.h.in
        ${TINYCBOR_GEN_DIR}/tinycbor-version.h @ONLY)

add_subdirectory(${CANOKEY_CORE_DIR} ${CMAKE_BINARY_DIR}/canokey-core)

# canokey-core 里编译 tinycbor / tinyusb dcd，需要拿到生成的头和 CMSIS 设备头
target_include_directories(canokey-core SYSTEM PUBLIC
        ${TINYCBOR_GEN_DIR}
        ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32F4xx/Include
        ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Include
        ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Inc
        ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy)
target_compile_definitions(canokey-core PUBLIC
        STM32F407xx
        HSE_VALUE=${HSE_VALUE})

add_definitions(-DCFG_TUSB_MCU=${TINYUSB_MCU})
# STM32F4 的 OTG FS/HS 都是 Synopsys DWC2 核心，不是 L4 的 fsdev
set(TINYUSB_DCD_SRC ${CANOKEY_CORE_DIR}/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c)
if (NOT EXISTS ${TINYUSB_DCD_SRC})
  set(TINYUSB_DCD_SRC ${CANOKEY_CORE_DIR}/tinyusb/src/portable/st/synopsys/dcd_synopsys.c)
endif ()

aux_source_directory(Src SOURCE_CODE_COMMON)
file(GLOB SOURCE_CODE_BOARD Drivers/STM32F4xx_HAL_Driver/Src/*.c)
list(FILTER SOURCE_CODE_BOARD EXCLUDE REGEX "_template\\.c$") # 排除 Cube 模板文件
add_executable(canokey-f407 ${SOURCE_CODE_COMMON} ${SOURCE_CODE_BOARD} ${TINYUSB_DCD_SRC} ${STARTUP_ASM_FILE})
target_link_libraries(canokey-f407 PRIVATE canokey-core c m nosys)

target_compile_definitions(canokey-f407 PRIVATE
    USE_HAL_DRIVER
    STM32F407xx
    HSE_VALUE=${HSE_VALUE})
if (ENABLE_DEBUG_OUTPUT)
    target_compile_definitions(canokey-f407 PRIVATE DEBUG_OUTPUT)
endif (ENABLE_DEBUG_OUTPUT)
if (BTN_TEST)
    target_compile_definitions(canokey-f407 PRIVATE BTN_TEST)
endif (BTN_TEST)
if (DIAG)
    target_compile_definitions(canokey-f407 PRIVATE DIAG)
endif (DIAG)

target_include_directories(canokey-f407 PRIVATE Drivers/STM32F4xx_HAL_Driver/Inc)
target_include_directories(canokey-f407 PRIVATE Drivers/STM32F4xx_HAL_Driver/Inc/Legacy)
target_include_directories(canokey-f407 PRIVATE Drivers/CMSIS/Device/ST/STM32F4xx/Include)
target_include_directories(canokey-f407 PRIVATE Drivers/CMSIS/Include)
target_include_directories(canokey-f407 PRIVATE Inc)

find_package(Git QUIET)
set(GIT_REV "unknown")
if (GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --tags --long --abbrev=8 --dirty
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          OUTPUT_VARIABLE GIT_REV
          OUTPUT_STRIP_TRAILING_WHITESPACE
          ERROR_QUIET)
  if (NOT GIT_REV)
    set(GIT_REV "unknown")
  endif ()
endif ()
message(STATUS "Firmware revision: ${GIT_REV}")
configure_file(Inc/git-rev.h.in ${CMAKE_BINARY_DIR}/git-rev.h)
target_include_directories(canokey-f407 PRIVATE ${CMAKE_BINARY_DIR})

add_custom_command(TARGET canokey-f407 POST_BUILD
    COMMAND "${CROSS_COMPILE}size" canokey-f407)

# 单文件烧录镜像：boot stub(16K) + 文件系统区(48K, 填 0xFF) + 应用(448K)
add_custom_target(canokey-f407.bin
    DEPENDS canokey-f407
    COMMAND "${CROSS_COMPILE}objcopy" -O binary -S canokey-f407 canokey-f407-raw.bin
    COMMAND ${CMAKE_COMMAND} -E env python ${CMAKE_SOURCE_DIR}/mkimage.py
            canokey-f407-raw.bin canokey-f407.bin
    )

add_custom_target(flash
    DEPENDS canokey-f407.bin
    COMMAND st-flash --reset write canokey-f407.bin 0x08000000)

add_custom_target(cubeflash
    DEPENDS canokey-f407.bin
    COMMAND STM32_Programmer_CLI -c port=SWD -w canokey-f407.bin 0x08000000 -rst)
