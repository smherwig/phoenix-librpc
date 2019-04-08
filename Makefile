# == CHANGE THE SETTINGS BELOW TO SUIT YOUR ENVIRONMENT

# Compiler options
#----------------------------------------------------------
CC= gcc -std=gnu99

INCLUDES= $(HOME)/include

# the -Wno-deprecated-declarations is for OpenSSL
#CFLAGS = -Wall -Werror -I $(INCLUDES) -DRHO_TRACE -DRHO_DEBUG
CFLAGS = -Wall -Werror -I $(INCLUDES)

# Utilities
#----------------------------------------------------------
AR= ar rcu
RANLIB= ranlib
RM= rm -f
MKDIR= mkdir -p
INSTALL= install -p
INSTALL_DATA= $(INSTALL) -m 0644

# If you don't have install, you can use "cp" instead.
# 
# INSTALL= cp -p
# INSTAL_EXEC= $(INSTALL)
# INSTAL_DATA= $(INSTALL)


# Install Location
# See, also, the local target
#----------------------------------------------------------
INSTALL_TOP= /usr/local
INSTALL_INC= $(INSTALL_TOP)/include
INSTALL_LIB= $(INSTALL_TOP)/lib


# == END OF USER SETTINGS -- NO NEED TO CHANGE ANYTHING BELOW THIS LINE =======

# Headers to intsall
#----------------------------------------------------------
TO_INC= rpc.h

# Library to install
#----------------------------------------------------------
TO_LIB= librpc.a librpc-pic.a

RPC_A= librpc.a
RPC_PIC_A= librpc-pic.a

RPC_OBJS= rpc.o
RPC_PIC_OBJS= $(addsuffix .do, $(basename $(RPC_OBJS)))

%.do : %.c
	$(CC) -c $(CFLAGS) -fPIC -fvisibility=hidden $(CPPFLAGS) -o $@ $<

# Targets start here
#----------------------------------------------------------
all: $(RPC_A) $(RPC_PIC_A)

$(RPC_A): $(RPC_OBJS)
	$(AR) $@ $(RPC_OBJS)
	$(RANLIB) $@

$(RPC_PIC_A): $(RPC_PIC_OBJS)
	$(AR) $@ $(RPC_PIC_OBJS)
	$(RANLIB) $@

install:
	$(MKDIR) $(INSTALL_INC) $(INSTALL_LIB)
	$(INSTALL_DATA) $(TO_INC) $(INSTALL_INC)
	$(INSTALL_DATA) $(TO_LIB) $(INSTALL_LIB)

uninstall:
	cd $(INSTALL_INC) && $(RM) $(TO_INC)
	cd $(INSTALL_LIB) && $(RM) $(TO_LIB)

local:
	$(MAKE) install INSTALL_TOP=../install

clean:
	$(RM) $(RPC_OBJS) $(RPC_A) $(RPC_PIC_OBJS) $(RPC_PIC_A)

echo:
	@echo "CC= $(CC)"
	@echo "CFLAGS= $(CFLAGS)"
	@echo "AR= $(AR)"
	@echo "RANLIB= $(RANLIB)"
	@echo "RM= $(RM)"
	@echo "MKDIR= $(MKDIR)"
	@echo "INSTALL= $(INSTALL)"
	@echo "INSTALL_DATA= $(INSTALL_DATA)"
	@echo "TO_INC= $(TO_INC)"
	@echo "TO_LIB= $(TO_LIB)"
	@echo "INSTALL_TOP= $(INSTALL_TOP)"
	@echo "INSTALL_INC= $(INSTALL_INC)"
	@echo "INSTALL_LIB= $(INSTALL_LIB)"

# DO NOT DELETE

$(addprefix rpc.,o do): rpc.c rpc.h

.PHONY: clean echo local install uninstall
