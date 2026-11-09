cmake_minimum_required(VERSION 3.20)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/arm-gcc.cmake)
project(m01fw C)

set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

add_executable(m01fw.elf
  src/startup.c
  src/system.c
  src/input.c
  src/lcd.c
  src/main.c
  src/radio.c
  src/mdp.c
  src/config.c
)
target_include_directories(m01fw.elf PRIVATE src lib lib/cmsis)
target_compile_definitions(m01fw.elf PRIVATE STM32F103xE)
set(MCU_FLAGS -mcpu=cortex-m3 -mthumb)
target_compile_options(m01fw.elf PRIVATE ${MCU_FLAGS} -Wall -Wextra -Wno-unused-parameter
  -ffunction-sections -fdata-sections -g3)
target_link_options(m01fw.elf PRIVATE ${MCU_FLAGS} -nostartfiles
  -T${CMAKE_CURRENT_SOURCE_DIR}/ld/m01.ld -Wl,--gc-sections -Wl,-Map=m01fw.map
  --specs=nano.specs --specs=nosys.specs)

set_target_properties(m01fw.elf PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ld/m01.ld)

add_custom_command(TARGET m01fw.elf POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex m01fw.elf m01fw.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary m01fw.elf m01fw.bin
  COMMAND ${CMAKE_SIZE} m01fw.elf)
