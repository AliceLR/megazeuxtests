.PHONY: all clean
all:

COMMON_FLAGS := -O3 -g
WARNING_FLAGS := -Wall -Wextra -pedantic -Wno-unused-parameter
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

MODUTIL_EXE  := modutil${BINEXT}
MODUTIL_OBJS := \
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

DIMGUTIL_EXE  := dimgutil${BINEXT}
DIMGUTIL_OBJS := \
  ${DIMG_OBJ}/dimgutil.o \
  ${DIMG_OBJ}/DiskImage.o \
  ${DIMG_OBJ}/FileIO.o \
  ${DIMG_OBJ}/FileInfo.o \
  ${DIMG_OBJ}/ADFS.o \
  ${DIMG_OBJ}/ArcFS.o \
  ${DIMG_OBJ}/FAT.o \
  ${DIMG_OBJ}/SparkFS.o \
  ${DIMG_OBJ}/arc_crc16.o \
  ${DIMG_OBJ}/arc_unpack.o \
  ${OBJ}/Config.o \

IFFDUMP_EXE  := iffdump${BINEXT}
IFFDUMP_OBJS := \
  ${OBJ}/iffdump.o \
  ${OBJ}/error.o \
  ${OBJ}/Config.o \

UNARC_EXE    := unarc${BINEXT}
UNARC_OBJS   := \
  ${DIMG_OBJ}/arc_arc.o \
  ${DIMG_OBJ}/arc_crc16.o \
  ${DIMG_OBJ}/arc_unpack.o \

UNARCFS_EXE  := unarcfs${BINEXT}
UNARCFS_OBJS := \
  ${DIMG_OBJ}/arc_arcfs.o \
  ${DIMG_OBJ}/arc_crc16.o \
  ${DIMG_OBJ}/arc_unpack.o \

-include ${MODUTIL_OBJS:.o=.d}
${MODUTIL_EXE}: ${MODUTIL_OBJS}

-include ${DIMGUTIL_OBJS:.o=.d}
${DIMGUTIL_EXE}: ${DIMGUTIL_OBJS}
${DIMGUTIL_OBJS}: | ${DIMG_OBJ}

-include ${IFFDUMP_OBJS:.o=.d}
${IFFDUMP_EXE}: ${IFFDUMP_OBJS}

-include ${UNARC_OBJS:.o=.d}
${UNARC_EXE}: ${UNARC_OBJS}
${UNARC_OBJS}: | ${DIMG_OBJ}

-include ${UNARCFS_OBJS:.o=.d}
${UNARCFS_EXE}: ${UNARCFS_OBJS}
${UNARCFS_OBJS}: | ${DIMG_OBJ}

ALL_EXES := \
  ${MODUTIL_EXE}  \
  ${DIMGUTIL_EXE} \
  ${IFFDUMP_EXE} \
  ${UNARC_EXE} \
  ${UNARCFS_EXE} \

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

${MODUTIL_EXE}:
	$(if ${V},,@echo " LINK    " $@)
	${LINKCXX} ${LDFLAGS} -o $@ ${MODUTIL_OBJS} ${LDLIBS}

${DIMGUTIL_EXE}:
	$(if ${V},,@echo " LINK    " $@)
	${LINKCXX} ${LDFLAGS} -o $@ ${DIMGUTIL_OBJS} ${LDLIBS}

${IFFDUMP_EXE}:
	$(if ${V},,@echo " LINK    " $@)
	${LINKCXX} ${LDFLAGS} -o $@ ${IFFDUMP_OBJS} ${LDLIBS}

${UNARC_EXE}:
	$(if ${V},,@echo " LINK    " $@)
	${LINKCC} ${LDFLAGS} -o $@ ${UNARC_OBJS} ${LDLIBS}

${UNARCFS_EXE}:
	$(if ${V},,@echo " LINK    " $@)
	${LINKCC} ${LDFLAGS} -o $@ ${UNARCFS_OBJS} ${LDLIBS}

clean:
	rm -rf src/.build src/.build_san*/
	rm -f modutil modutil.exe modutil_san*
	rm -f dimgutil dimgutil.exe dimgutil_san*
	rm -f iffdump iffdump.exe iffdump_san*
	rm -f unarc unarc.exe unarc_san*
	rm -f unarcfs unarcfs.exe unarcfs_san*
