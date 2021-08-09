TARGET  := ./build/edit
EXT     := c cc

SRC_DIR := src
OBJ_DIR := build
DEP_DIR := lib

CPPFLAGS  = -MMD -MP -MF $(@:$(OBJ_DIR)/%.o=$(DEP_DIR)/%.d)
CFLAGS   := -Wall -Wextra -pedantic -ggdb -pthread -lncurses
CXXFLAGS := -std=c++17 $(CFLAGS)
LDFLAGS  := -pthread

SOURCE := $(foreach ext, $(EXT), $(wildcard $(SRC_DIR)/*.$(ext)))
OBJECT := $(SOURCE:$(SRC_DIR)/%=$(OBJ_DIR)/%.o)
DEPEND := $(OBJECT:$(OBJ_DIR)/%.o=$(DEP_DIR)/%.d)

define rule =
$(OBJ_DIR)/%.$(1).o: $(SRC_DIR)/%.$(1) | $(OBJ_DIR) $(DEP_DIR)
	$$(COMPILE.$(1)) $$< -o $$@
endef

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECT)
	$(CXX) $(LDFLAGS) $(CXXFLAGS) $^ -o $@ ./lib/libtree-sitter.a

$(foreach ext, $(EXT), $(eval $(call rule,$(ext))))

$(OBJ_DIR) $(DEP_DIR):
	mkdir -p $@

-include $(DEPEND)

clean:
	$(RM) -r $(TARGET) $(OBJ_DIR)
