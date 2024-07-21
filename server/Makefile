SRC=$(wildcard *.c)
DEP=$(SRC:.c=.o)
CFLAGS ?= -g -Wall -Werror -I.
LDFLAGS ?= -lpthread -lrt
TARGET ?= aesdsocket

ifeq ($(CROSS_COMPILE),)
	CC?=gcc
else
	CC?=$(addprefix $(CROSS_COMPILE), gcc)
endif


all: $(DEP)  $(TARGET)

%: %.o
	$(CC) $(CFLAGS) -o $@ $(DEP) $(LDFLAGS) -Wl,-Map=mapfile.map

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $< -o $@ $(LDFLAGS)

clean:
	rm -rf *.o aesdsocket *.map
