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

DBM_OBJS := ${OBJ}/dbmutil.o ${OBJ}/IFF.o
DBM_EXE  := dbmutil${BINEXT}
-include ${DBM_OBJS:.o=.d}

FAR_OBJS := ${OBJ}/farutil.o
FAR_EXE  := farutil${BINEXT}
-include ${FAR_OBJS:.o=.d}

GDM_OBJS := ${OBJ}/gdmutil.o
GDM_EXE  := gdmutil${BINEXT}
-include ${GDM_OBJS:.o=.d}

MED_OBJS := ${OBJ}/medutil.o
MED_EXE  := medutil${BINEXT}
-include ${MED_OBJS:.o=.d}

MOD_OBJS := ${OBJ}/modutil.o
MOD_EXE  := modutil${BINEXT}
-include ${MOD_OBJS:.o=.d}

STM_OBJS := ${OBJ}/stmutil.o
STM_EXE  := stmutil${BINEXT}
-include ${STM_OBJS:.o=.d}

XM_OBJS  := ${OBJ}/xmutil.o
XM_EXE   := xmutil${BINEXT}
-include ${XM_OBJS:.o=.d}

all: ${_669_EXE} ${DBM_EXE} ${FAR_EXE} ${GDM_EXE} ${MED_EXE} ${MOD_EXE} ${STM_EXE} ${XM_EXE}

${OBJ}:
	mkdir -p ${OBJ}

${OBJ}/%.o: ${SRC}/%.c | ${OBJ}
	${CC} -MD ${CFLAGS} -c $< -o $@

${OBJ}/%.o: ${SRC}/%.cpp | ${OBJ}
	${CXX} -MD ${CXXFLAGS} -c $< -o $@

${_669_EXE}: ${_669_OBJS}
	${LINKCXX} ${LDFLAGS} -o $@ $^ ${LDLIBS}

${DBM_EXE}: ${DBM_OBJS}
	${LINKCXX} ${LDFLAGS} -o $@ $^ ${LDLIBS}

${FAR_EXE}: ${FAR_OBJS}
	${LINKCXX} ${LDFLAGS} -o $@ $^ ${LDLIBS}

${GDM_EXE}: ${GDM_OBJS}
	${LINKCXX} ${LDFLAGS} -o $@ $^ ${LDLIBS}

${MED_EXE}: ${MED_OBJS}
	${LINKCXX} ${LDFLAGS} -o $@ $^ ${LDLIBS}

${MOD_EXE}: ${MOD_OBJS}
	${LINKCXX} ${LDFLAGS} -o $@ $^ ${LDLIBS}

${STM_EXE}: ${STM_OBJS}
	${LINKCXX} ${LDFLAGS} -o $@ $^ ${LDLIBS}

${XM_EXE}: ${XM_OBJS}
	${LINKCXX} ${LDFLAGS} -o $@ $^ ${LDLIBS}

clean:
	rm -rf ${OBJ}
	rm -f ${_669_EXE}
	rm -f ${DBM_EXE}
	rm -f ${FAR_EXE}
	rm -f ${GDM_EXE}
	rm -f ${MED_EXE}
	rm -f ${MOD_EXE}
	rm -f ${STM_EXE}
	rm -f ${XM_EXE}
