CC ?= $(CROSS_COMPILE)gcc
CFLAGS := -Wall -Werror
 
USE_AESD_CHAR_DEVICE ?= 1
ifeq ($(USE_AESD_CHAR_DEVICE),1)
	CFLAGS += -DUSE_AESD_CHAR_DEVICE
endif

all: aesdsocket
 
default: all
 
aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) $(LDFLAGS) aesdsocket.c -o aesdsocket
 
clean:
	rm -f aesdsocket
