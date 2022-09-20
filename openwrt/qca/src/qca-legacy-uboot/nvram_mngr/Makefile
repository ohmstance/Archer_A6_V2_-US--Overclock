# Makefile for nvrammngr lib.
# This lib is needed for tplink firmware recovery function support.
#

include $(TOPDIR)/config.mk

LIB	= libnvrammngr.a

OBJS =  nm_api.o nm_fwup.o nm_lib.o sysProductInfo.o fwrecovery_api.o fwrecovery_gpio.o md5.o

ifneq ($(CONFIG_FIRMWARE_NOCHECK),y)
OBJS += rsaVerify.o bigNumber.o shaAndSha512.o
endif

#CFLAGS += -DCONFIG_LZMA=1

$(LIB):	.depend $(OBJS)
	$(AR) crv $@ $(OBJS)


.depend:	Makefile $(OBJS:.o=.c)
		$(CC) -M $(CFLAGS) $(OBJS:.o=.c) > $@

sinclude .depend
