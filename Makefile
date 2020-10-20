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

FAR_OBJS := ${OBJ}/farutil.o
FAR_EXE  := farutil${BINEXT}
-include ${FAR_OBJS:.o=.d}

GDM_OBJS := ${OBJ}/gdmutil.o
GDM_EXE  := gdmutil${BINEXT}
-include ${GDM_OBJS:.o=.d}

WOW_OBJS := ${OBJ}/wowutil.o
WOW_EXE  := wowutil${BINEXT}
-include ${WOW_OBJS:.o=.d}

XM_OBJS  := ${OBJ}/xmutil.o
XM_EXE   := xmutil${BINEXT}
-include ${XM_OBJS:.o=.d}

all: ${_669_EXE} ${FAR_EXE} ${GDM_EXE} ${WOW_EXE} ${XM_EXE}

${OBJ}:
	mkdir -p ${OBJ}

${OBJ}/%.o: ${SRC}/%.c | ${OBJ}
	${CC} -MD ${CFLAGS} -c $< -o $@

${OBJ}/%.o: ${SRC}/%.cpp | ${OBJ}
	${CXX} -MD ${CXXFLAGS} -c $< -o $@

${_669_EXE}: ${_669_OBJS}
	${LINKCXX} ${LDFLAGS} -o $@ $^ ${LDLIBS}

${FAR_EXE}: ${FAR_OBJS}
	${LINKCXX} ${LDFLAGS} -o $@ $^ ${LDLIBS}

${GDM_EXE}: ${GDM_OBJS}
	${LINKCXX} ${LDFLAGS} -o $@ $^ ${LDLIBS}

${WOW_EXE}: ${WOW_OBJS}
	${LINKCXX} ${LDFLAGS} -o $@ $^ ${LDLIBS}

${XM_EXE}: ${XM_OBJS}
	${LINKCXX} ${LDFLAGS} -o $@ $^ ${LDLIBS}

clean:
	rm -rf ${OBJ}
	rm -f ${_669_EXE}
	rm -f ${FAR_EXE}
	rm -f ${GDM_EXE}
	rm -f ${WOW_EXE}
	rm -f ${XM_EXE}
