# change application name here (executable output name)
TARGET=main
SOURCE=main.c
OBJ=main.o

# compiler
CC=gcc
# debug
DEBUG=-g
# optimisation
OPT=-O0
# warnings
WARN=-Wall

PTHREAD=-pthread

CCFLAGS=$(DEBUG) $(OPT) $(WARN) $(PTHREAD) -pipe

GTKLIB=`pkg-config --cflags --libs gtk+-3.0` -l bcm2835

# linker
LD=gcc
LDFLAGS=$(PTHREAD) $(GTKLIB) -export-dynamic

OBJS=    main.o

all: $(OBJS)
	$(LD) -o $(TARGET) $(OBJS) $(LDFLAGS)
    
main.o: $(SOURCE)
	$(CC) -c $(CCFLAGS) $(SOURCE) $(GTKLIB) -o main.o
    
clean:
	rm -f *.o $(TARGET)
