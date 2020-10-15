.PHONY: all clean
all:

CFLAGS += -O3 -g -Wall -Wextra -pedantic -Wno-unused-parameter

ifneq (${MSYSTEM},)
BINEXT := .exe
endif

SRC  = src
OBJ  = src/.build

FAR_OBJS := ${OBJ}/farutil.o
FAR_EXE  := farutil${BINEXT}

WOW_OBJS := ${OBJ}/wowutil.o
WOW_EXE  := wowutil${BINEXT}

all: ${FAR_EXE} ${WOW_EXE}

${OBJ}:
	mkdir -p ${OBJ}

${OBJ}/%.o: ${SRC}/%.c | ${OBJ}
	${CC} -MD ${CFLAGS} -c $< -o $@

${OBJ}/%.o: ${SRC}/%.cpp | ${OBJ}
	${CXX} -MD ${CFLAGS} -c $< -o $@

${FAR_EXE}: ${FAR_OBJS}
	${CC} ${LDFLAGS} -o $@ ${FAR_OBJS} ${LDLIBS}

${WOW_EXE}: ${WOW_OBJS}
	${CC} ${LDFLAGS} -o $@ ${WOW_OBJS} ${LDLIBS}

clean:
	rm -rf ${OBJ}
	rm -f ${FAR_EXE}
	rm -f ${WOW_EXE}
