.SECONDEXPANSION:

apps := \
	test_pid_maps \
	test_filter

SRC_DIR := $(CURDIR)/../src

BASE_CFLAGS  := -Wall -Wextra -Werror -I$(SRC_DIR) -g -ggdb3
BASE_LDFLAGS :=

test_pid_maps_SRC := \
	$(CURDIR)/test_pid_maps.c \
	$(SRC_DIR)/region.c \
	$(SRC_DIR)/pid_maps.c
test_pid_maps_CFLAGS :=
test_pid_maps_LDFLAGS :=

test_filter_SRC := \
	$(CURDIR)/test_filter.c \
	$(SRC_DIR)/region.c \
	$(SRC_DIR)/pid_maps.c
test_filter_CFLAGS :=
test_filter_LDFLAGS :=



all: $(apps)

$(apps): SRC = $($(@)_SRC)
$(apps): OBJ = $(patsubst %.c,%.o,$(SRC))
$(apps): CFLAGS = $(BASE_CFLAGS) $($(@)_CFLAGS)
$(apps): LDFLAGS = $(BASE_LDFLAGS) $($(@)_LDFLAGS)
$(apps): ERROR = $(error src: $(SRC))
$(apps): $$(OBJ)
	gcc -o $@ $(filter %.o,$^) $(LDFLAGS)


clean:
	rm -f $(apps)
	rm -f *.o $(SRC_DIR)/*.o
