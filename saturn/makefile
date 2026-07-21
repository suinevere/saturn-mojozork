# Configuration
SRL_MAX_TEXTURES = 100          # Number of VDP1 texture slots
SRL_MODE = NTSC                 # Valid options are PAL or NTSC
SRL_HIGH_RES = 0                # 480i mode
SRL_FRAMERATE = 1               # Framerate control (0=dynamic, 1=< 60/value)
SRL_MAX_CD_BACKGROUND_JOBS = 1  # Maximum number of files GFS can open at once
SRL_MAX_CD_FILES = 256          # Maximum number of files on a CD
SRL_MAX_CD_RETRIES = 5          # Number of times to retry on unsuccessful read

# Sound driver specific configuration
SRL_USE_SGL_SOUND_DRIVER = 1    # Set to 1 if you want to use SGL sound driver, this will copy necessary files into the CD folder
SRL_ENABLE_FREQ_ANALYSIS = 0    # Set to 1 if you want to enable frequency analysis for CD audio, this will load a DSP program into effect slot 1, SGL sound driver must be enabled

# SGL configuration
SGL_MAX_VERTICES = 2500         # Number of vertices that can be used
SGL_MAX_POLYGONS = 1500         # Number of polygons that can be used
SGL_MAX_EVENTS = 1             	# Number of events that can be used
SGL_MAX_WORKS = 1             	# Number of works that can be used 

# Disk name. Drives every BuildDrop artifact basename (.elf/.iso/.bin/.cue/.map)
# and the cue's FILE line, so the build output is release-named as-is.
# Safe despite the spaces/parens: shared.mk never uses BUILD_* as a rule target,
# only inside recipes where each reference is already double-quoted.
CD_NAME = Zaturn (USA) (Netlink Edition)

# Directory build will be placed into
BUILD_DROP = ./BuildDrop

# SRL installation directory
SRL_INSTALL_ROOT ?= ../SaturnRingLib

# Find all .c and .cxx files
SOURCES = $(patsubst ./%,%,$(shell find src/ -name '*.c')) 
SOURCES += $(patsubst ./%,%,$(shell find src/ -name '*.cxx'))

# --- .netbin build ------------------------------------------------------
# `make NETBIN=1` (see compile-netbin.bat) links a self-contained image for
# the PlanetWeb 4.0 loader: base 0x06010000 instead of 0x06004000, with the
# story file and SGL sound driver embedded rather than read from CD.
#
# LDFILE cannot be set here: shared.mk:10 assigns it with `=` and this file
# is included first, so the SDK would overwrite us. compile-netbin.bat passes
# it on the make command line, which does override a makefile assignment.
#
# SRL_USE_SGL_SOUND_DRIVER deliberately stays 1. The netbin still needs SRL's
# sound stack compiled; it only replaces where the driver bytes come from
# (see netbin_sound.cxx).
ifeq ($(strip $(NETBIN)),1)
SRL_CUSTOM_CCFLAGS += -DNETBIN
BUILD_NETBIN = $(BUILD_DROP)/zaturn.netbin
endif

# Include shared makefile
SDK_ROOT = $(SRL_INSTALL_ROOT)/saturnringlib
include $(SDK_ROOT)/shared.mk