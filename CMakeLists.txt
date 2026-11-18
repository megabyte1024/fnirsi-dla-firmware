# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.25)
project(fnirsi_dla_firmware C ASM)
include(cmake/BitoscopeBuildJobs.cmake)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(target_flags -march=rv32imac_zicsr -mabi=ilp32 -mcmodel=medlow)

function(add_firmware_image name linker_script)
  add_executable(${name} ${ARGN})
  target_compile_options(${name} PRIVATE ${target_flags} -Os -ffunction-sections -fdata-sections
                         -ffreestanding -Wall -Wextra -Wpedantic -Werror)
  target_link_options(${name} PRIVATE ${target_flags} -nostartfiles -nostdlib -T ${linker_script}
                      -Wl,--gc-sections -Wl,-Map=${CMAKE_BINARY_DIR}/${name}.map -Wl,--no-warn-rwx-segments)
  target_link_libraries(${name} PRIVATE gcc)
  set_target_properties(${name} PROPERTIES SUFFIX ".elf" LINK_DEPENDS ${linker_script})
  add_custom_command(TARGET ${name} POST_BUILD
                     COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}> ${CMAKE_BINARY_DIR}/${name}.bin
                     COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${name}>
                     VERBATIM)
endfunction()

add_firmware_image(dla16_boot ${CMAKE_SOURCE_DIR}/link/dla16_boot.ld
                   src/start.S src/boot.c src/boot_protocol.c src/usb2.c src/system.c src/flash.c src/settings.c
                   src/spi_flash.c)
target_compile_options(dla16_boot PRIVATE -flto -msave-restore)
target_link_options(dla16_boot PRIVATE -flto -msave-restore)
set(wch_lib_source ${CMAKE_SOURCE_DIR}/third_party/wch-ch56x-lib/src)
set(wch_bsp ${CMAKE_SOURCE_DIR}/third_party/wch-ch56x-bsp)
if(NOT EXISTS ${wch_lib_source}/wch-ch56x-lib/usb/usb30.c OR NOT EXISTS ${wch_bsp}/drv/CH56xSFR.h)
  message(FATAL_ERROR "third_party submodules are missing: run 'git submodule update --init'")
endif()
set(wch_lib ${CMAKE_BINARY_DIR}/third_party/wch-ch56x-lib/src)
file(REMOVE_RECURSE ${wch_lib})
file(COPY ${wch_lib_source}/wch-ch56x-lib DESTINATION ${wch_lib})
execute_process(COMMAND ${CMAKE_COMMAND} -E env GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}
                        git apply -p1 ${CMAKE_SOURCE_DIR}/third_party/patches/wch-ch56x-lib-usb30-hooks.patch
                WORKING_DIRECTORY ${wch_lib}/wch-ch56x-lib RESULT_VARIABLE wch_patch_result)
file(READ ${wch_lib}/wch-ch56x-lib/usb/usb30.c wch_patched_usb30)
string(FIND "${wch_patched_usb30}" "usb30_take_ep_in" wch_hook_found)
if(NOT wch_patch_result EQUAL 0 OR wch_hook_found EQUAL -1)
  message(FATAL_ERROR "third_party/patches/wch-ch56x-lib-usb30-hooks.patch does not apply to the wch-ch56x-lib submodule")
endif()
add_library(wch_usb STATIC
            ${wch_lib}/wch-ch56x-lib/usb/usb30.c ${wch_lib}/wch-ch56x-lib/usb/usb20.c
            ${wch_lib}/wch-ch56x-lib/usb/usb_device.c ${wch_lib}/wch-ch56x-lib/usb/usb_endpoints.c
            ${wch_lib}/wch-ch56x-lib/utils/critical_section.c)
target_compile_options(wch_usb PRIVATE ${target_flags} -O2 -ffunction-sections -fdata-sections -ffreestanding
                       -std=gnu99 -fsigned-char -msmall-data-limit=8)
set_target_properties(wch_usb PROPERTIES C_EXTENSIONS ON)
set_source_files_properties(src/usb3.c PROPERTIES COMPILE_OPTIONS -std=gnu2x)
target_include_directories(wch_usb PUBLIC ${CMAKE_SOURCE_DIR}/src/port)
target_include_directories(wch_usb SYSTEM PUBLIC ${wch_lib} ${wch_bsp}/drv ${wch_bsp}/rvmsis)

add_firmware_image(dla16_firmware ${CMAKE_SOURCE_DIR}/link/dla16_app.ld
                   src/start.S src/vectors.S src/interrupts.c src/app.c src/app_protocol.c src/usb3.c src/usb3_descriptors.c src/system.c src/uart.c
                   src/flash.c src/settings.c src/fpga_link.c src/delay.c src/fpga_steps.c src/usb2.c src/blackbox.c src/data_path.c src/data_link.c src/stall_watch.c src/sample_offer.c src/model.c)
target_link_libraries(dla16_firmware PRIVATE wch_usb c)
target_compile_options(dla16_firmware PRIVATE -O2)

set(installer_image_source ${CMAKE_BINARY_DIR}/dla16_installer_image.c)
add_custom_command(OUTPUT ${installer_image_source}
                   COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_BINARY_DIR}/dla16_boot.bin
                           -DOUTPUT=${installer_image_source} -P ${CMAKE_SOURCE_DIR}/cmake/embed_bootloader.cmake
                   DEPENDS dla16_boot ${CMAKE_SOURCE_DIR}/cmake/embed_bootloader.cmake
                   VERBATIM)
add_firmware_image(dla16_installer ${CMAKE_SOURCE_DIR}/link/dla16_app.ld
                   src/start.S src/installer_main.c src/installer.c src/system.c src/flash.c src/settings.c
                   src/fpga_link.c src/delay.c ${installer_image_source})
target_include_directories(dla16_installer PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_custom_command(TARGET dla16_installer POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -DIMAGE=${CMAKE_BINARY_DIR}/dla16_installer.bin
                           -P ${CMAKE_SOURCE_DIR}/cmake/check_installer_mark.cmake
                   VERBATIM)

if(EXISTS ${CMAKE_SOURCE_DIR}/private/CMakeLists.txt)
  add_subdirectory(private)
endif()
