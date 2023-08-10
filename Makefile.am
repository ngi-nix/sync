# This Makefile is in the public domain

if DOC_ONLY
if ENABLE_DOC
  SUBDIRS = . doc
else
  SUBDIRS = .
endif
else
if ENABLE_DOC
  SUBDIRS = . src doc contrib
else
  SUBDIRS = . src doc contrib
endif
endif

@DX_RULES@

ACLOCAL_AMFLAGS = -I m4
EXTRA_DIST = \
 AUTHORS \
 COPYING.AGPL \
 Doxyfile
