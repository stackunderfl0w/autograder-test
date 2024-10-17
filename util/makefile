
prefix ?= /usr/local
exec_prefix ?= $(prefix)
bindir ?= $(exec_prefix)/bin

SRCS := $(shell find . -type f -name '*.c' -printf '%P\n')
OBJS := $(addprefix $(BUILDDIR),$(SRCS:.c=.o))
DEPS := $(addprefix $(BUILDDIR),$(SRCS:.c=.d))
EXES := $(filter-out %/signum,$(OBJS:.o=))

.PHONY: all install uninstall
all: $(EXES)

install: $(EXES)
	install -D $(EXES) $(PREFIX)/bin

uninstall:
	rm -f $(addprefix $(PREFIX)/bin,$(EXES))

$(addprefix $(BUILDDIR)/,check_signal_disposition signum test_term_sig wait_for_signal tracer): $(BUILDDIR)/signum.o

%.o: %.c
	$(LINK.c) $< $(LOADLIBES) $(LDLIBS) -o $@

$(BUILDDIR)/%.d: $(SRCDIR)/%.c
	$(CPP) -MM -MF $@ -MT '$$(BUILDDIR)/$(<:.c=.o)' $(CPPFLAGS) $< >/dev/null

makefile: $(DEPS)
 
include $(DEPS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(COMPILE.c) -o $@ $<


clean:
	rm -f $(OBJS) $(DEPS) $(EXES)
