.SECONDEXPANSION:

EMPTY :=
SPACE := $(EMPTY) $(EMPTY)

BASE_CFLAGS := -Wall -Wextra -Werror -O2 -std=gnu99
BASE_LDFLAGS = -L$(BUILD_DIR_LIB)

BUILD_DIR = $(abspath $(TOP_DIR)/build)
BUILD_DIR_BIN = $(BUILD_DIR)/bin
BUILD_DIR_LIB = $(BUILD_DIR)/lib
BUILD_DIR_OBJ = $(BUILD_DIR)/obj
BUILD_DIR_INCLUDE = $(BUILD_DIR)/include


$(BUILD_DIR_INCLUDE)/%.h: NAME = $*
$(BUILD_DIR_INCLUDE)/%.h: SRC = $(abspath $(TOP_DIR)/$(NAME).h)
$(BUILD_DIR_INCLUDE)/%.h: $$(SRC) $$(MAKEFILE_LIST)
	@mkdir -p $(dir $@)
	cp $< $@

$(BUILD_DIR_OBJ)/%.o: WORDS = $(subst /,$(SPACE),$*)
$(BUILD_DIR_OBJ)/%.o: CFG = $(firstword $(WORDS))
$(BUILD_DIR_OBJ)/%.o: NAME = $(patsubst $(CFG)/%,%,$*)
$(BUILD_DIR_OBJ)/%.o: SRC = $(abspath $(TOP_DIR)/$(NAME).c)
$(BUILD_DIR_OBJ)/%.o: DEP = $(abspath $(TOP_DIR)/$(NAME).d)
$(BUILD_DIR_OBJ)/%.o: $$(SRC) $$(MAKEFILE_LIST) #$$(DEP)
	@mkdir -p $(dir $@)
	gcc -M $< $(CFLAGS) > $(basename $@).d
	gcc -o $@ -c $< $(CFLAGS)

$(BUILD_DIR_BIN)/%: $$(DEPEND) $$(MAKEFILE_LIST)
	@mkdir -p $(dir $@)
	gcc -o $@ $(filter %.o,$^) $(LDFLAGS) $(LDADD)

$(BUILD_DIR_LIB)/%.a: $$(DEPEND) $$(MAKEFILE_LIST)
	@mkdir -p $(dir $@)
	ar -rc $@ $(filter %.o,$^)
	ranlib $@

$(BUILD_DIR_LIB)/%.so: $$(DEPEND) $$(MAKEFILE_LIST)
	@mkdir -p $(dir $@)
	gcc -o $@ $(filter %.o,$^) $(LDFLAGS) $(LDADD)

$(TOP_DIR)/%.d: ;
