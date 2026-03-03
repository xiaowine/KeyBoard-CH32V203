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

# Set CMake minimum require
cmake_minimum_required(VERSION 3.16)

# Set CMake sysytem name
set(CMAKE_SYSTEM_NAME Generic)


# Skipping compiler detection and use custom toolchain
SET(CMAKE_C_COMPILER_WORKS TRUE)
SET(CMAKE_CXX_COMPILER_WORKS TRUE)
SET(CMAKE_ASM_COMPILER_WORKS TRUE)

# Use MRS toolchian to compile
set(TOOLCHAIN_FOLDER "f:/MounRiver/MounRiver_Studio2/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC12")
set(TOOLCHAIN_PREFIX "riscv-wch-elf-")
set(CMAKE_C_COMPILER "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}gcc.exe")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}g++.exe")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}gcc.exe")
set(CMAKE_OBJCOPY "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}objcopy.exe")
set(CMAKE_OBJDUMP "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}objdump.exe")
set(CMAKE_SIZE "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}size.exe")
set(CMAKE_AR "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}ar.exe")

set(PROJECT_NAME "KeyBoard")
set(TARGET_ARTIFACT "${PROJECT_NAME}.elf")
set(MAP_FILE "${PROJECT_NAME}.map")
set(HEX_FILE "${PROJECT_NAME}.hex")
set(LST_FILE "${PROJECT_NAME}.lst")

set(CMAKE_C_OUTPUT_EXTENSION .o)
set(CMAKE_CXX_OUTPUT_EXTENSION .o)

project("${PROJECT_NAME}" LANGUAGES C ASM CXX)

add_compile_options(-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=..)


file(GLOB SRC_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/User/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/USBLIB/USB-Driver/src/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/USBLIB/CONFIG/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Peripheral/src/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/KeyBoard/src/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Debug/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/*.c"
)

list(APPEND SRC_FILES
        "Startup/startup_ch32v20x_D6.S"
)
set(COMMON_RISCV_FLAGS " -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Ofast -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -flto -pipe -Wunused -Wuninitialized -g")

set(COMMON_INCLUDE_DIRS " -I\"${CMAKE_CURRENT_SOURCE_DIR}/Debug\" -I\"${CMAKE_CURRENT_SOURCE_DIR}/Core\" -I\"${CMAKE_CURRENT_SOURCE_DIR}/User\" -I\"${CMAKE_CURRENT_SOURCE_DIR}/Peripheral/inc\" -I\"${CMAKE_CURRENT_SOURCE_DIR}/KeyBoard/inc\" -I\"${CMAKE_CURRENT_SOURCE_DIR}/USBLIB/CONFIG\" -I\"${CMAKE_CURRENT_SOURCE_DIR}/USBLIB/USB-Driver/inc\"")

set(CMAKE_C_FLAGS "${COMMON_RISCV_FLAGS}${COMMON_INCLUDE_DIRS} -std=gnu11")
set(CMAKE_CXX_FLAGS "${COMMON_RISCV_FLAGS} -std=gnu++11 -fabi-version=0")
set(CMAKE_ASM_FLAGS "${COMMON_RISCV_FLAGS} -I\"${CMAKE_CURRENT_SOURCE_DIR}/Startup\" -x assembler-with-cpp")

add_executable("${TARGET_ARTIFACT}" ${SRC_FILES})

set_target_properties("${TARGET_ARTIFACT}" PROPERTIES LINK_FLAGS "${COMMON_RISCV_FLAGS} -T \"${CMAKE_CURRENT_SOURCE_DIR}/Ld/Link.ld\" -nostartfiles -Xlinker --gc-sections -Xlinker --print-memory-usage -Wl,-Map,\"${MAP_FILE}\" --specs=nano.specs --specs=nosys.specs -o \"${TARGET_ARTIFACT}\"")

target_link_libraries("${TARGET_ARTIFACT}")

add_custom_command(TARGET "${TARGET_ARTIFACT}"
        POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} ARGS -O ihex "${TARGET_ARTIFACT}" "${HEX_FILE}"
        COMMAND ${CMAKE_OBJDUMP} ARGS --all-headers --demangle --disassemble -M xw "${TARGET_ARTIFACT}" > "${LST_FILE}"
        COMMAND ${CMAKE_SIZE} ARGS --format=berkeley "${TARGET_ARTIFACT}"
)

