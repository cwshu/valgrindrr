CC = gcc
CFLAGS = -g
TESTCODE_SRC = $(wildcard *.c)
TESTCODE = $(patsubst %.c,%,$(TESTCODE_SRC))

all: $(TESTCODE)

clean: 
	rm -f $(TESTCODE)

$(TESTCODE): %: %.c
	${CC} ${CFLAGS} $< -o $@

.PNOHY: all clean
