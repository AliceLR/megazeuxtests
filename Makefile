.PHONY: all
all:

CFLAGS += -O3 -g -Wall -Wextra -pedantic

ifneq (${MSYSTEM},)
BINEXT := .exe
endif

SRC  = src
OBJ  = src/.build

FAR_OBJS := ${OBJ}/farutil.o
FAR_EXE  := farutil${BINEXT}

all: ${FAR_EXE}

${OBJ}:
	mkdir -p ${OBJ}

${OBJ}/%.o: ${SRC}/%.c | ${OBJ}
	${CC} -MD ${CFLAGS} -c $< -o $@

${FAR_EXE}: ${FAR_OBJS}
	${CC} ${LDFLAGS} -o $@ ${FAR_OBJS} ${LDLIBS}
