##############################################################################
# Common project definitions
#

MKFILE_PATH := $(realpath $(lastword $(MAKEFILE_LIST)))

# Project root
PROJECT_ROOT ?= $(dir $(MKFILE_PATH))

# Same, without the trailing slash, for use inside target names
PROJDIR := $(patsubst %/,%,$(PROJECT_ROOT))

# Common includes
COMMON_INC_PATH ?= $(realpath $(PROJECT_ROOT)/../common/)

# Common sources
COMMON_SRC_PATH ?= $(realpath $(PROJECT_ROOT)/../common/)

# Installation directory
INSTALLDIR ?= $(PROJDIR)

# Tools directory
TOOLSDIR ?= $(PROJDIR)/../../../tools

# External library directory
EXTDIR ?= $(PROJDIR)/../../ext

# CMSIS library location
CMSISDIR ?= $(EXTDIR)/CMSIS/CMSIS

# Linker scripts location
LDDIR ?= $(PROJDIR)/../ld

##############################################################################
# Include custom project configuration and sources
#

include config.mk

##############################################################################
# Wavetable variants
#
# Every wav in wt/ is baked into build/gen/<name>/wt_data.{h,c} by
# tools/wav2table.py, and every wav is then built as the same six variants the
# acid unit uses (one per build option combination), so one folder of samples
# turns into six folders of .nts3unit files:
#
#   <wav stem>          the baked wavetable, e.g. bass.nts3unit
#   <wav stem>_evo      + -DAUTODRIFT, the 16 step line mutates once per bar
#   <wav stem>_L        + -DUNIT_OUT_LEFT, voice on the left output only
#   <wav stem>_R        + -DUNIT_OUT_RIGHT, voice on the right output only
#   <wav stem>_evo_L    both of the above
#   <wav stem>_evo_R    both of the above
#
# The wav stem is passed down as -DWT_NAME and selects that table's generated
# source and include directory; the axis suffix only adds build macros, so all
# six variants of a wav share one baked table. The generated header carries the
# on-device unit name and unit id for the wav; header.c combines them with the
# axis macros. Drop a new wav into wt/ and `make` builds its six variants.
#
#   make WTNAME=lead WTAXIS=evo_L wasm   build one variant for the sandbox
#   make bake                            re-bake the tables without building
#

WTDIR   := $(PROJDIR)/wt
GENDIR  := $(PROJDIR)/build/gen
BAKER   := $(PROJDIR)/tools/wav2table.py
WTSRCS  := $(wildcard $(WTDIR)/*.wav)
WTSTEMS := $(notdir $(basename $(WTSRCS)))

ifeq ($(strip $(WTSRCS)),)
$(error no wav files in $(WTDIR) -- nothing to build)
endif

# The build option axes, crossed with the wav list. `plain` is the unadorned
# unit and keeps the bare wav stem as its variant name; the macros are
# independent and combine, so the cross product is the full 2x2x2 lattice (evo x
# left x right) with plain as its origin. See the build option block in
# wt_osc.h and the variant selection in header.c.
WT_AXES := plain evo L R evo_L evo_R

WT_AXIS_suffix_plain :=
WT_AXIS_suffix_evo   := _evo
WT_AXIS_suffix_L     := _L
WT_AXIS_suffix_R     := _R
WT_AXIS_suffix_evo_L := _evo_L
WT_AXIS_suffix_evo_R := _evo_R

WT_AXIS_defines_plain :=
WT_AXIS_defines_evo   := -DAUTODRIFT
WT_AXIS_defines_L     := -DUNIT_OUT_LEFT
WT_AXIS_defines_R     := -DUNIT_OUT_RIGHT
WT_AXIS_defines_evo_L := -DAUTODRIFT -DUNIT_OUT_LEFT
WT_AXIS_defines_evo_R := -DAUTODRIFT -DUNIT_OUT_RIGHT

# WTPAD_PITCH (header.c) is orthogonal to the axes: it is not a variant, it is
# `make WTPAD_PITCH=1`, which adds the X-across-the-pad behaviour to all of
# them. It has to be a variable of its own because the variant rules pass
# UDEFS= on the sub-make command line, which would shadow a UDEFS= given here.
ifdef WTPAD_PITCH
WT_EXTRA_DEFINES := -DWTPAD_PITCH
else
WT_EXTRA_DEFINES :=
endif

# One variant per (wav, axis) pair: the wav stem, plus the axis suffix (empty
# for plain). -DWT_NAME names the wav, the axis only contributes its own
# defines -- both deferred, so one WT_VARIANT call covers every stem.
WT_NAMES := $(foreach v,$(WTSTEMS),$(foreach a,$(WT_AXES),$(v)$(WT_AXIS_suffix_$(a))))

VARIANTS := $(WT_NAMES)

define WT_VARIANT
variant_$(1)_defines := -DWT_NAME=$(2) $$(WT_AXIS_defines_$(3)) $$(WT_EXTRA_DEFINES)
variant_$(1)_srcs := $(GENDIR)/$(2)/wt_data.c
variant_$(1)_hdrs := $(GENDIR)/$(2)/wt_data.h
variant_$(1)_incs := $(GENDIR)/$(2)
endef
$(foreach v,$(WTSTEMS),$(foreach a,$(WT_AXES),$(eval $(call WT_VARIANT,$(v)$(WT_AXIS_suffix_$(a)),$(v),$(a)))))

GEN := $(patsubst $(WTDIR)/%.wav,$(GENDIR)/%/wt_data.c,$(WTSRCS))

# Table + variant built by `make wasm` (the first one unless overridden)
WTNAME ?= $(firstword $(WTSTEMS))
WTAXIS ?= plain
WTGEN  := $(GENDIR)/$(WTNAME)

##############################################################################
# Common defaults
#

# Define project name here
PROJECT ?= wavetable

##############################################################################
# Setup cross compilation
#

MCU := cortex-m7

MCU_MODEL := STM32H725xE

GCC_TARGET := arm-none-eabi-
GCC_BIN_PATH ?= $(TOOLSDIR)/gcc/gcc-arm-none-eabi-10.3-2021.10/bin

CC    := $(GCC_BIN_PATH)/$(GCC_TARGET)gcc
CXXC  := $(GCC_BIN_PATH)/$(GCC_TARGET)g++
LD    := $(GCC_BIN_PATH)/$(GCC_TARGET)gcc
#LD   := $(GCC_BIN_PATH)/$(GCC_TARGET)g++
CP    := $(GCC_BIN_PATH)/$(GCC_TARGET)objcopy
AS    := $(GCC_BIN_PATH)/$(GCC_TARGET)gcc -x assembler-with-cpp
AR    := $(GCC_BIN_PATH)/$(GCC_TARGET)ar
OD    := $(GCC_BIN_PATH)/$(GCC_TARGET)objdump
SZ    := $(GCC_BIN_PATH)/$(GCC_TARGET)size
STRIP := $(GCC_BIN_PATH)/$(GCC_TARGET)strip

HEX   := $(CP) -O ihex
BIN   := $(CP) -O binary

RULESPATH := $(LDDIR)
LDSCRIPT := $(LDDIR)/unit.ld
DLIBS := -lc

DADEFS := -D$(MCU_MODEL) -DCORTEX_USE_FPU=TRUE -DARM_MATH_CM7
DDEFS := -D$(MCU_MODEL) -DCORTEX_USE_FPU=TRUE -DARM_MATH_CM7 -D__FPU_PRESENT

COPT := -fPIC -std=c11 -fno-exceptions
CXXOPT := -fPIC -fno-use-cxa-atexit -std=c++11 -fno-rtti -fno-exceptions -fno-non-call-exceptions

LDOPT := -shared --entry=0 -specs=nano.specs -specs=nosys.specs

CWARN := -W -Wall -Wextra
CXXWARN :=

FPU_OPTS := -mfloat-abi=hard -mfpu=fpv4-sp-d16 -fsingle-precision-constant -fcheck-new

OPT := -g -Os -mlittle-endian
OPT += $(FPU_OPTS)

## TODO: there seems to be a bug or some yet unknown behavior that breaks PLT code for external calls when LTO is enabled alongside -nostartfiles
#OPT += -flto

TOPT := -mthumb -mno-thumb-interwork -DTHUMB_NO_INTERWORKING -DTHUMB_PRESENT

##############################################################################
# Set compilation targets and directories
#

PRODUCT := $(PROJECT).nts3unit

BUILDDIR := $(PROJDIR)/build/$(PROJECT)
OBJDIR := $(BUILDDIR)/obj
LSTDIR := $(BUILDDIR)/lst

ASMSRC := $(UASMSRC)

ASMXSRC := $(UASMXSRC)

CSRC := $(UCSRC)
CSRC += $(realpath $(COMMON_SRC_PATH)/_unit_base.c)

CXXSRC := $(filter %.cc,$(UCXXSRC))
CXXPSRC := $(filter %.cpp,$(UCXXSRC))

# Headers: the project's own plus the generated one for this variant. The object
# rules depend on them, since the Makefile keeps no .d files.
HDRS := $(UHDR) $(COMMON_INC_PATH)/processor.h \
        $(COMMON_INC_PATH)/unit_genericfx.h \
        $(COMMON_INC_PATH)/fx_api.h \
        $(COMMON_INC_PATH)/osc_api.h \
        $(COMMON_INC_PATH)/utils/float_math.h

vpath %.s $(sort $(dir $(ASMSRC)))
vpath %.S $(sort $(dir $(ASMXSRC)))
vpath %.c $(sort $(dir $(CSRC)))
vpath %.cc $(sort $(dir $(CXXSRC)))
vpath %.cpp $(sort $(dir $(CXXPSRC)))

ASMOBJS := $(addprefix $(OBJDIR)/, $(notdir $(ASMSRC:.s=.o)))
ASMXOBJS := $(addprefix $(OBJDIR)/, $(notdir $(ASMXSRC:.S=.o)))
COBJS := $(addprefix $(OBJDIR)/, $(notdir $(CSRC:.c=.o)))
CXXOBJS := $(addprefix $(OBJDIR)/, $(notdir $(CXXSRC:.cc=.o)))
CXXPOBJS := $(addprefix $(OBJDIR)/, $(notdir $(CXXPSRC:.cpp=.o)))

OBJS := $(ASMXOBJS) $(ASMOBJS) $(COBJS) $(CXXOBJS) $(CXXPOBJS)

DINCDIR := $(COMMON_INC_PATH) \
           $(CMSISDIR)/Include

INCDIR := $(patsubst %,-I%,$(DINCDIR) $(UINCDIR))

DEFS := $(DDEFS) $(UDEFS)
ADEFS := $(DADEFS) $(UADEFS)

LIBS := $(DLIBS) $(ULIBS)

LIBDIR := $(patsubst %,-I%,$(DLIBDIR) $(ULIBDIR))

##############################################################################
# WebAssembly compiler and binding tools
#
SANDBOXDIR ?= $(PROJDIR)/../../../websim
WASMSRC = wasm.cc $(UCSRC) $(UCXXSRC) $(wildcard $(SANDBOXDIR)/dsp/*.c) $(wildcard $(SANDBOXDIR)/dsp/*.cpp)
# One sandbox per variant, so WTNAME=bass WTAXIS=evo_L does not overwrite the
# plain bass page.
ifeq ($(WTAXIS),plain)
SIMNAME ?= $(WTNAME)
else
SIMNAME ?= $(WTNAME)_$(WTAXIS)
endif
WASMDIR := $(PROJDIR)/sim/$(SIMNAME)
# The variant's defines, so the sandbox matches the unit it previews
WTDEFS := $(variant_$(SIMNAME)_defines)
# emscripten/version.h #errors when reached through the source-tree include
# path; point at the sysroot emcc actually uses instead.
EMCC_SYSROOT_INC := $(TOOLSDIR)/emsdk/upstream/emscripten/cache/sysroot/include
DINCDIR += $(EMCC_SYSROOT_INC)
EMCC_BIN_PATH := $(TOOLSDIR)/emsdk/upstream/emscripten

##############################################################################
# Compiler flags
#

MCFLAGS   := -mcpu=$(MCU)
ODFLAGS	  := -x --syms
ASFLAGS   = $(MCFLAGS) -g $(TOPT) -Wa,-alms=$(LSTDIR)/$(notdir $(<:.s=.lst)) $(ADEFS)
ASXFLAGS  = $(MCFLAGS) -g $(TOPT) -Wa,-alms=$(LSTDIR)/$(notdir $(<:.S=.lst)) $(ADEFS)
CFLAGS    = $(MCFLAGS) $(TOPT) $(OPT) $(COPT) $(CWARN) -Wa,-alms=$(LSTDIR)/$(notdir $(<:.c=.lst)) $(DEFS)
CXXFLAGS  = $(MCFLAGS) $(TOPT) $(OPT) $(CXXOPT) $(CXXWARN) -Wa,-alms=$(LSTDIR)/$(notdir $(<:.cc=.lst)) $(DEFS)
LDFLAGS   := $(MCFLAGS) $(TOPT) $(OPT) -nostartfiles $(LIBDIR) -Wl,-z,max-page-size=128,-Map=$(BUILDDIR)/$(PROJECT).map,--cref,--no-warn-mismatch,--library-path=$(RULESPATH),--script=$(LDSCRIPT) $(LDOPT)

OUTFILES := $(BUILDDIR)/$(PROJECT).elf \
	    $(BUILDDIR)/$(PROJECT).hex \
	    $(BUILDDIR)/$(PROJECT).bin \
	    $(BUILDDIR)/$(PROJECT).dmp \
	    $(BUILDDIR)/$(PROJECT).list

##############################################################################
# Targets
#

all: PRE_ALL $(GEN) $(addprefix $(PROJDIR)/, $(addsuffix .nts3unit, $(VARIANTS))) POST_ALL
	@echo Done
	@echo

PRE_ALL:

POST_ALL:

# Bake one wav per variant into its own source + include directory. The baker
# only rewrites a file when its contents change, so an unchanged table does not
# trigger a recompile.
$(GENDIR)/%/wt_data.c: $(WTDIR)/%.wav $(BAKER)
	@echo "=== Baking wavetable $* ==="
	@mkdir -p $(dir $@)
	@python3 $(BAKER) $< $@ --name $*

bake: $(GEN)
	@echo Done
	@echo

# Build + install one variant per (wav, axis) pair via a recursive make carrying
# its defines. The variant's baked table is added to the source and include
# lists; see the wavetable section at the top of this file.
#
# One explicit rule per variant rather than a pattern rule, because the
# prerequisites have to name that variant's generated sources and headers and
# $* is not substituted inside variable references in a pattern rule's
# prerequisite list (GNU make 3.81, which is what macOS ships). Getting that
# wrong expands the prerequisites to nothing, and an existing .nts3unit is then
# considered up to date and never rebuilt.
#
# The prerequisites are what makes an existing bass.nts3unit rebuild at all:
# the recipe would otherwise never run. A pattern rule cannot be marked .PHONY
# either -- that adds an empty explicit rule which shadows it.
define WT_BUILD_RULE
$(PROJDIR)/$(1).nts3unit: $$(UCSRC) $$(UCXXSRC) $$(variant_$(1)_srcs) $$(variant_$(1)_hdrs) $$(UHDR) $$(HDRS) Makefile config.mk
	@echo "=== Building variant $(1) ==="
	$$(MAKE) --no-print-directory PROJECT=$(1) UDEFS="$$(variant_$(1)_defines)" \
		UCSRC="$$(UCSRC) $$(variant_$(1)_srcs)" UINCDIR="$$(UINCDIR) $$(variant_$(1)_incs)" \
		UHDR="$$(UHDR) $$(variant_$(1)_hdrs)" install
endef
$(foreach v,$(VARIANTS),$(eval $(call WT_BUILD_RULE,$(v))))

$(OBJS): | $(BUILDDIR) $(OBJDIR) $(LSTDIR)

$(BUILDDIR):
	@echo Compiler Options
	@echo $(CC) -c $(CFLAGS) -I. $(INCDIR)
	@echo
	@mkdir -p $(BUILDDIR)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(LSTDIR):
	@mkdir -p $(LSTDIR)

$(ASMOBJS) : $(OBJDIR)/%.o : %.s Makefile
	@echo Assembling $(<F)
	@$(AS) -c $(ASFLAGS) -I. $(INCDIR) $< -o $@

$(ASMXOBJS) : $(OBJDIR)/%.o : %.S Makefile
	@echo Assembling $(<F)
	@$(CC) -c $(ASXFLAGS) -I. $(INCDIR) $< -o $@

$(COBJS) : $(OBJDIR)/%.o : %.c $(HDRS) Makefile
	@echo Compiling $(<F)
	@$(CC) -c $(CFLAGS) -I. $(INCDIR) $< -o $@

$(CXXOBJS) : $(OBJDIR)/%.o : %.cc $(HDRS) Makefile
	@echo Compiling $(<F)
	@$(CXXC) -c $(CXXFLAGS) -I. $(INCDIR) $< -o $@

$(CXXPOBJS) : $(OBJDIR)/%.o : %.cpp $(HDRS) Makefile
	@echo Compiling $(<F)
	@$(CXXC) -c $(CXXFLAGS) -I. $(INCDIR) $< -o $@

$(BUILDDIR)/%.elf: $(OBJS) $(LDSCRIPT)
	@echo Linking $@
	@echo $(LD) $(OBJS) $(LDFLAGS) $(LIBS) -o $@
	@$(LD) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

%.hex: %.elf
	@echo Creating $@
	@$(HEX) $< $@

%.bin: %.elf
	@echo Creating $@
	@$(BIN) $< $@

%.dmp: %.elf
	@echo Creating $@
	@$(OD) $(ODFLAGS) $< > $@
	@echo
	@$(SZ) $<
	@echo

%.list: %.elf
	@echo Creating $@
	@$(OD) -S $< > $@

clean:
	@echo Cleaning
	-rm -fR $(PROJDIR)/.dep $(PROJDIR)/build $(PROJDIR)/sim $(PROJDIR)/*.nts3unit
	@echo Done
	@echo

$(BUILDDIR)/$(PRODUCT): | $(OBJS) $(OUTFILES)
	@echo Making $(BUILDDIR)/$(PRODUCT)
	@cp -a $(BUILDDIR)/$(PROJECT).elf $(BUILDDIR)/$(PRODUCT)
	@$(STRIP) $(BUILDDIR)/$(PRODUCT)

install: $(BUILDDIR)/$(PRODUCT)
	@echo Deploying to $(INSTALLDIR)/$(PRODUCT)
	@mv $(BUILDDIR)/$(PRODUCT) $(INSTALLDIR)/$(PRODUCT)
	@echo Done
	@echo

# The sandbox needs the same baked table and the same build macros as the
# hardware build.
#
#   make WTNAME=bass wasm                build the plain bass page (no browser)
#   make WTNAME=bass WTAXIS=evo_L wasm   build one variant's page instead
#   make WTNAME=bass wasmrun             build it and open it with emrun
wasm: $(WTGEN)/wt_data.c
	@echo "Building WebAssembly audio processor ($(SIMNAME))"
	@mkdir -p $(WASMDIR)
	@cp -r $(SANDBOXDIR)/samples $(WASMDIR)/
	@cp -r $(SANDBOXDIR)/scripts $(WASMDIR)/
	@cp -r $(SANDBOXDIR)/images $(WASMDIR)/
	@cp -r $(WASMSRC) $(WASMDIR)/
	@cp -r $(WTGEN)/wt_data.c $(WTGEN)/wt_data.h $(WASMDIR)/
	@$(EMCC_BIN_PATH)/emcc -Wno-unknown-attributes -Wno-limited-postlink-optimizations $(WTDEFS) $(INCDIR) -I$(WASMDIR) \
		-s AUDIO_WORKLET=1 \
		-s WASM_WORKERS=1 \
		-lembind \
		--shell-file $(SANDBOXDIR)/xypad.html \
		-O2 \
		-g -fdebug-compilation-dir='..' \
		$(WASMSRC) $(WTGEN)/wt_data.c \
		-o $(WASMDIR)/$(SIMNAME).html
	@echo Done
	@echo "Sandbox written to $(WASMDIR)/$(SIMNAME).html"
	@echo "Open it with: make WTNAME=$(WTNAME) WTAXIS=$(WTAXIS) wasmrun"

# emrun serves the page and blocks until the browser tab is closed, so it is
# kept out of the `wasm` target.
wasmrun: wasm
	@echo Opening the sandbox
	@$(EMCC_BIN_PATH)/emrun --browser chrome --serve_after_close $(WASMDIR)/$(SIMNAME).html
