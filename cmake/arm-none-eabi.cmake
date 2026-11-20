# arm-none-eabi.cmake — Cortex-M7 cross toolchain (H745 / F746), hardware single-precision FPU.
# Usage: cmake -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# The compiler is looked up in PATH, then in $ARM_TOOLCHAIN/bin, then in the usual install places of the
# Arm GNU Toolchain (the .pkg in /Applications/ArmGNUToolchain, or the tarball unpacked in ~/opt).
file(GLOB _arm_dirs "$ENV{HOME}/opt/arm-gnu-toolchain-*/bin" "/Applications/ArmGNUToolchain/*/arm-none-eabi/bin")
list(SORT _arm_dirs ORDER DESCENDING) # newest version first
find_program(ARM_GCC arm-none-eabi-gcc HINTS "$ENV{ARM_TOOLCHAIN}/bin" ${_arm_dirs} REQUIRED)
get_filename_component(_arm_bin ${ARM_GCC} DIRECTORY)
set(CMAKE_C_COMPILER ${ARM_GCC})
set(CMAKE_ASM_COMPILER ${ARM_GCC})
set(CMAKE_AR ${_arm_bin}/arm-none-eabi-ar)
set(CMAKE_RANLIB ${_arm_bin}/arm-none-eabi-ranlib)

# fpv5-sp-d16: single-precision FPU, shared with the F746's M7; the H745 also has double precision, not needed here.
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
