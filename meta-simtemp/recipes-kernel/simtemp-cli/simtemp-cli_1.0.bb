# =================================================================
# CLI for interacting with the NXP SimTemp kernel module
# =================================================================
SUMMARY = "CLI for interacting with /dev/simtemp"
LICENSE = "CLOSED"

# =================================================================
# Source code comes from the same Git repository as the driver
# =================================================================
SRC_URI = "git://github.com/ibarramiguel119/nxp-simtemp.git;protocol=https;branch=master"

# Specific commit to ensure reproducible builds
SRCREV = "${AUTOREV}"

# Directory inside the repository where the Python script resides
S = "${WORKDIR}/git/user/cli"

# Dont exit compilation
do_compile() {
    :
}


do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/simtemp_cli.py ${D}${bindir}/simtemp-cli
}

RDEPENDS_${PN} = "python3"
FILES_${PN} = "${bindir}/simtemp-cli"

inherit allarch

