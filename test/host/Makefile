# Host unit tests for ESPINK pure logic. Run: make -C test/host
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -g -I../../main
BUILD   := build

.PHONY: test clean
test: $(BUILD)/test_slots $(BUILD)/test_render_fb
	$(BUILD)/test_slots
	$(BUILD)/test_render_fb

$(BUILD)/test_slots: test_slots.c ../../main/slots.c ../../main/slots.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_slots.c ../../main/slots.c

$(BUILD)/test_render_fb: test_render_fb.c ../../main/render_fb.c ../../main/render_fb.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_render_fb.c ../../main/render_fb.c

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
