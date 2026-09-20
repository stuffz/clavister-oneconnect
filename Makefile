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

.PHONY: all cli gui helper clean install uninstall lint format run help \
	package test test-build image container-build container-test

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

# makepkg rewrites pkgver= in whatever PKGBUILD it runs, so it runs on a copy
# and the tracked file keeps its placeholder. The copy is told where the
# repository is and packages its committed HEAD.
PACKAGE_DIR = $(BUILD_BASE)/package

package:
	mkdir -p "$(PACKAGE_DIR)"
	cp packaging/arch/PKGBUILD "$(PACKAGE_DIR)/"
	cd "$(PACKAGE_DIR)" && ONECONNECT_REPO="$(CURDIR)" makepkg -fsi $(MAKEPKG_FLAGS)

# ------------------------------------------------------------------- tests ---
# CMake rather than this Makefile: QTest needs moc, and CMAKE_AUTOMOC plus
# CTest is the whole of that rule. tests/ is its own CMake project so nothing
# suggests the binaries build any other way than the rules above.
CMAKE ?= cmake
CTEST ?= ctest
# Host and container need separate directories: a CMake cache records the
# absolute path it was configured at, and the container's is /work.
TEST_DIR ?= $(BUILD_BASE)/test
CONTAINER_TEST_DIR = $(BUILD_BASE)/test-container

# Qt6 adds this for itself, in the spelling GCC accepts. clangd is clang and
# rejects it, so the database the editor reads loses it. The build keeps it.
GCC_ONLY_FLAG = -mno-direct-extern-access

# The compile database lands in tests/, not the root: clangd picks the nearest
# one, so the tests get their moc include paths from it while src/ keeps
# reading compile_flags.txt.
test-build:
	$(CMAKE) -S tests -B "$(TEST_DIR)" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	$(CMAKE) --build "$(TEST_DIR)" --parallel
	sed 's/ $(GCC_ONLY_FLAG)//g' "$(TEST_DIR)/compile_commands.json" > tests/compile_commands.json

test: test-build
	$(CTEST) --test-dir "$(TEST_DIR)" --output-on-failure

# --------------------------------------------------------------- container ---
# Everything the build needs lives in the image; nothing installs on the host.
DOCKER ?= docker
IMAGE = clavister-oneconnect-build
DOCKER_RUN = $(DOCKER) run --rm -u $(shell id -u):$(shell id -g) \
	-e TEST_DIR="$(CONTAINER_TEST_DIR)" -v "$(CURDIR)":/work -w /work $(IMAGE)

image:
	$(DOCKER) build -t $(IMAGE) .

container-build: image
	$(DOCKER_RUN) make

# /work is this directory bind mounted, so rewriting the paths makes the
# database name the same files, generated moc output included.
container-test: image
	$(DOCKER_RUN) make test
	sed 's|/work|$(CURDIR)|g' "$(CONTAINER_TEST_DIR)/compile_commands.json" | \
		sed 's/ $(GCC_ONLY_FLAG)//g' > tests/compile_commands.json

# Needs a compile_commands.json or compile_flags.txt; the latter is checked in.
# The tests go through the database test-build exports instead: each one
# includes moc output that exists only in the test build tree.
LINT_TESTS := $(shell find tests -name '*.cpp' | sort)

lint: test-build
	clang-tidy $(CLI_SOURCES) $(HELPER_SOURCES) -- $(CXXFLAGS)
	@test "$(HAVE_QT)" = "yes" && clang-tidy $(GUI_SOURCES) -- $(GUI_CXXFLAGS) || true
	clang-tidy --quiet -p tests $(LINT_TESTS)

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
	@echo "  package    build and install the Arch package from committed HEAD"
	@echo "  test       configure with CMake and run the CTest suite"
	@echo "  image      build the build container image"
	@echo "  container-build  build inside the container"
	@echo "  container-test   run the CTest suite inside the container"
	@echo ""
	@echo "Variables:"
	@echo "  DEBUG=1    debug build, no optimisation"
	@echo "  PREFIX     install prefix"
	@echo ""
	@echo "Qt6Widgets found: $(if $(HAVE_QT),yes,no)"
