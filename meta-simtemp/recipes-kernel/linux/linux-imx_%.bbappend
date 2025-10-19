# =================================================================
# Simtemp Overlay for i.MX Kernel
# =================================================================
DESCRIPTION = "Overlay for the simtemp driver"
LICENSE = "CLOSED"

# =================================================================
# Additional Files (location of the DTS)
# =================================================================
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# =================================================================
# Required Dependencies
# =================================================================
DEPENDS += "dtc-native"

# =================================================================
# Compile the DTS Overlay Manually
# =================================================================
do_compile:append() {
    echo "==> Copying simtemp-overlay.dts and compiling..."

    mkdir -p ${B}/overlays
    echo "==> Passing path 1..."  

    # Copy the DTS from the current layer
    cp /home/elibert/prueba_copilacion_yocto/imx-yocto-bsp/sources/meta-simtemp/recipes-kernel/linux/files/simtemp-overlay.dts ${B}/simtemp-overlay.dts

    echo "==> Passing path 2..."  

    # Compile the overlay
    dtc -@ -I dts -O dtb -o ${B}/overlays/simtemp.dtbo ${B}/simtemp-overlay.dts
}

# =================================================================
# Install and Deploy the Compiled Overlay
# =================================================================
do_install:append() {
    install -Dm 0644 ${B}/overlays/simtemp.dtbo ${D}/boot/overlays/simtemp.dtbo
}

do_deploy:append() {
    install -Dm 0644 ${B}/overlays/simtemp.dtbo ${DEPLOYDIR}/overlays/simtemp.dtbo
}

PACKAGE_ARCH = "${MACHINE_ARCH}"
