TOP_DIR := $(CURDIR)/..
include $(TOP_DIR)/master.mk

DEPEND := shared
SUBS := ptracer

.PHONY: all
all: depend
	@for x in $(SUBS) ; do \
		$(MAKE) -C $(CURDIR)/$${x} ; \
	done

.PHONY: depend
depend:
	@for x in $(DEPEND) ; do \
		$(MAKE) -C $(TOP_DIR)/$${x} ; \
	done

.PHONY: clean
clean:
	@for x in $(SUBS) ; do \
		$(MAKE) -C $(CURDIR)/$${x} clean ; \
	done

