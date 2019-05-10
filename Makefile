ccflags-y :=  -Wall -Wextra -Werror -Wno-unused-parameter -std=gnu89
CFLAGS_nswitch.o := -Wall -Wextra -Werror -Wno-unused-parameter -std=gnu89
obj-m += nswitch.o
nswitch-objs := simplejc.o hid-nswitch.o nswitch-hw-init.o
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

re: clean all
