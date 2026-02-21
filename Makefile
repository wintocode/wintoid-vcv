RACK_DIR ?= ./Rack-SDK

# macOS Sequoia adds com.apple.provenance xattr that breaks default codesign
CODESIGN ?= codesign --force --sign - --no-strict

FLAGS +=
CFLAGS +=
CXXFLAGS +=
LDFLAGS +=

SOURCES += $(wildcard src/*.cpp) $(wildcard src/*/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

include $(RACK_DIR)/plugin.mk
