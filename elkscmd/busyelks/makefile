BASEDIR	= ..
include $(BASEDIR)/Make.defs
include $(BASEDIR)/Make.rules

NAME	= busyelks

OUT	= $(NAME)
CFLAGS	+= -I.
OBJS	+= main.o lib/bsearch.o

include config.mk

ifdef CMD_basename
	CFLAGS	+= -DCMD_basename
	OBJS	+= cmd/basename.o
	NEED_stripslash	= yes
endif
ifdef CMD_cal
	CFLAGS	+= -DCMD_cal
	OBJS	+= cmd/cal.o
endif
ifdef CMD_cat
	CFLAGS	+= -DCMD_cat
	OBJS	+= cmd/cat.o
endif
ifdef CMD_chgrp
	CFLAGS	+= -DCMD_chgrp
	OBJS	+= cmd/chgrp.o
endif
ifdef CMD_chmod
	CFLAGS	+= -DCMD_chmod
	OBJS	+= cmd/chmod.o
endif
ifdef CMD_chown
	CFLAGS	+= -DCMD_chown
	OBJS	+= cmd/chown.o
endif
ifdef CMD_cksum
	CFLAGS	+= -DCMD_cksum
	OBJS	+= cmd/cksum.o
endif
ifdef CMD_cmp
	CFLAGS	+= -DCMD_cmp
	OBJS	+= cmd/cmp.o
endif
ifdef CMD_cp
	CFLAGS	+= -DCMD_cp
	OBJS	+= cmd/cp.o
	NEED_buildname	= yes
	NEED_copyfile	= yes
	NEED_isadir	= yes
endif
ifdef CMD_cut
	CFLAGS	+= -DCMD_cut
	OBJS	+= cmd/cut.o
endif
ifdef CMD_date
	CFLAGS	+= -DCMD_date
	OBJS	+= cmd/date.o
	NEED_utc_mktime	= yes
endif
ifdef CMD_dd
	CFLAGS	+= -DCMD_dd
	OBJS	+= cmd/dd.o
endif
ifdef CMD_diff
	CFLAGS	+= -DCMD_diff
	OBJS	+= cmd/diff.o
endif
ifdef CMD_dirname
	CFLAGS	+= -DCMD_dirname
	OBJS	+= cmd/dirname.o
	NEED_stripslash	= yes
endif
ifdef CMD_du
	CFLAGS	+= -DCMD_du
	OBJS	+= cmd/du.o
endif
ifdef CMD_echo
	CFLAGS	+= -DCMD_echo
	OBJS	+= cmd/echo.o
endif
ifdef CMD_ed
	CFLAGS	+= -DCMD_ed
	OBJS	+= cmd/ed.o
endif
ifdef CMD_false
	CFLAGS	+= -DCMD_false
endif
ifdef CMD_fdisk
	CFLAGS	+= -DCMD_fdisk
	OBJS	+= cmd/fdisk.o
endif
ifdef CMD_find
	CFLAGS	+= -DCMD_find
	OBJS	+= cmd/find.o
endif
ifdef CMD_true
	CFLAGS	+= -DCMD_true
endif

ifdef NEED_buildname
	OBJS	+= lib/buildname.o
endif
ifdef NEED_copyfile
	OBJS	+= lib/copyfile.o
endif
ifdef NEED_isadir
	OBJS	+= lib/isadir.o
endif
ifdef NEED_stripslash
	OBJS	+= lib/stripslash.o
endif
ifdef	NEED_utc_mktime
	OBJS	+= lib/utc_mktime.o
endif

all: $(OUT)

$(OUT):	$(OBJS)
	@echo '[LD]	$@'
	@$(LD) $(LDFLAGS) -o $@ $^ > busyelks.map

$(OBJS): config.mk

clean:
	$(RM) $(OBJS)

doc:	README.html

README.html:	README
	asciidoc README

install: $(OUT) busyelks.tar.xz
	$(INSTALL) $(OUT) $(DESTDIR)/bin
	tar Jxf busyelks.tar.xz -C $(DESTDIR)

busyelks.tar.xz: busyelks.fs $(OUT)
	b=`grep '/busyelks' < $<` \
	&& $(RM) -r _install \
	&& mkdir -p "`dirname "_install/$$b"`" \
	&& cp $(OUT) "_install/$$b" \
	&& grep '^[ 	]*CMD(' < main.c \
		| sed 's|^.*CMD([0 \t]*"||' \
		| sed 's|".*$$||' \
		| while read f; \
			do \
				if p=`grep "/$$f"` < $<; \
				then \
					mkdir -p "`dirname "_install/$$p"`"; \
					ln -sr "_install/$$b" "_install/$$p"; \
				fi; \
			done
	tar Jcf $@ -C _install .
	$(RM) -r _install