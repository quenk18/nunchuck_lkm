obj-m+=nunchuck_driver.o
PWD=$(CURDIR)
all:
	${MAKE} -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	rm *.order *.symvers *.ko *.mod *.mod.c *.o

