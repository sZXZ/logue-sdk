##############################################################################
# Configuration for Makefile
#

PROJECT := wavetable
PROJECT_TYPE := genericfx

##############################################################################
# Sources
#

# C sources
# (the per-variant baked wavetable wt_data.c is appended by the Makefile)
UCSRC = header.c

# C++ sources
UCXXSRC = unit.cc wt_osc.cpp

# List ASM source files here
UASMSRC =

UASMXSRC =

##############################################################################
# Include Paths
#

UINCDIR  =

##############################################################################
# Headers
#
# Listed so that editing one of them rebuilds the objects that include it; the
# per-variant wt_data.h is appended by the Makefile
##############################################################################

UHDR = wt_osc.h

##############################################################################
# Library Paths
#

ULIBDIR =

##############################################################################
# Libraries
#

ULIBS  = -lm

##############################################################################
# Macros
#

UDEFS =
