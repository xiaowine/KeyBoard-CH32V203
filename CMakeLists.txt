############################################################[Chinese Manual]###################################################################
# *****工程路径不要包含中文字符和空格,否则可能会报错！*****
# 第一步： 确认机器上已经安装了MRS的工具链(假设当前平台为Linux,工具链路径为"/path/to/toolchain/RISC-V Embedded GCC12",工具链相关前缀为"riscv-wch-elf-")
# 第二步: 设置工具链路径,例如修改替换下方的set(TOOLCHAIN_FOLDER "/path/to/toolchain/RISC-V Embedded GCC12")
# 第三步: 设置工具链中相关可执行文件的前缀,例如修改替换下方的set(TOOLCHAIN_PREFIX "riscv-wch-elf-")
# 第四步: 执行 cd your/path/of/project
# 第五步: 执行 cmake -B build -G "Unix Makefiles"
# 第六步: 执行 cmake --build build
# 注意: 在Windows平台运行,需要a.安装GNU MAKE并将其路径(bin目录)添加到系统环境变量,之后执行cmake -B build -G "Unix Makefiles"(推荐)
#       或者b.安装MinGW,将其路径(bin目录)添加到系统环境变量,之后执行cmake -B build -G "MinGW Makefiles"
# 如有相关疑问,可通过support@mounriver.com联系我们
#################################################################################################################################################

# 设置 CMake 最低版本要求
cmake_minimum_required(VERSION 3.16)

# 设置 CMake 系统名称（用于交叉编译等场景）
set(CMAKE_SYSTEM_NAME Generic)

# ---------- 公共配置（放在文件顶部，便于复用与查阅） ----------
# 项目与产物名
set(PROJECT_NAME "KeyBoard")
set(TARGET_ARTIFACT "${PROJECT_NAME}.elf")
set(MAP_FILE "${PROJECT_NAME}.map")
set(HEX_FILE "${PROJECT_NAME}.hex")
set(LST_FILE "${PROJECT_NAME}.lst")
set(BIN_FILE "${PROJECT_NAME}.bin")

# 公共源文件模式（KeyBoard 与 Bootloader 共用）
set(COMMON_SRC_PATTERNS
        "${CMAKE_CURRENT_SOURCE_DIR}/User/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/USBLIB/USB-Driver/src/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/USBLIB/CONFIG/src/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Peripheral/src/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Debug/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/*.c"
)

# 公共 include 列表（复用）
set(COMMON_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/Debug"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core"
        "${CMAKE_CURRENT_SOURCE_DIR}/User"
        "${CMAKE_CURRENT_SOURCE_DIR}/Peripheral/inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/KeyBoard/inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/USBLIB/CONFIG/inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/USBLIB/USB-Driver/inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/Startup"
)

# 公共编译/链接标志
set(COMMON_RISCV_FLAGS " -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -flto -pipe -Wunused -Wuninitialized -g")
set(RTL_DUMP_FLAGS "-fdump-rtl-expand")

set(CMAKE_C_FLAGS "${COMMON_RISCV_FLAGS} -std=gnu11 ${RTL_DUMP_FLAGS}")
set(CMAKE_CXX_FLAGS "${COMMON_RISCV_FLAGS} -std=gnu++11 -fabi-version=0 ${RTL_DUMP_FLAGS}")
set(CMAKE_ASM_FLAGS "${COMMON_RISCV_FLAGS} -x assembler-with-cpp ${RTL_DUMP_FLAGS}")


add_compile_options(-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=..)

SET(CMAKE_C_COMPILER_WORKS TRUE)
SET(CMAKE_CXX_COMPILER_WORKS TRUE)
SET(CMAKE_ASM_COMPILER_WORKS TRUE)


if (DEFINED ENV{TOOLCHAIN_FOLDER})
    message(STATUS "Using TOOLCHAIN_FOLDER from environment: $ENV{TOOLCHAIN_FOLDER}")
    set(TOOLCHAIN_FOLDER "$ENV{TOOLCHAIN_FOLDER}")
else ()
    set(TOOLCHAIN_FOLDER "f:/MounRiver/MounRiver_Studio2/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC15")
    message(STATUS "TOOLCHAIN_FOLDER not set in environment; using default: ${TOOLCHAIN_FOLDER}")
endif ()
set(TOOLCHAIN_PREFIX "riscv32-wch-elf-")


set(CMAKE_C_COMPILER "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}gcc.exe")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}g++.exe")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}gcc.exe")
set(CMAKE_OBJCOPY "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}objcopy.exe")
set(CMAKE_OBJDUMP "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}objdump.exe")
set(CMAKE_SIZE "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}size.exe")
set(CMAKE_AR "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}ar.exe")


set(CMAKE_C_OUTPUT_EXTENSION .o)
set(CMAKE_CXX_OUTPUT_EXTENSION .o)

project("${PROJECT_NAME}" LANGUAGES C ASM CXX)

find_package(Python3 COMPONENTS Interpreter QUIET)


# 通用目标配置函数：设置 include、linker、输出及构建后操作（post-build）
function(configure_firmware_target target_name opt_flags runtime_name hex_file bin_file lst_file map_file linker_script)
    target_include_directories(${target_name} PRIVATE ${COMMON_INCLUDE_DIRS})
    set_target_properties(${target_name} PROPERTIES LINK_FLAGS "${COMMON_RISCV_FLAGS} -T \"${CMAKE_CURRENT_SOURCE_DIR}/${linker_script}\" -nostartfiles -Xlinker --gc-sections -Xlinker --print-memory-usage -Wl,-Map,\"${map_file}\" --specs=nano.specs --specs=nosys.specs")
    set_target_properties(${target_name} PROPERTIES RUNTIME_OUTPUT_NAME "${runtime_name}")
    set_target_properties(${target_name} PROPERTIES PREFIX "" SUFFIX ".elf")

    if ("${opt_flags}" STREQUAL "")
        set(opt_flags "-O2")
    endif ()
    target_compile_options(${target_name} PRIVATE ${opt_flags})

    add_custom_command(TARGET ${target_name}
            POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} ARGS -O ihex "$<TARGET_FILE:${target_name}>" "${hex_file}"
            COMMAND ${CMAKE_OBJCOPY} ARGS -O binary "$<TARGET_FILE:${target_name}>" "${bin_file}"
            COMMAND ${CMAKE_OBJDUMP} ARGS --all-headers --demangle --disassemble -M xw "$<TARGET_FILE:${target_name}>" > "${lst_file}"
            COMMAND ${CMAKE_SIZE} ARGS --format=berkeley "$<TARGET_FILE:${target_name}>"
    )
endfunction()

# 为 KeyBoardTarget 应用通用配置

# 应用公共源码模式并向目标添加特定源码
file(GLOB SRC_FILES ${COMMON_SRC_PATTERNS} "${CMAKE_CURRENT_SOURCE_DIR}/KeyBoard/src/*.c")
list(APPEND SRC_FILES "KeyBoard/startup_app.S")
add_executable(KeyBoardTarget ${SRC_FILES})
configure_firmware_target(KeyBoardTarget "-Ofast" "${PROJECT_NAME}" "${HEX_FILE}" "${BIN_FILE}" "${LST_FILE}" "${MAP_FILE}" "KeyBoard/Link_app.Ld")


# 引导程序（Bootloader）相关配置
set(BOOT_PROJECT_NAME "bootloader")
set(BOOT_TARGET "${BOOT_PROJECT_NAME}.elf")
set(BOOT_HEX "${BOOT_PROJECT_NAME}.hex")
set(BOOT_BIN "${BOOT_PROJECT_NAME}.bin")
set(BOOT_LST "${BOOT_PROJECT_NAME}.lst")


# 使用公共源码模式并添加引导程序（bootloader）特定源码
# 只收集 bootloader 自己的源文件，避免意外包含应用层的源（例如 KeyBoard/src/main.c），
# 否则会造成 `main` 或 `_start` 的重复定义。
file(GLOB BOOT_SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Bootloader/src/*.c")
list(APPEND BOOT_SRC_FILES "Bootloader/startup_bootloader.S")
add_executable(BootloaderTarget ${BOOT_SRC_FILES})
# 使用通用配置包装 BootloaderTarget（引导程序）
configure_firmware_target(BootloaderTarget "-Os" "${BOOT_PROJECT_NAME}" "${BOOT_HEX}" "${BOOT_BIN}" "${BOOT_LST}" "${BOOT_PROJECT_NAME}.map" "Bootloader/Link_bootloader.ld")


# 合并固件目标（用于测试）：将引导程序与应用合并为一个二进制文件
set(COMBINED_BIN "${CMAKE_CURRENT_BINARY_DIR}/combined.bin")
set(COMBINED_HEX "${CMAKE_CURRENT_BINARY_DIR}/combined.hex")
set(APP_OFFSET 0x2000)

if (NOT Python3_Interpreter_FOUND)
    message(STATUS "未检测到 Python3 解释器；如果 PATH 中存在 'python'，combined_firmware 目标仍可用。")
    set(PYTHON_EXECUTABLE python)
else ()
    message(STATUS "检测到 Python3 解释器： ${Python3_EXECUTABLE}")
    set(PYTHON_EXECUTABLE "${Python3_EXECUTABLE}")
endif ()

add_custom_command(
        OUTPUT ${COMBINED_BIN}
        COMMAND ${CMAKE_COMMAND} -E echo "Merging bootloader and app into ${COMBINED_BIN}"
        COMMAND ${PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/merge_firmware.py" "${CMAKE_CURRENT_SOURCE_DIR}/obj/bootloader.bin" "${CMAKE_CURRENT_SOURCE_DIR}/obj/KeyBoard.bin" ${COMBINED_BIN} ${APP_OFFSET}
        COMMAND ${CMAKE_OBJCOPY} ARGS -I binary -O ihex ${COMBINED_BIN} ${COMBINED_HEX}
        DEPENDS BootloaderTarget KeyBoardTarget
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Create combined firmware binary and hex"
)

add_custom_target(combined_firmware ALL DEPENDS ${COMBINED_BIN})

