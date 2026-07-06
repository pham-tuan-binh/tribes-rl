CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror
ASAN_FLAGS = -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror

BUILD := build
TEST_SRCS := $(wildcard core/tests/test_*.c)
TESTS := $(patsubst core/tests/%.c,$(BUILD)/%,$(TEST_SRCS))
HDRS := core/polytopia.h core/constants.h core/actions.h

.PHONY: test asan clean

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

$(BUILD)/test_%: core/tests/test_%.c $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

asan: | $(BUILD)
	@for t in $(TEST_SRCS); do \
		out=$(BUILD)/$$(basename $$t .c)_asan; \
		$(CC) $(ASAN_FLAGS) -o $$out $$t && ./$$out || exit 1; \
	done

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
