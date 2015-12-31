TOP_DIR := $(CURDIR)/..
include $(TOP_DIR)/master.mk

LIBNAMES := \
	libwintermute.a  libwintermute-test.a \
	libwintermute.so libwintermute-test.so

TARGETS := $(foreach lib,$(LIBNAMES),$(BUILD_DIR_LIB)/$(lib))

DEPEND := shared lib


SHARED_CFLAGS := $(BASE_CFLAGS) -I$(BUILD_DIR_INCLUDE)
SHARED_LDFLAGS := $(BASE_LDFLAGS)

TEST_CFLAGS := -ggdb3 -g -O0
TEST_LDFLAGS :=

STATIC_CFLAGS := -static
STATIC_LDFLAGS :=

DYNAMIC_CFLAGS := -fpic
DYNAMIC_LDFLAGS = -shared -Wl,-soname,$(SONAME)

SRC_PATH_TAIL := $(patsubst $(abspath $(TOP_DIR))/%,%,$(abspath $(CURDIR)))
OBJ_PATH = $(abspath $(BUILD_DIR_OBJ)/$(CFG)/$(SRC_PATH_TAIL))

SRC := \
	command.c \
	match.c \
	pid_maps.c \
	region.c
#	server.c

OBJ = $(foreach src,$(SRC),$(abspath $(OBJ_PATH)/$(src:.c=.o)))

CFG_LIST := $(foreach tail,.a -test.a .so -test.so,libwintermute$(tail))

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

$(BUILD_DIR_LIB)/libwintermute.a: CFG = libwintermute.a
$(BUILD_DIR_LIB)/libwintermute.a: CFLAGS = $(SHARED_CFLAGS) $(STATIC_CFLAGS)
$(BUILD_DIR_LIB)/libwintermute.a: LDFLAGS = $(SHARED_LDFLAGS) $(STATIC_LDFLAGS)
$(BUILD_DIR_LIB)/libwintermute.a: DEPEND = $(OBJ)

$(BUILD_DIR_LIB)/libwintermute-test.a: CFG = libwintermute-test.a
$(BUILD_DIR_LIB)/libwintermute-test.a: CFLAGS  = $(SHARED_CFLAGS)
$(BUILD_DIR_LIB)/libwintermute-test.a: CFLAGS += $(TEST_CFLAGS)
$(BUILD_DIR_LIB)/libwintermute-test.a: CFLAGS += $(STATIC_CFLAGS)
$(BUILD_DIR_LIB)/libwintermute-test.a: LDFLAGS  = $(SHARED_LDFLAGS)
$(BUILD_DIR_LIB)/libwintermute-test.a: LDFLAGS += $(TEST_LDFLAGS)
$(BUILD_DIR_LIB)/libwintermute-test.a: LDFLAGS += $(STATIC_LDFLAGS)
$(BUILD_DIR_LIB)/libwintermute-test.a: DEPEND = $(OBJ)

$(BUILD_DIR_LIB)/libwintermute.so: CFG = libwintermute.so
$(BUILD_DIR_LIB)/libwintermute.so: CFLAGS = $(SHARED_CFLAGS) $(DYNAMIC_CFLAGS)
$(BUILD_DIR_LIB)/libwintermute.so: LDFLAGS  = $(SHARED_LDFLAGS)
$(BUILD_DIR_LIB)/libwintermute.so: LDFLAGS += $(DYNAMIC_LDFLAGS)
$(BUILD_DIR_LIB)/libwintermute.so: SONAME = wintermute
$(BUILD_DIR_LIB)/libwintermute.so: DEPEND = $(OBJ)

$(BUILD_DIR_LIB)/libwintermute-test.so: CFG = libwintermute-test.so
$(BUILD_DIR_LIB)/libwintermute-test.so: CFLAGS  = $(SHARED_CFLAGS)
$(BUILD_DIR_LIB)/libwintermute-test.so: CFLAGS += $(TEST_CFLAGS)
$(BUILD_DIR_LIB)/libwintermute-test.so: CFLAGS += $(DYNAMIC_CFLAGS)
$(BUILD_DIR_LIB)/libwintermute-test.so: LDFLAGS  = $(SHARED_LDFLAGS)
$(BUILD_DIR_LIB)/libwintermute-test.so: LDFLAGS += $(TEST_LDFLAGS)
$(BUILD_DIR_LIB)/libwintermute-test.so: LDFLAGS += $(DYNAMIC_LDFLAGS)
$(BUILD_DIR_LIB)/libwintermute-test.so: SONAME = wintermute
$(BUILD_DIR_LIB)/libwintermute-test.so: DEPEND = $(OBJ)

