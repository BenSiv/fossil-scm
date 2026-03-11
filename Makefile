#!/usr/bin/make
#
# This is the top-level makefile for Fossil when the build is occurring
# on a unix platform.  This works out-of-the-box on most unix platforms.
# But you are free to vary some of the definitions if desired.
#
#### The toplevel directory of the source tree.  Fossil can be built
#    in a directory that is separate from the source tree.  Just change
#    the following to point from the build directory to the src/ folder.
#
SRCDIR = ./src
TOPDIR = .
#### Upstream source files included directly in this repository.
#
SRCDIR_extsrc = ./dep/vendor/extsrc
#### In-tree tools such as code generators and translators:
#
SRCDIR_tools = ./dev/tools

#### The directory into which object code files should be written.
#    Having a "./" prefix in the value of this variable breaks our use of the
#    "makeheaders" tool when running make on the MinGW platform, apparently
#    due to some command line argument manipulation performed automatically
#    by the shell.
#
#
OBJDIR = bld

#### C Compiler and options for use in building executables that
#    will run on the platform that is doing the build.  This is used
#    to compile code-generator programs as part of the build process.
#    See TCC below for the C compiler for building the finished binary.
#
BCC = cc

#### The suffix to add to final executable file.  When cross-compiling
#    to windows, make this ".exe".  Otherwise leave it blank.
#
E = 

TCC = cc

#### Tcl shell for use in running the fossil testsuite.  If you do not
#    care about testing the end result, this can be blank.
#
TCLSH = tclsh

CFLAGS = -g -Os
CFLAGS_INCLUDE = -I. -I$(SRCDIR) -I$(SRCDIR_extsrc)
LIB =	  -lresolv -lssl -lcrypto -lz -ldl -lpthread -lm
BCCFLAGS =	 $(CFLAGS)
TCCFLAGS =	-Wall -Wdeclaration-after-statement -DFOSSIL_DYNAMIC_BUILD=1  $(CFLAGS) -DHAVE_AUTOCONFIG_H
#
# Fuzzing may be enable by appending -fsanitize=fuzzer -DFOSSIL_FUZZ
# to the TCCFLAGS variable.
# For more thorough (but also slower) investigation
#      -fsanitize=fuzzer,undefined,address
# might be more useful.

INSTALLDIR = $(DESTDIR)/usr/local/bin
USE_SYSTEM_SQLITE = 0
SQLITE3_SRC.2 = 
SQLITE3_OBJ.2 = 
SQLITE3_SHELL_SRC.2 = $(SQLITE3_SHELL_SRC.0)
SQLITE3_ORIGIN = 0
# SQLITE3_ORIGIN changes...
#  SQLITE3_SRC:
#   0=src/sqlite3.c, 1=src/sqlite3-see.c, 2=$(SQLITE3_SRC.2)
#  SQLITE3_SHELL_SRC:
#   0=src/shell.c, 1=src/shell-see.c, 2=$(SQLITE3_SHELL_SRC.2)
USE_LINENOISE = 1
USE_MMAN_H = 0
USE_SEE = 0
APPNAME = fossil
#
# APPNAME = fossil-fuzz
# may be more appropriate for fuzzing.

#### Emscripten stuff for optionally doing in-tree builds
# of any WASM components. We store precompiled WASM in the SCM, so
# this is only useful for people who actively work on WASM
# components. EMSDK_ENV refers to the "environment" script which comes
# with the Emscripten SDK package:
# https://emscripten.org/docs/getting_started/downloads.html
EMSDK_HOME = 
EMSDK_ENV = @EMSDK_ENV@
EMCC_OPT = 
EMCC_WRAPPER = $(SRCDIR_tools)/emcc.sh

# MAKE_COMPILATION_DB (yes/no) determines whether or not the
# compile_commands.json file will be generated.
MAKE_COMPILATION_DB = no

.PHONY: all tags

include $(SRCDIR)/main.mk
SQLITE_OPTIONS += 
SHELL_OPTIONS += 
# ^^^ must come after main.mk is included

distclean: clean
	-rm -f autoconfig.h config.log Makefile
	-rm -f cscope.out tags

reconfig:
	./configure

tags:
	ctags -R ./src
	

# Automatically reconfigure whenever an autosetup file or one of the
# make source files change.
#
# The "touch" is necessary to avoid a make loop due to a new upstream
# feature in autosetup (GH 0a71e3c3b7) which rewrites *.in outputs only
# if doing so will write different contents; otherwise, it leaves them
# alone so the mtime doesn't change.  This means that if you change one
# of our dependencies besides Makefile.in, we'll reconfigure but Makefile
# won't change, so this rule will remain out of date, so we'll reconfig
# but Makefile won't change, so we'll reconfig but... endlessly.
#
# This is also why we repeat the reconfig target's command here instead
# of delegating to it with "$(MAKE) reconfig": having children running
# around interfering makes this failure mode even worse.
Makefile: ./Makefile.in $(SRCDIR)/main.mk /home/bensiv/Projects/fossil-scm/dep/vendor/autosetup/autosetup /home/bensiv/Projects/fossil-scm/dep/vendor/autosetup/local.tcl /home/bensiv/Projects/fossil-scm/auto.def /home/bensiv/Projects/fossil-scm/dep/vendor/autosetup/system.tcl /home/bensiv/Projects/fossil-scm/dep/vendor/autosetup/cc.tcl /home/bensiv/Projects/fossil-scm/dep/vendor/autosetup/cc-lib.tcl
	./configure
	touch /home/bensiv/Projects/fossil-scm/Makefile

# Container stuff
SRCTB := src-.tar.gz
IMGVER := fossil:
CNTVER := fossil-
CENGINE := docker
container:
	$(CENGINE) image inspect $(IMGVER) > /dev/null 2>&1 || \
		$(MAKE) container-image
	$(CENGINE) container inspect $(CNTVER) > /dev/null 2>&1 || \
		$(CENGINE) create \
			--name $(CNTVER) \
			--cap-drop AUDIT_WRITE \
			--cap-drop CHOWN \
			--cap-drop FSETID \
			--cap-drop KILL \
			--cap-drop MKNOD \
			--cap-drop NET_BIND_SERVICE \
			--cap-drop NET_RAW \
			--cap-drop SETFCAP \
			--cap-drop SETPCAP \
			--publish 8080:8080 \
			$(DCFLAGS) $(IMGVER) $(DCCMD)

container-clean:
	-$(CENGINE) container kill $(CNTVER)
	-$(CENGINE) container rm   $(CNTVER)
	-$(CENGINE) image     rm   $(IMGVER)

container-image:
	$(APPNAME) tarball --name src  $(SRCTB)
	$(CENGINE) buildx build \
		--load \
		--tag $(IMGVER) \
		--build-arg FSLURL=$(SRCTB) \
		$(DBFLAGS) .
	rm -f $(SRCTB)

container-run container-start: container
	$(CENGINE) start $(DSFLAGS) $(CNTVER)
	@sleep 1   # decrease likelihood of logging race condition
	$(CENGINE) container logs $(CNTVER)

container-stop:
	$(CENGINE) stop $(CNTVER)

container-version:
	@echo $(CNTVER)
