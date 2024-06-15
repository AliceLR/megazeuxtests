.PHONY: all clean
all:

COMMON_FLAGS := -O3 -g
WARNING_FLAGS := -Wall -Wextra -pedantic -Wno-unused-parameter -Wno-gnu-zero-variadic-macro-arguments
TAG :=

F_SANITIZE = ${SANITIZE}
ifneq (${FUZZER},)
SANITIZE  := ${FUZZER}
F_SANITIZE := fuzzer,${SANITIZE}
endif

ifneq (${SANITIZE},)
CC  ?= clang
CXX ?= clang++
COMMON_FLAGS := -O3 "-fsanitize=${F_SANITIZE}" -fno-omit-frame-pointer -g
TAG := _san
ifeq (${SANITIZE},address)
TAG := ${TAG}A
endif
ifeq (${SANITIZE},memory)
COMMON_FLAGS += -fsanitize-memory-track-origins=2
TAG := ${TAG}M
endif
ifeq (${SANITIZE},undefined)
COMMON_FLAGS += -fno-sanitize-recover=all -fno-sanitize=shift-base
TAG := ${TAG}U
endif
ifeq (${SANITIZE},address,undefined)
COMMON_FLAGS += -fno-sanitize-recover=all -fno-sanitize=shift-base
TAG := ${TAG}AU
endif
endif

COMMON_FLAGS += ${WARNING_FLAGS}

ifneq (${FUZZER},)
COMMON_FLAGS += -DLIBFUZZER_FRONTEND
TAG := ${TAG}F
endif

ifneq (${V},1)
CC  := @${CC}
CXX := @${CXX}
endif

CFLAGS   := -std=gnu99   ${COMMON_FLAGS} ${CFLAGS}
CXXFLAGS := -std=gnu++17 ${COMMON_FLAGS} ${CXXFLAGS}
LDFLAGS  := ${COMMON_FLAGS} ${LDFLAGS}
CC       := ${CC}
CXX      := ${CXX}
LINKCC   := ${CC}
LINKCXX  := ${CXX}

ifneq (${MSYSTEM},)
BINEXT := .exe
LDLIBS += -lshlwapi
endif

BINEXT := ${TAG}${BINEXT}

SRC  = src
OBJ  = src/.build${TAG}

DIMG_OBJ = ${OBJ}/dimgutil

MODULEDIAG_EXE  := moddiag${BINEXT}
MODULEDIAG_OBJS := \
  ${OBJ}/modutil.o \
  ${OBJ}/encode.o \
  ${OBJ}/error.o \
  ${OBJ}/Config.o \
  ${OBJ}/LZW.o \
  ${OBJ}/mod_load.o \
  ${OBJ}/s3m_load.o \
  ${OBJ}/xm_load.o \
  ${OBJ}/it_load.o \
  ${OBJ}/669_load.o \
  ${OBJ}/amf_load.o \
  ${OBJ}/asy_load.o \
  ${OBJ}/coco_load.o \
  ${OBJ}/dbm_load.o \
  ${OBJ}/dsm_load.o \
  ${OBJ}/dtm_load.o \
  ${OBJ}/dtt_load.o \
  ${OBJ}/far_load.o \
  ${OBJ}/gdm_load.o \
  ${OBJ}/med_load.o \
  ${OBJ}/mtm_load.o \
  ${OBJ}/musx_load.o \
  ${OBJ}/okt_load.o \
  ${OBJ}/ps16_load.o \
  ${OBJ}/psm_load.o \
  ${OBJ}/stm_load.o \
  ${OBJ}/sym_load.o \
  ${OBJ}/ult_load.o \
  ${OBJ}/xmf_load.o \

MODULEUNPACK_EXE  := modunpack${BINEXT}
MODULEUNPACK_OBJS := \
  ${DIMG_OBJ}/dimgutil.o \
  ${DIMG_OBJ}/DiskImage.o \
  ${DIMG_OBJ}/FileIO.o \
  ${DIMG_OBJ}/FileInfo.o \
  ${DIMG_OBJ}/ADFS.o \
  ${DIMG_OBJ}/ArcFS.o \
  ${DIMG_OBJ}/FAT.o \
  ${DIMG_OBJ}/LZX.o \
  ${DIMG_OBJ}/SparkFS.o \
  ${DIMG_OBJ}/crc32.o \
  ${DIMG_OBJ}/arc_unpack.o \
  ${DIMG_OBJ}/lzx_unpack.o \
  ${OBJ}/Config.o \

DSYMGEN_EXE  := dsymgen${BINEXT}
DSYMGEN_OBJS := \
  ${OBJ}/dsymgen.o

WAV2AVR_EXE  := wav2avr${BINEXT}
WAV2AVR_OBJS := \
  ${OBJ}/wav2avr.o \
  ${OBJ}/error.o \
  ${OBJ}/Config.o \

IFFDUMP_EXE  := iffdump${BINEXT}
IFFDUMP_OBJS := \
  ${OBJ}/iffdump.o \
  ${OBJ}/error.o \
  ${OBJ}/Config.o \

UNARC_EXE    := unarc${BINEXT}
UNARC_OBJS   := \
  ${DIMG_OBJ}/arc_arc.o \
  ${DIMG_OBJ}/arc_unpack.o \
  ${DIMG_OBJ}/crc32.o \

UNARCFS_EXE  := unarcfs${BINEXT}
UNARCFS_OBJS := \
  ${DIMG_OBJ}/arc_arcfs.o \
  ${DIMG_OBJ}/arc_unpack.o \
  ${DIMG_OBJ}/crc32.o \

UNICE_EXE    := unice${BINEXT}
UNICE_OBJS   := \
  ${DIMG_OBJ}/ice_ice.o \
  ${DIMG_OBJ}/ice_unpack.o \

UNLZX_EXE    := unlzx${BINEXT}
UNLZX_OBJS   := \
  ${DIMG_OBJ}/lzx_lzx.o \
  ${DIMG_OBJ}/lzx_unpack.o \
  ${DIMG_OBJ}/crc32.o \

-include ${MODULEDIAG_OBJS:.o=.d}
${MODULEDIAG_EXE}: ${MODULEDIAG_OBJS}

-include ${MODULEUNPACK_OBJS:.o=.d}
${MODULEUNPACK_EXE}: ${MODULEUNPACK_OBJS}
${MODULEUNPACK_OBJS}: | ${MODULEUNPACK_OBJ}

-include ${DSYMGEN_OBJS:.o=.d}
${DSYMGEN_EXE}: ${DSYMGEN_OBJS}

-include ${WAV2AVR_OBJS:.o=.d}
${WAV2AVR_EXE}: ${WAV2AVR_OBJS}

-include ${IFFDUMP_OBJS:.o=.d}
${IFFDUMP_EXE}: ${IFFDUMP_OBJS}

-include ${UNARC_OBJS:.o=.d}
${UNARC_EXE}: ${UNARC_OBJS}
${UNARC_OBJS}: | ${DIMG_OBJ}

-include ${UNARCFS_OBJS:.o=.d}
${UNARCFS_EXE}: ${UNARCFS_OBJS}
${UNARCFS_OBJS}: | ${DIMG_OBJ}

-include ${UNICE_OBJS:.o=.d}
${UNICE_EXE}: ${UNICE_OBJS}
${UNICE_OBJS}: | ${DIMG_OBJ}

-include ${UNLZX_OBJS:.o=.d}
${UNLZX_EXE}: ${UNLZX_OBJS}
${UNLZX_OBJS}: | ${DIMG_OBJ}

ALL_EXES := \
  ${MODULEDIAG_EXE}  \
  ${MODULEUNPACK_EXE} \
  ${DSYMGEN_EXE} \
  ${WAV2AVR_EXE} \
  ${IFFDUMP_EXE} \
  ${UNARC_EXE} \
  ${UNARCFS_EXE} \
  ${UNICE_EXE} \
  ${UNLZX_EXE} \

all: ${ALL_EXES}

${OBJ} ${OBJ}/dimgutil:
	$(if ${V},,@echo " MKDIR   " $@)
	@mkdir -p "$@"

${OBJ}/%.o: ${SRC}/%.c | ${OBJ}
	$(if ${V},,@echo " CC      " $<)
	${CC} -MD ${CFLAGS} -c $< -o $@

${OBJ}/%.o: ${SRC}/%.cpp | ${OBJ}
	$(if ${V},,@echo " CXX     " $<)
	${CXX} -MD ${CXXFLAGS} -c $< -o $@

${MODULEDIAG_EXE}:
	$(if ${V},,@echo " LINK    " $@)
	${LINKCXX} ${LDFLAGS} -o $@ ${MODULEDIAG_OBJS} ${LDLIBS}

${MODULEUNPACK_EXE}:
	$(if ${V},,@echo " LINK    " $@)
	${LINKCXX} ${LDFLAGS} -o $@ ${MODULEUNPACK_OBJS} ${LDLIBS}

${DSYMGEN_EXE}:
	$(if ${V},,@echo " LINK    " $@)
	${LINKCXX} ${LDFLAGS} -o $@ ${DSYMGEN_OBJS} ${LDLIBS}

${WAV2AVR_EXE}:
	$(if ${V},,@echo " LINK    " $@)
	${LINKCXX} ${LDFLAGS} -o $@ ${WAV2AVR_OBJS} ${LDLIBS}

${IFFDUMP_EXE}:
	$(if ${V},,@echo " LINK    " $@)
	${LINKCXX} ${LDFLAGS} -o $@ ${IFFDUMP_OBJS} ${LDLIBS}

${UNARC_EXE}:
	$(if ${V},,@echo " LINK    " $@)
	${LINKCC} ${LDFLAGS} -o $@ ${UNARC_OBJS} ${LDLIBS}

${UNARCFS_EXE}:
	$(if ${V},,@echo " LINK    " $@)
	${LINKCC} ${LDFLAGS} -o $@ ${UNARCFS_OBJS} ${LDLIBS}

${UNICE_EXE}:
	$(if ${V},,@echo " LINK    " $@)
	${LINKCC} ${LDFLAGS} -o $@ ${UNICE_OBJS} ${LDLIBS}

${UNLZX_EXE}:
	$(if ${V},,@echo " LINK    " $@)
	${LINKCC} ${LDFLAGS} -o $@ ${UNLZX_OBJS} ${LDLIBS}

clean:
	rm -rf src/.build src/.build_san*/
	rm -f moddiag moddiag.exe moddiag_san*
	rm -f modunpack modunpack.exe modunpack_san*
	rm -f modutil modutil.exe modutil_san*
	rm -f dimgutil dimgutil.exe dimgutil_san*
	rm -f dsymgen dsymgen.exe dsymgen_san*
	rm -rf wav2avr wav2avr.exe wav2avr_san*
	rm -f iffdump iffdump.exe iffdump_san*
	rm -f unarc unarc.exe unarc_san*
	rm -f unarcfs unarcfs.exe unarcfs_san*
	rm -f unice unice.exe unice_san*
	rm -f unlzx unlzx.exe unlzx_san*

#
# Build all sanitizers/fuzzers (recursive).
#
BATCH_SANITIZE := address,undefined memory
BATCH_FUZZER := address,undefined memory

batch:
	@${MAKE}
	@for s in ${BATCH_SANITIZE}; do CC=clang CXX=clang++ ${MAKE} SANITIZE=$$s; done
	@for f in ${BATCH_FUZZER}; do CC=clang CXX=clang++ ${MAKE} FUZZER=$$f; done
