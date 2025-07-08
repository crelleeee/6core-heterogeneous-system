obj-m += hetero_mmap.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

test: all
	-sudo rmmod hetero_mmap 2>/dev/null || true
	sudo insmod hetero_mmap.ko
	@echo "Driver loaded, check dmesg"

.PHONY: all clean test
