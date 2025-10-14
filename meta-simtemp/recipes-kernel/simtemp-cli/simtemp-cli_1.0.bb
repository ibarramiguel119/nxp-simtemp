SUMMARY = "CLI for interacting with /dev/simtemp"
LICENSE = "CLOSED"

SRC_URI = "file://simtemp_cli.py"

# No hay compilación
do_compile() {
    :
}

# No hay unpack real
do_unpack() {
    :
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${THISDIR}/files/simtemp_cli.py ${D}${bindir}/simtemp-cli
}

RDEPENDS_${PN} = "python3"
FILES_${PN} = "${bindir}/simtemp-cli"

inherit allarch
