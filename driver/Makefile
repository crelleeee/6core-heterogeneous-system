# Makefile for 6-core heterogeneous SoC driver
obj-m += hetero_soc.o

# Kernel build directory
KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Default target
all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	rm -f *.o *~ core .depend .*.cmd *.mod *.mod.c
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions

install:
	sudo insmod hetero_soc.ko

remove:
	sudo rmmod hetero_soc

# 快速重新加载（先卸载再加载）
reload: remove all install

# 编译并测试
test: all
	@echo "=== Loading driver ==="
	-sudo rmmod hetero_soc 2>/dev/null || true
	sudo insmod hetero_soc.ko
	@echo "=== Checking device ==="
	ls -l /dev/hetero_soc
	@echo "=== Testing read ==="
	sudo cat /dev/hetero_soc
	@echo "=== Module info ==="
	lsmod | grep hetero_soc
	@echo "=== Recent logs ==="
	dmesg | tail -10

.PHONY: all clean install remove reload test
