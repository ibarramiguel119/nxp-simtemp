SUMMARY = "Simtemp kernel module"
LICENSE = "CLOSED"

inherit module

SRC_URI = "git://github.com/ibarramiguel119/nxp-simtemp.git;protocol=https;branch=refactor/simtemp-unified-source"
SRCREV = "${AUTOREV}"


S = "${WORKDIR}/git/kernel"


# The inherit of module.bbclass will automatically name module packages with
# "kernel-module-" prefix as required by the oe-core build environment.

RPROVIDES:${PN} += "kernel-module-hello"
KERNEL_MODULE_AUTOLOAD += "simtemp"

