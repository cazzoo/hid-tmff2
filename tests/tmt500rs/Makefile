obj-m := test-tmt500rs.o

ccflags-y := -I$(src)/../../src -fno-common
KBUILD_EXTRA_SYMBOLS := $(src)/../../Module.symvers

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	# First build the main module
	$(MAKE) -C $(KDIR) M=$(PWD)/../../ modules
	# Then build the test module
	$(MAKE) -C $(KDIR) M=$(PWD) modules KBUILD_EXTRA_SYMBOLS=$(PWD)/../../Module.symvers

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(MAKE) -C $(KDIR) M=$(PWD)/../../ clean

# Add dependencies
test-tmt500rs.o: $(PWD)/../../hid-tmff-new.ko
KBUILD_MODPOST_WARN := y
KBUILD_MODPOST_NOFINAL := y
CFLAGS_test-tmt500rs.o := -fno-common 