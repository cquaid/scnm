TOP_DIR := $(CURDIR)/..
include $(TOP_DIR)/master.mk

INC_SRC_DIR  := $(CURDIR)/include
INC_DEST_DIR := $(BUILD_DIR_INCLUDE)/shared

INCLUDE_FILES := $(wildcard $(INC_SRC_DIR)/*.h)

TARGETS := \
	$(foreach header,$(INCLUDE_FILES), \
		$(INC_DEST_DIR)/$(notdir $(header)))

.PHONY: all
all: $(TARGETS)

.PHONY: clean
clean:
	rm -f $(TARGETS)
	rm -rf $(INC_DEST_DIR)

$(INC_DEST_DIR)/%.h: NAME = $*
$(INC_DEST_DIR)/%.h: SRC = $(abspath $(INC_SRC_DIR)/$(NAME).h)
$(INC_DEST_DIR)/%.h: $$(SRC) $$(MAKEFILE_LIST)
	@mkdir -p $(dir $@)
	cp $< $@

