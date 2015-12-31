TOP_DIR := $(CURDIR)/..
include $(TOP_DIR)/master.mk

SHARED_SRC_DIR := $(CURDIR)/include
SHARED_INC_DIR := $(BUILD_DIR_INCLUDE)/shared

INCLUDE_SRC := $(wildcard $(SHARED_SRC_DIR)/*.h)

INCLUDE_DEST := \
	$(foreach header,$(INCLUDE_SRC), \
		$(SHARED_INC_DIR)/$(notdir $(header)))


.PHONY: all
all: $(INCLUDE_DEST)

.PHONY: clean
clean:
	rm -f $(INCLUDE_DEST)
	rm -rf $(SHARED_INC_DIR)

$(SHARED_INC_DIR)/%.h: NAME = $*
$(SHARED_INC_DIR)/%.h: SRC = $(abspath $(SHARED_SRC_DIR)/$(NAME).h)
$(SHARED_INC_DIR)/%.h: $$(SRC) $$(MAKEFILE_LIST)
	@mkdir -p $(dir $@)
	cp $< $@

