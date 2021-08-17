ccflags-y += -I$(src) 
ccflags-y += -Wno-unused-but-set-variable -Wno-unused-variable
ccflags-y += -Wno-declaration-after-statement

obj-m += ccnvmetest.o

ccnvmetest-m += ccnvme_test.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) KBUILD_EXTRA_SYMBOLS=$(PWD)/../ccnvme/Module.symvers modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
