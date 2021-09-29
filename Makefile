.PHONY: all clean
all:

CFLAGS   := -O3 -g -Wall -Wextra -pedantic -Wno-unused-parameter ${CFLAGS}
CXXFLAGS := -O3 -g -Wall -Wextra -pedantic -Wno-unused-parameter ${CXXFLAGS}
CXX      := @${CXX}
LINKCXX  := ${CXX}

ifneq (${MSYSTEM},)
BINEXT := .exe
endif

SRC  = src
OBJ  = src/.build

MODUTIL_EXE  := modutil${BINEXT}
MODUTIL_OBJS := \
  ${OBJ}/modutil.o \
  ${OBJ}/error.o \
  ${OBJ}/Config.o \
  ${OBJ}/mod_load.o \
  ${OBJ}/xm_load.o \
  ${OBJ}/it_load.o \
  ${OBJ}/669_load.o \
  ${OBJ}/amf_load.o \
  ${OBJ}/dbm_load.o \
  ${OBJ}/dsm_load.o \
  ${OBJ}/far_load.o \
  ${OBJ}/gdm_load.o \
  ${OBJ}/masi_load.o \
  ${OBJ}/med_load.o \
  ${OBJ}/okt_load.o \
  ${OBJ}/stm_load.o \
  ${OBJ}/ult_load.o \

IFFDUMP_EXE  := iffdump${BINEXT}
IFFDUMP_OBJS := \
  ${OBJ}/iffdump.o \
  ${OBJ}/error.o \
  ${OBJ}/Config.o \

-include ${MODUTIL_OBJS:.o=.d}
${MODUTIL_EXE}: ${MODUTIL_OBJS}

-include ${IFFDUMP_OBJS:.o=.d}
${IFFDUMP_EXE}: ${IFFDUMP_OBJS}

ALL_EXES := \
  ${MODUTIL_EXE}  \
  ${IFFDUMP_EXE} \

all: ${ALL_EXES}

${OBJ}:
	$(if ${V},,@echo " MKDIR   " $@)
	@mkdir -p ${OBJ}

${OBJ}/%.o: ${SRC}/%.c | ${OBJ}
	$(if ${V},,@echo " CC      " $<)
	${CC} -MD ${CFLAGS} -c $< -o $@

${OBJ}/%.o: ${SRC}/%.cpp | ${OBJ}
	$(if ${V},,@echo " CXX     " $<)
	${CXX} -MD ${CXXFLAGS} -c $< -o $@

${MODUTIL_EXE}:
	$(if ${V},,@echo " LINK    " $@)
	${LINKCXX} ${LDFLAGS} -o $@ ${MODUTIL_OBJS} ${LDLIBS}

${IFFDUMP_EXE}:
	$(if ${V},,@echo " LINK    " $@)
	${LINKCXX} ${LDFLAGS} -o $@ ${IFFDUMP_OBJS} ${LDLIBS}

clean:
	rm -rf ${OBJ}
	rm -f ${ALL_EXES}
