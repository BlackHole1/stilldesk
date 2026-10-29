# StillDesk：构建注入库和启动器。
#
#   make            构建到 build/
#   make install    安装到 ToDesk.app（见 scripts/install.sh）
#   make uninstall  还原 ToDesk.app
#   make clean

APP        ?= /Applications/ToDesk.app
BUILD      := build
CC         := xcrun clang
ARCH       := -arch arm64
MIN        := -mmacosx-version-min=11.0
CFLAGS     := $(ARCH) $(MIN) -O2 -Wall -Wextra -Wno-unused-parameter
DYLIB      := $(BUILD)/libStillDesk.dylib
LAUNCHER   := $(BUILD)/stilldesk-launch

all: $(DYLIB) $(LAUNCHER)

$(BUILD):
	@mkdir -p $(BUILD)

$(DYLIB): src/stilldesk.c src/config_dump.c src/trampoline.s | $(BUILD)
	$(CC) $(CFLAGS) -dynamiclib -o $@ src/stilldesk.c src/config_dump.c src/trampoline.s

$(LAUNCHER): src/launcher.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ src/launcher.c

install: all
	./scripts/install.sh "$(APP)"

uninstall:
	./scripts/uninstall.sh "$(APP)"

clean:
	rm -rf $(BUILD)

.PHONY: all install uninstall clean
