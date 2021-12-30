.PHONY: all clean
all:

CFLAGS   := -std=gnu99   -O3 -g -Wall -Wextra -pedantic -Wno-unused-parameter ${CFLAGS}
CXXFLAGS := -std=gnu++17 -O3 -g -Wall -Wextra -pedantic -Wno-unused-parameter ${CXXFLAGS}
CC       := @${CC}
CXX      := @${CXX}
LINKCC   := ${CC}
LINKCXX  := ${CXX}

ifneq (${MSYSTEM},)
BINEXT := .exe
LDLIBS += -lshlwapi
endif

SRC  = src
OBJ  = src/.build

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
	rm -rf ${OBJ}
	rm -f ${ALL_EXES}
