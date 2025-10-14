SUMMARY = "Simtemp kernel module"
LICENSE = "CLOSED"

inherit module

SRC_URI = "file://Makefile \
           file://simtemp.c \
	   file://nxp-simtemp.dtsi\
          "

S = "${WORKDIR}/sources"
UNPACKDIR = "${S}"

# The inherit of module.bbclass will automatically name module packages with
# "kernel-module-" prefix as required by the oe-core build environment.

RPROVIDES:${PN} += "kernel-module-hello"


