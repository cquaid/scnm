TOP_DIR := $(CURDIR)
include $(TOP_DIR)/master.mk

SUBS := shared lib src test


.PHONY: all
all:
	@for x in $(SUBS) ; do \
		$(MAKE) -C $(CURDIR)/$${x} ; \
	done

.PHONY: clean
clean:
	@for x in $(SUBS) ; do \
		$(MAKE) -C $(CURDIR)/$${x} clean ; \
	done

.PHONY: realclean
realclean: clean
	rm -rf $(BUILD_DIR)

