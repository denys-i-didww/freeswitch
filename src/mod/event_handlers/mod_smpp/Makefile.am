include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_smpp

if HAVE_SMPP34

mod_LTLIBRARIES = mod_smpp.la
mod_smpp_la_SOURCES  = mod_smpp_utils.c mod_smpp_gateway.c mod_smpp_message.c mod_smpp.c
mod_smpp_la_CFLAGS   = $(AM_CFLAGS) $(SMPP34_CFLAGS)
mod_smpp_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_smpp_la_LDFLAGS  = -avoid-version -module -no-undefined -shared $(SMPP34_LIBS) $(SWITCH_AM_LDFLAGS)

# The executable compiles the real handlers with a test-only gateway boundary.
# No test controls are linked into the production module.
noinst_PROGRAMS = test/test_mod_smpp
test_test_mod_smpp_SOURCES = test/test_mod_smpp.c test/smpp_test_transport.c
test_test_mod_smpp_CFLAGS = $(mod_smpp_la_CFLAGS) \
	-DSWITCH_TEST_BASE_DIR_FOR_CONF=\"${abs_srcdir}/test\" \
	-DSWITCH_TEST_BASE_DIR_OVERRIDE=\"${abs_builddir}/test\"
test_test_mod_smpp_LDADD = $(switch_builddir)/libfreeswitch.la
test_test_mod_smpp_LDFLAGS = -avoid-version -no-undefined $(SWITCH_AM_LDFLAGS)
TESTS = $(noinst_PROGRAMS)
EXTRA_DIST = test/smpp_test_transport.h test/conf/freeswitch.xml \
	test/check_status_regression.py README.md

else
install: error
all: error
error:
	$(error You must install libsmpp34-dev to build this module)
endif
