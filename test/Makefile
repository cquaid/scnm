TOP_DIR := $(CURDIR)/..
include $(TOP_DIR)/master.mk

CFG := test
CFG_LIST := test

APPS := \
	test_pid_maps \
	test_filter

TARGETS := $(foreach app,$(APPS),$(BUILD_DIR_BIN)/test/$(app))

DEPEND := shared src

SHARED_CFLAGS := \
	$(BASE_CFLAGS) \
	-I$(BUILD_DIR_INCLUDE) \
	-I$(TOP_DIR)/src \
	-ggdb3 -g -O0

SHARED_LDFLAGS := $(BASE_LDFLAGS)

SRC_PATH_TAIL := $(patsubst $(abspath $(TOP_DIR))/%,%,$(abspath $(CURDIR)))
OBJ_PATH = $(abspath $(BUILD_DIR_OBJ)/$(CFG)/$(SRC_PATH_TAIL))

OBJ = $(foreach src,$(SRC),$(abspath $(OBJ_PATH)/$(src:.c=.o)))

test_pid_maps_SRC := test_pid_maps.c
test_pid_maps_LDFLAGS := -l:libwintermute.a

test_filter_SRC := test_filter.c
test_filter_LDFLAGS := -l:libwintermute.a


.PHONY: all
all: depend $(TARGETS)

.PHONY: depend
depend:
	@for x in $(DEPEND) ; do \
		$(MAKE) -C $(TOP_DIR)/$${x} ; \
	done

.PHONY: clean
clean:
	rm -f $(TARGETS)
	rm -rf $(foreach CFG,$(CFG_LIST),$(OBJ_PATH))

$(TARGETS): NAME = $(notdir $(basename $@))
$(TARGETS): SRC = $($(NAME)_SRC)
$(TARGETS): CFLAGS = $(SHARED_CFLAGS) $($(NAME)_CFLAGS)
$(TARGETS): LDFLAGS = $(SHARED_LDFLAGS) $($(NAME)_LDFLAGS)
$(TARGETS): DEPEND = $(OBJ)

