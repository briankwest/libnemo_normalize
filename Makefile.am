ACLOCAL_AMFLAGS = -I m4

# ---------- shared library ----------
lib_LTLIBRARIES = libnemo_normalize.la

libnemo_normalize_la_SOURCES = nemo_normalize.cpp
libnemo_normalize_la_CXXFLAGS = -std=c++17 -O2 -Wall $(FST_CFLAGS)
libnemo_normalize_la_LIBADD  = $(FST_LIBS) -ldl
libnemo_normalize_la_LDFLAGS = -version-info 0:0:0

# ---------- public header ----------
include_HEADERS = nemo_normalize.h

# ---------- pkg-config ----------
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = nemo_normalize.pc

# ---------- test binary ----------
check_PROGRAMS = test_normalize

test_normalize_SOURCES = test_normalize.c
test_normalize_CFLAGS  = -std=c11 -O2 -Wall -I$(srcdir)
test_normalize_LDADD   = libnemo_normalize.la -lm

TESTS = test_normalize

# ---------- data files (far_export/) ----------
fardatadir = $(datadir)/nemo-normalize/far_export

install-data-local:
	@if test -d "$(srcdir)/far_export"; then \
	  echo "Installing FAR grammar data..."; \
	  cd "$(srcdir)/far_export" && find . -type d | while read d; do \
	    $(MKDIR_P) "$(DESTDIR)$(fardatadir)/$$d"; \
	  done && find . -type f | while read f; do \
	    $(INSTALL_DATA) "$$f" "$(DESTDIR)$(fardatadir)/$$f"; \
	  done; \
	fi

uninstall-local:
	rm -rf "$(DESTDIR)$(fardatadir)"

# ---------- convenience alias ----------
test: check
.PHONY: test
