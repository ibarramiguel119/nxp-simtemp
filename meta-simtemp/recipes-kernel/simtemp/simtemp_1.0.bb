# =================================================================
# SimTemp kernel module
# =================================================================
SUMMARY = "Simtemp kernel module"
LICENSE = "CLOSED"
DESCRIPTION = "Kernel module for a simulated temperature sensor"

# =================================================================
# Inherit the module class to build a kernel module
# =================================================================
inherit module

# =================================================================
# Source code location: Git repository
# =================================================================
SRC_URI = "git://github.com/ibarramiguel119/nxp-simtemp.git;protocol=https;branch=master"

# Use the latest commit on the branch (can also use a fixed commit hash for reproducibility)
SRCREV = "${AUTOREV}"

# =================================================================
# Set the source directory inside WORKDIR
# =================================================================
S = "${WORKDIR}/sources"
UNPACKDIR = "${S}"

# =================================================================
# The 'module.bbclass' will automatically name the package with
# the 'kernel-module-' prefix required by oe-core
# =================================================================
RPROVIDES:${PN} += "kernel-module-hello"

# =================================================================
# Automatically load the module at boot
# =================================================================
KERNEL_MODULE_AUTOLOAD += "simtemp"
