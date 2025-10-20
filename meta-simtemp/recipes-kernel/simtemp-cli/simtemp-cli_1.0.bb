# =================================================================
# CLI for interacting with the NXP SimTemp kernel module
# =================================================================
SUMMARY = "CLI for interacting with /dev/simtemp"
LICENSE = "CLOSED"

# =================================================================
# Source code comes from the same Git repository as the driver
# =================================================================
SRC_URI = "git://github.com/ibarramiguel119/nxp-simtemp.git;protocol=https;branch=refactor/simtemp-unified-source"

# Specific commit to ensure reproducible builds
SRCREV = "${AUTOREV}"

# Directory inside the repository where the Python script resides
S = "${WORKDIR}/git/user/cli"

# =================================================================
# Architecture independent (Python script)
# =================================================================
inherit allarch

# =================================================================
# No compilation required
# =================================================================
do_compile() {
    :
}

# =================================================================
# No unpacking needed (Git fetch handled by Yocto)
# =================================================================
do_unpack() {
    :
}

# =================================================================
# Install the CLI script into the bindir
# =================================================================
do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/simtemp_cli.py ${D}${bindir}/simtemp-cli
   
}

# =================================================================
# Runtime dependencies
# =================================================================
RDEPENDS_${PN} = "python3"

# =================================================================
# Files to include in the package
# =================================================================
FILES_${PN} = "${bindir}/simtemp-cli"
