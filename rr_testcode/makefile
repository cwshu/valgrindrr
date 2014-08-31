CC = gcc
CFLAGS = -g
SIMPLE_C_TESTCODE = std_input file_IO localtime_test

all: ${SIMPLE_C_TESTCODE}

clean: 
	rm -f ${SIMPLE_C_TESTCODE}

%: %.c
	${CC} ${CFLAGS} $< -o $@
