.PHONY: all clean
all:

CFLAGS   += -O3 -g -Wall -Wextra -pedantic -Wno-unused-parameter
CXXFLAGS += -O3 -g -Wall -Wextra -pedantic -Wno-unused-parameter
LINKCXX  := ${CXX}

ifneq (${MSYSTEM},)
BINEXT := .exe
endif

SRC  = src
OBJ  = src/.build

_669_OBJS:= ${OBJ}/669util.o
_669_EXE := 669util${BINEXT}
-include ${_669_OBJS:.o=.d}
${_669_EXE}: ${_669_OBJS}

DBM_OBJS := ${OBJ}/dbmutil.o ${OBJ}/IFF.o
DBM_EXE  := dbmutil${BINEXT}
-include ${DBM_OBJS:.o=.d}
${DBM_EXE}: ${DBM_OBJS}

FAR_OBJS := ${OBJ}/farutil.o
FAR_EXE  := farutil${BINEXT}
-include ${FAR_OBJS:.o=.d}
${FAR_EXE}: ${FAR_OBJS}

GDM_OBJS := ${OBJ}/gdmutil.o
GDM_EXE  := gdmutil${BINEXT}
-include ${GDM_OBJS:.o=.d}
${GDM_EXE}: ${GDM_OBJS}

MASI_OBJS:= ${OBJ}/masiutil.o ${OBJ}/IFF.o
MASI_EXE := masiutil${BINEXT}
-include ${MASI_OBJS:.o=.d}
${MASI_EXE}: ${MASI_OBJS}

MED_OBJS := ${OBJ}/medutil.o
MED_EXE  := medutil${BINEXT}
-include ${MED_OBJS:.o=.d}
${MED_EXE}: ${MED_OBJS}

MOD_OBJS := ${OBJ}/modutil.o
MOD_EXE  := modutil${BINEXT}
-include ${MOD_OBJS:.o=.d}
${MOD_EXE}: ${MOD_OBJS}

OKT_OBJS := ${OBJ}/oktutil.o ${OBJ}/IFF.o
OKT_EXE  := oktutil${BINEXT}
-include ${OKT_OBJS:.o=.d}
${OKT_EXE}: ${OKT_OBJS}

STM_OBJS := ${OBJ}/stmutil.o
STM_EXE  := stmutil${BINEXT}
-include ${STM_OBJS:.o=.d}
${STM_EXE}: ${STM_OBJS}

XM_OBJS  := ${OBJ}/xmutil.o
XM_EXE   := xmutil${BINEXT}
-include ${XM_OBJS:.o=.d}
${XM_EXE}: ${XM_OBJS}

ALL_EXES := \
  ${_669_EXE} \
  ${DBM_EXE}  \
  ${FAR_EXE}  \
  ${GDM_EXE}  \
  ${MASI_EXE} \
  ${MED_EXE}  \
  ${MOD_EXE}  \
  ${OKT_EXE}  \
  ${STM_EXE}  \
  ${XM_EXE}   \

all: ${ALL_EXES}

${OBJ}:
	mkdir -p ${OBJ}

${OBJ}/%.o: ${SRC}/%.c | ${OBJ}
	${CC} -MD ${CFLAGS} -c $< -o $@

${OBJ}/%.o: ${SRC}/%.cpp | ${OBJ}
	${CXX} -MD ${CXXFLAGS} -c $< -o $@

${ALL_EXES}:
	${LINKCXX} ${LDFLAGS} -o $@ $^ ${LDLIBS}

clean:
	rm -rf ${OBJ}
	rm -f ${ALL_EXES}
