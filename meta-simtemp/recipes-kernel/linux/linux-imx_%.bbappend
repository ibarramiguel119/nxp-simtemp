# =================================================================
# Overlay simtemp para kernel i.MX
# =================================================================
DESCRIPTION = "Overlay para el driver simtemp"
LICENSE = "CLOSED"

# =================================================================
# Archivos adicionales (donde está el DTS)
# =================================================================
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# =================================================================
# Dependencias necesarias
# =================================================================
DEPENDS += "dtc-native"

# =================================================================
# Compilar el overlay DTS manualmente
# =================================================================
do_compile:append() {
    echo "==> Copiando simtemp-overlay.dts y compilando..."

    mkdir -p ${B}/overlays
    echo "==> paso esta ruta 1..." 

    # Copiar el DTS desde el layer actual
    cp /home/elibert/prueba_copilacion_yocto/imx-yocto-bsp/sources/meta-simtemp/recipes-kernel/linux/files/simtemp-overlay.dts ${B}/simtemp-overlay.dts

    echo "==> paso esta ruta  2..." 

    # Compilar el overlay
    dtc -@ -I dts -O dtb -o ${B}/overlays/simtemp.dtbo ${B}/simtemp-overlay.dts

}

# =================================================================
# Instalar y desplegar el overlay compilado
# =================================================================
do_install:append() {
    install -Dm 0644 ${B}/overlays/simtemp.dtbo ${D}/boot/overlays/simtemp.dtbo
}

do_deploy:append() {
    install -Dm 0644 ${B}/overlays/simtemp.dtbo ${DEPLOYDIR}/overlays/simtemp.dtbo
}

PACKAGE_ARCH = "${MACHINE_ARCH}"
