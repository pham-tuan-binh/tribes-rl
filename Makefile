CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror
ASAN_FLAGS = -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror

BUILD := build
TESTS := $(BUILD)/test_constants

.PHONY: test asan clean

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

$(BUILD)/test_constants: core/tests/test_constants.c core/polytopia.h core/constants.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

asan: | $(BUILD)
	$(CC) $(ASAN_FLAGS) -o $(BUILD)/test_constants_asan core/tests/test_constants.c
	./$(BUILD)/test_constants_asan

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
