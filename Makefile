#-------------------------------------------------------------------------
#
# Makefile for src/bin/pg_clearxlogtail
#
# Copyright (c) 1998-2008, PostgreSQL Global Development Group
#
#-------------------------------------------------------------------------

PGFILEDESC = "pg_clearxlogtail - clear unused space at xlog tail"

ifdef USE_PGXS

OBJS = pg_clearxlogtail.o
PROGRAM = pg_clearxlogtail

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

else

subdir = src/bin/pg_clearxlogtail
top_builddir = ../../..
include $(top_builddir)/src/Makefile.global

override CPPFLAGS += -DFRONTEND

OBJS= pg_clearxlogtail.o $(WIN32RES)

all: pg_clearxlogtail

pg_clearxlogtail: $(OBJS)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) $(LIBS) -o $@$(X)

install: all installdirs
	$(INSTALL_PROGRAM) pg_clearxlogtail$(X) '$(DESTDIR)$(bindir)/pg_clearxlogtail$(X)'

installdirs:
	$(mkinstalldirs) '$(DESTDIR)$(bindir)'

uninstall:
	rm -f '$(DESTDIR)$(bindir)/pg_clearxlogtail$(X)'

clean distclean maintainer-clean:
	rm -f pg_clearxlogtail$(X) $(OBJS)

endif
