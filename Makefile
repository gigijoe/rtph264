C=gcc
AR=ar

CFLAGS=-Wall -O2 -funroll-loops -msse2 -I/usr/local/include
LDFLAGS=-L/usr/local/lib -lavformat -lavcodec -lavutil -lm -lz
LIBS=rtph264.o mp4mux.o

%.o : %.cc
	$(CC) -c $(CFLAGS) $< -o $@

all : rtph264

rtph264 : ${LIBS} main.o
	${CC} -o $@ ${LIBS} main.o ${LDFLAGS}

clean :
	rm -rf ./*.o
	rm -rf rtph264
