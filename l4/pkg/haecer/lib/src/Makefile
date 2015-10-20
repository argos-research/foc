PKGDIR?= ../..
L4DIR ?= $(PKGDIR)/../..

# the name of your library
TARGET	= lib$(PKGNAME).a

# list your .c or .cc files here
#SRC_C	=
SRC_CC  = main.cc eThread.cc

#uncomment the following 2 lines to use ferret
#DEFINES += -DUSE_FERRET
#REQUIRES_LIBS += ferret-producer ferret-common
REQUIRES_LIBS += libdebug libstdc++

include $(L4DIR)/mk/lib.mk
