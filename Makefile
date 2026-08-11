# Makefile for clavister-oneconnect (Linux)

CXX ?= g++
BUILD_DATE := $(shell date +"%Y-%m-%d %H:%M:%S")
GIT_HASH ?= $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")

DEBUG ?= 0
PREFIX ?= /usr/local
DESTDIR ?=

SRC_DIR = src
BUILD_BASE = build

ifeq ($(DEBUG), 1)
    BUILD_DIR = $(BUILD_BASE)/debug
else
    BUILD_DIR = $(BUILD_BASE)/release
endif

OBJ_DIR = $(BUILD_DIR)/obj

CLI_NAME = oneconnect
GUI_NAME = oneconnect-gui
HELPER_NAME = oneconnect-helper
CLI_TARGET = $(BUILD_DIR)/$(CLI_NAME)
GUI_TARGET = $(BUILD_DIR)/$(GUI_NAME)
HELPER_TARGET = $(BUILD_DIR)/$(HELPER_NAME)

WARNINGS = -Wall -Wextra -Wsuggest-override -Wnon-virtual-dtor -Wshadow

COMMON_CXXFLAGS = -std=c++17 -O2 $(WARNINGS) -MMD -MP -I$(SRC_DIR) \
	-DBUILD_DATE="\"$(BUILD_DATE)\"" -DGIT_HASH="\"$(GIT_HASH)\""

# libopenconnect and yaml-cpp both ship pkg-config files; fall back to plain -l
# so the build still works where the .pc is missing.
CORE_PKGS = openconnect yaml-cpp libsecret-1
CORE_CFLAGS := $(shell pkg-config --cflags $(CORE_PKGS) 2>/dev/null)
CORE_LIBS := $(shell pkg-config --libs $(CORE_PKGS) 2>/dev/null || echo "-lopenconnect -lyaml-cpp -lsecret-1")

# -isystem, not -I: keeps Qt's own header warnings out of our build output.
QT_PKGS = Qt6Widgets
QT_CFLAGS := $(shell pkg-config --cflags-only-I $(QT_PKGS) 2>/dev/null | sed 's/-I/-isystem /g')
QT_OTHER := $(shell pkg-config --cflags-only-other $(QT_PKGS) 2>/dev/null)
QT_LIBS := $(shell pkg-config --libs $(QT_PKGS) 2>/dev/null)
HAVE_QT := $(shell pkg-config --exists $(QT_PKGS) 2>/dev/null && echo yes)

CLI_SOURCES = $(SRC_DIR)/main_cli.cpp
GUI_SOURCES = $(SRC_DIR)/main_gui.cpp
HELPER_SOURCES = $(SRC_DIR)/main_helper.cpp

CXXFLAGS = $(COMMON_CXXFLAGS) $(CORE_CFLAGS)
LIBS = $(CORE_LIBS) -lpthread

# No Q_OBJECT anywhere in src/, so there is no moc step: signals come from Qt's
# own classes and everything we connect is a lambda or a plain member function.
# Keep it that way -- adding Q_OBJECT means adding a moc rule here.
#
# QT_NO_KEYWORDS is required, not cosmetic. Qt defines `signals` as `public`,
# and GLib (pulled in by libsecret) has a struct member called `signals` --
# GDBusInterfaceInfo expands to `GDBusSignalInfo **public;` and the compile
# dies. We use Q_SIGNALS/Q_SLOTS spellings nowhere, so turning the unprefixed
# keywords off costs nothing.
GUI_CXXFLAGS = $(CXXFLAGS) $(QT_CFLAGS) $(QT_OTHER) -fPIC -DQT_NO_KEYWORDS
GUI_LIBS = $(LIBS) $(QT_LIBS)

ifeq ($(DEBUG), 1)
    CXXFLAGS += -g -DDEBUG -O0
    GUI_CXXFLAGS += -g -DDEBUG -O0
endif

CLI_OBJECTS = $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(CLI_SOURCES))
HELPER_OBJECTS = $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(HELPER_SOURCES))
GUI_OBJECTS = $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/gui_%.o,$(GUI_SOURCES))
DEPS = $(CLI_OBJECTS:.o=.d) $(GUI_OBJECTS:.o=.d) $(HELPER_OBJECTS:.o=.d)

.PHONY: all cli gui helper clean install uninstall lint format run help

ifeq ($(HAVE_QT),yes)
all: cli gui helper
else
all: cli helper
	@echo "note: Qt6Widgets not found, skipping $(GUI_NAME). Install qt6-base (Arch) or qt6-base-dev (Debian/Ubuntu)."
endif

cli: $(CLI_TARGET)
gui: $(GUI_TARGET)
helper: $(HELPER_TARGET)

$(CLI_TARGET): $(CLI_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CLI_OBJECTS) -o $@ $(LDFLAGS) $(LIBS)
	@echo "built $@"

$(GUI_TARGET): $(GUI_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(GUI_OBJECTS) -o $@ $(LDFLAGS) $(GUI_LIBS)
	@echo "built $@"

# The helper links nothing but libc: no openconnect, no yaml, no Qt. Keeping
# the privileged binary's dependency list at zero is the point of the split.
$(HELPER_TARGET): $(HELPER_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(HELPER_OBJECTS) -o $@ $(LDFLAGS)
	@echo "built $@"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/gui_%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(GUI_CXXFLAGS) -c $< -o $@

-include $(DEPS)

clean:
	rm -rf $(BUILD_BASE)

install: all
	install -Dm755 $(CLI_TARGET) $(DESTDIR)$(PREFIX)/bin/$(CLI_NAME)
	install -Dm755 $(HELPER_TARGET) $(DESTDIR)$(PREFIX)/lib/$(CLI_NAME)/$(HELPER_NAME)
# The policy annotates the helper's absolute path, which depends on PREFIX.
	sed 's|/usr/local/lib|$(PREFIX)/lib|' resources/linux/io.github.stuffz.oneconnect.policy \
		> $(BUILD_DIR)/io.github.stuffz.oneconnect.policy
	install -Dm644 $(BUILD_DIR)/io.github.stuffz.oneconnect.policy \
		$(DESTDIR)/usr/share/polkit-1/actions/io.github.stuffz.oneconnect.policy
	install -Dm644 resources/icons/oneconnect.svg \
		$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/oneconnect.svg
	install -Dm644 resources/icons/oneconnect-disconnected.svg \
		$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/oneconnect-disconnected.svg
	@test -f $(GUI_TARGET) && install -Dm755 $(GUI_TARGET) $(DESTDIR)$(PREFIX)/bin/$(GUI_NAME) || true
	@test -f $(GUI_TARGET) && install -Dm644 resources/linux/$(GUI_NAME).desktop \
		$(DESTDIR)$(PREFIX)/share/applications/$(GUI_NAME).desktop || true

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(CLI_NAME) $(DESTDIR)$(PREFIX)/bin/$(GUI_NAME)
	rm -f $(DESTDIR)$(PREFIX)/share/applications/$(GUI_NAME).desktop

# Needs a compile_commands.json or compile_flags.txt; the latter is checked in.
lint:
	clang-tidy $(CLI_SOURCES) $(HELPER_SOURCES) -- $(CXXFLAGS)
	@test "$(HAVE_QT)" = "yes" && clang-tidy $(GUI_SOURCES) -- $(GUI_CXXFLAGS) || true

format:
	clang-format -i $(shell find $(SRC_DIR) -name '*.hpp' -o -name '*.cpp')

# Convenience target: needs root for the tun device.
run: $(CLI_TARGET)
	sudo $(CLI_TARGET) $(ARGS)

# -E keeps the invoking user's environment, which is what puts the Qt platform
# plugin, theme and Wayland socket within reach of a root-run GUI.
run-gui: $(GUI_TARGET)
	sudo -E $(GUI_TARGET)

help:
	@echo "Targets:"
	@echo "  all        build both binaries (default)"
	@echo "  cli        build $(CLI_NAME) only"
	@echo "  gui        build $(GUI_NAME) only (needs Qt6Widgets)"
	@echo "  clean      remove build output"
	@echo "  install    install to \$$PREFIX/bin (default /usr/local)"
	@echo "  lint       run clang-tidy"
	@echo "  format     run clang-format over src/"
	@echo "  run        build and run the CLI with sudo, pass ARGS=\"--profile name\""
	@echo "  run-gui    build and run the GUI with sudo -E"
	@echo ""
	@echo "Variables:"
	@echo "  DEBUG=1    debug build, no optimisation"
	@echo "  PREFIX     install prefix"
	@echo ""
	@echo "Qt6Widgets found: $(if $(HAVE_QT),yes,no)"
