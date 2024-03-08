
SRC=$(wildcard *.c)
DEP=$(SRC:.c=.o)
FLAGS= -g -Wall -Werror -I.

ifeq ($(CROSS_COMPILE),)
	CC=gcc
else
	CC=$(addprefix $(CROSS_COMPILE), gcc)
endif


all: $(DEP)  aesdsocket 

%: %.o
	$(CC) $(FLAG) -o $@ $(DEP) -lpthread -Wl,-Map=mapfile.map


%.o: %.c %.h
	$(CC) $(FLAG) -c $< -o $@ -lpthread


clean:
	rm -rf *.o aesdsocket *.map

