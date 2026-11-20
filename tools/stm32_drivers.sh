#!/bin/sh
# stm32_drivers.sh — copies the ST drivers a target needs (HAL, CMSIS, board BSP, audio codec) from the local
# STM32Cube firmware repository into target/<board>/Drivers, so they are not versioned in git.
# Run before every build (`just h745`): CubeMX deletes the HAL files its own configuration does not use
# when it regenerates, the board support packages need them. The firmware version is read from the .ioc. Install it once from STM32CubeMX:
# Help > Manage embedded software packages.
#
#   sh tools/stm32_drivers.sh            # target/h745
#   sh tools/stm32_drivers.sh h745
set -eu
board=${1:-h745}
root=$(cd "$(dirname "$0")/.." && pwd)
dir="$root/target/$board"
ioc=$(ls "$dir"/*.ioc 2>/dev/null | head -1) || true
[ -n "$ioc" ] || { echo "no .ioc in $dir" >&2; exit 1; }

# "STM32Cube FW_H7 V1.13.0" -> STM32Cube_FW_H7_V1.13.0
pkg=$(sed -n 's/^ProjectManager.FirmwarePackage=//p' "$ioc" | tr ' ' '_')
repo="${STM32CUBE_REPOSITORY:-$HOME/STM32Cube/Repository}/$pkg"
[ -d "$repo/Drivers" ] || {
    echo "$pkg is not in ${STM32CUBE_REPOSITORY:-$HOME/STM32Cube/Repository}:" >&2
    echo "install it in STM32CubeMX (Help > Manage embedded software packages), or set STM32CUBE_REPOSITORY" >&2
    exit 1
}

case "$board" in
    h745) family=STM32H7xx bsp=STM32H745I-DISCO codec=wm8994 components="rk043fn48h mt48lc4m32b2 ft5336 gt911 lan8742" ;;
    *) echo "unknown board: $board" >&2; exit 1 ;;
esac

out="$dir/Drivers"
mkdir -p "$out/CMSIS/Device/ST/$family" "$out/${family}_HAL_Driver" "$out/BSP/Components"
sync() { rsync -a "$@"; } # keeps the dates: files already there are not rebuilt
sync "$repo/Drivers/CMSIS/Include" "$out/CMSIS/"
sync "$repo/Drivers/CMSIS/Device/ST/$family/Include" "$out/CMSIS/Device/ST/$family/"
sync "$repo/Drivers/${family}_HAL_Driver/Inc" "$repo/Drivers/${family}_HAL_Driver/Src" "$out/${family}_HAL_Driver/"
mkdir -p "$out/BSP/$bsp"
sync "$repo/Drivers/BSP/$bsp"/*.[ch] "$out/BSP/$bsp/"
sync "$repo/Drivers/BSP/Components/Common" "$repo/Drivers/BSP/Components/$codec" "$out/BSP/Components/"
# the M4's screen: display panel, SDRAM chip, touch controllers
for c in $components; do sync "$repo/Drivers/BSP/Components/$c" "$out/BSP/Components/"; done
# the BSP's audio file links to ST's PDM microphone filter (headers and the Cortex-M7 GCC library)
pdm=Middlewares/ST/STM32_Audio/Addons/PDM
mkdir -p "$out/$pdm/Lib"
sync "$repo/$pdm/Inc" "$out/$pdm/"
sync "$repo/$pdm/Lib/libPDMFilter_CM7_GCC_wc32.a" "$out/$pdm/Lib/"
echo "$pkg -> $out ($(du -sh "$out" | cut -f1))"
