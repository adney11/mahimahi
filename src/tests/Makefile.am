dist_check_SCRIPTS = packetshell-test

bin_PROGRAMS = test_advlinkshell
test_advlinkshell_SOURCES = test_advlinkshell.cc
test_advlinkshell_LDFLAGS = -pthread

bin_PROGRAMS += test_advlinkshell_client
test_advlinkshell_client_SOURCES = test_advlinkshell_client.cc
test_advlinkshell_client_LDFLAGS = -pthread



installcheck-local:
	$(srcdir)/packetshell-test
