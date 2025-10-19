obj-m := nxp_simtemp.o

KDIR ?= $(KERNEL_SRC)
KDIR ?= $(KERNEL_PATH)
KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean