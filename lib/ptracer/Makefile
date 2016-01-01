TOP_DIR := $(CURDIR)/../..
include $(TOP_DIR)/master.mk

INC_SRC_DIR  := $(CURDIR)/include
INC_DEST_DIR := $(BUILD_DIR_INCLUDE)/ptracer

INCLUDE_FILES := $(wildcard $(INC_SRC_DIR)/*.h)
INCLUDE_TARGET_FILES := \
	$(foreach header,$(INCLUDE_FILES), \
		$(abspath $(INC_DEST_DIR)/$(notdir $(header))))

LIBNAMES := \
	libptracer.a  libptracer-test.a \
	libptracer.so libptracer-test.so

TARGETS := \
	$(INCLUDE_TARGET_FILES) \
	$(foreach lib,$(LIBNAMES),$(BUILD_DIR_LIB)/$(lib))

DEPEND := shared

SHARED_CFLAGS := \
	$(BASE_CFLAGS) \
	-I$(BUILD_DIR_INCLUDE) \
	-I$(INC_SRC_DIR)

SHARED_LDFLAGS := $(BASE_LDFLAGS)

TEST_CFLAGS := -ggdb3 -g -O0
TEST_LDFLAGS :=

STATIC_CFLAGS := -static
STATIC_LDFLAGS :=

DYNAMIC_CFLAGS := -fpic
DYNAMIC_LDFLAGS = -shared -Wl,-soname,$(SONAME)

SRC_PATH_TAIL := $(patsubst $(abspath $(TOP_DIR))/%,%,$(abspath $(CURDIR)))
OBJ_PATH = $(abspath $(BUILD_DIR_OBJ)/$(CFG)/$(SRC_PATH_TAIL))

SRC_DIR := $(CURDIR)/src
SRC_FILES := $(wildcard $(SRC_DIR)/*.c)
SRC := $(foreach src,$(SRC_FILES), src/$(notdir $(src)))

OBJ = $(foreach src,$(SRC),$(abspath $(OBJ_PATH)/$(src:.c=.o)))

CFG_LIST := $(foreach tail,.a -test.a .so -test.so,libptracer$(tail))

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
	rm -f $(foreach CFG,$(CFG_LIST),$(OBJ))
	rm -rf $(INC_DEST_DIR)
	rm -rf $(foreach CFG,$(CFG_LIST),$(OBJ_PATH))

$(BUILD_DIR_LIB)/libptracer.a: CFG = libptracer.a
$(BUILD_DIR_LIB)/libptracer.a: CFLAGS = $(SHARED_CFLAGS) $(STATIC_CFLAGS)
$(BUILD_DIR_LIB)/libptracer.a: LDFLAGS = $(SHARED_LDFLAGS) $(STATIC_LDFLAGS)
$(BUILD_DIR_LIB)/libptracer.a: DEPEND = $(OBJ)

$(BUILD_DIR_LIB)/libptracer-test.a: CFG = libptracer-test.a
$(BUILD_DIR_LIB)/libptracer-test.a: CFLAGS  = $(SHARED_CFLAGS)
$(BUILD_DIR_LIB)/libptracer-test.a: CFLAGS += $(TEST_CFLAGS)
$(BUILD_DIR_LIB)/libptracer-test.a: CFLAGS += $(STATIC_CFLAGS)
$(BUILD_DIR_LIB)/libptracer-test.a: LDFLAGS  = $(SHARED_LDFLAGS)
$(BUILD_DIR_LIB)/libptracer-test.a: LDFLAGS += $(TEST_LDFLAGS)
$(BUILD_DIR_LIB)/libptracer-test.a: LDFLAGS += $(STATIC_LDFLAGS)
$(BUILD_DIR_LIB)/libptracer-test.a: DEPEND = $(OBJ)

$(BUILD_DIR_LIB)/libptracer.so: CFG = libptracer.so
$(BUILD_DIR_LIB)/libptracer.so: CFLAGS = $(SHARED_CFLAGS) $(DYNAMIC_CFLAGS)
$(BUILD_DIR_LIB)/libptracer.so: LDFLAGS = $(SHARED_LDFLAGS) $(DYNAMIC_LDFLAGS)
$(BUILD_DIR_LIB)/libptracer.so: SONAME = ptracer
$(BUILD_DIR_LIB)/libptracer.so: DEPEND = $(OBJ)

$(BUILD_DIR_LIB)/libptracer-test.so: CFG = libptracer-test.so
$(BUILD_DIR_LIB)/libptracer-test.so: CFLAGS  = $(SHARED_CFLAGS)
$(BUILD_DIR_LIB)/libptracer-test.so: CFLAGS += $(TEST_CFLAGS)
$(BUILD_DIR_LIB)/libptracer-test.so: CFLAGS += $(DYNAMIC_CFLAGS)
$(BUILD_DIR_LIB)/libptracer-test.so: LDFLAGS  = $(SHARED_LDFLAGS)
$(BUILD_DIR_LIB)/libptracer-test.so: LDFLAGS += $(TEST_LDFLAGS)
$(BUILD_DIR_LIB)/libptracer-test.so: LDFLAGS += $(DYNAMIC_LDFLAGS)
$(BUILD_DIR_LIB)/libptracer-test.so: SONAME = ptracer
$(BUILD_DIR_LIB)/libptracer-test.so: DEPEND = $(OBJ)

$(INC_DEST_DIR)/%.h: NAME = $*
$(INC_DEST_DIR)/%.h: SRC = $(abspath $(INC_SRC_DIR)/$(NAME).h)
$(INC_DEST_DIR)/%.h: $$(SRC) $$(MAKEFILE_LIST)
	@mkdir -p $(dir $@)
	cp $< $@

