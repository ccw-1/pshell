# Makefile - POSIX-compliant, portable
CC = cc
CFLAGS = -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=200809L -g -O2 -pthread
LDFLAGS = -pthread

# Platform detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -lutil
endif
ifeq ($(UNAME_S),Darwin)
    LDFLAGS += -lutil
endif
ifeq ($(UNAME_S),SunOS)
    LDFLAGS += -lsocket -lnsl
endif

# Directories
SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin
INCLUDE_DIR = include

# Source files
DAEMON_SOURCES = $(shell find $(SRC_DIR)/daemon -name '*.c') \
                 $(shell find $(SRC_DIR)/http -name '*.c') \
                 $(shell find $(SRC_DIR)/socket -name '*.c' ! -name 'socket_client.c') \
                 $(shell find $(SRC_DIR)/session -name '*.c') \
                 $(shell find $(SRC_DIR)/pty -name '*.c') \
                 $(shell find $(SRC_DIR)/terminal -name '*.c') \
                 $(shell find $(SRC_DIR)/pattern -name '*.c') \
                 $(shell find $(SRC_DIR)/utils -name '*.c' ! -name 'test_stubs.c')

CLIENT_SOURCES = $(shell find $(SRC_DIR)/cli -name '*.c') \
                 $(SRC_DIR)/utils/logger.c \
                 $(SRC_DIR)/utils/json_simple.c \
                 $(SRC_DIR)/utils/string_utils.c

CTL_SOURCES = $(shell find $(SRC_DIR)/ctl -name '*.c') \
              $(SRC_DIR)/socket/socket_client.c \
              $(SRC_DIR)/utils/json_simple.c \
              $(SRC_DIR)/utils/string_utils.c \
              $(SRC_DIR)/utils/logger.c

DAEMON_OBJECTS = $(DAEMON_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
CLIENT_OBJECTS = $(CLIENT_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
CTL_OBJECTS = $(CTL_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Targets
DAEMON = $(BIN_DIR)/pshell
CLIENT = $(BIN_DIR)/pshell-client
CTL = $(BIN_DIR)/pshellctl

# Test sources
TEST_SOURCES = $(shell find tests/unit -name 'test_*.c')
TEST_OBJECTS = $(TEST_SOURCES:tests/unit/%.c=$(BUILD_DIR)/tests/%.o)
TEST_BINS = $(TEST_SOURCES:tests/unit/%.c=$(BIN_DIR)/tests/%)

.PHONY: all clean install test check help

all: $(DAEMON) $(CLIENT) $(CTL)

$(DAEMON): $(DAEMON_OBJECTS)
	@mkdir -p $(BIN_DIR)
	@echo "Linking $@..."
	$(CC) -o $@ $(DAEMON_OBJECTS) -pthread -lpthread -lutil
	@echo "Built: $@"

$(CLIENT): $(CLIENT_OBJECTS)
	@mkdir -p $(BIN_DIR)
	@echo "Linking $@..."
	@$(CC) -o $@ $(CLIENT_OBJECTS) $(LDFLAGS)
	@echo "Built: $@"

$(CTL): $(CTL_OBJECTS)
	@mkdir -p $(BIN_DIR)
	@echo "Linking $@..."
	@$(CC) -o $@ $(CTL_OBJECTS) $(LDFLAGS)
	@echo "Built: $@"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $< -o $@

# Test targets
$(BUILD_DIR)/tests/%.o: tests/unit/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling test $<..."
	@$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $< -o $@

$(BUILD_DIR)/utils/test_stubs.o: $(SRC_DIR)/utils/test_stubs.c
	@mkdir -p $(dir $@)
	@echo "Compiling test stubs..."
	@$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $< -o $@

$(BIN_DIR)/tests/%: $(BUILD_DIR)/tests/%.o $(BUILD_DIR)/utils/test_stubs.o $(DAEMON_OBJECTS)
	@mkdir -p $(dir $@)
	@echo "Linking test $@..."
	@$(CC) $< $(BUILD_DIR)/utils/test_stubs.o $(filter-out $(BUILD_DIR)/daemon/main.o,$(DAEMON_OBJECTS)) -o $@ $(LDFLAGS)

test: $(TEST_BINS)
	@echo "Running unit tests..."
	@for test in $(TEST_BINS); do \
		echo "Running $$test..."; \
		$$test || exit 1; \
	done
	@echo "All tests passed!"

check: all
	@echo "Running integration tests..."
	@./tests/integration/test_full_workflow.sh

clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "Clean complete."

install: all
	@echo "Installing pshell..."
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(DAEMON) $(DESTDIR)/usr/local/bin/
	install -m 755 $(CLIENT) $(DESTDIR)/usr/local/bin/
	install -m 755 $(CTL) $(DESTDIR)/usr/local/bin/
	install -d $(DESTDIR)/usr/local/share/pshell/web
	cp -r web/* $(DESTDIR)/usr/local/share/pshell/web/
	install -d $(DESTDIR)/etc/pshell
	install -m 644 config/pshell.conf $(DESTDIR)/etc/pshell/
	@echo "Installation complete."

deploy: all
	@echo "deploying locally"
	cp $(DAEMON) ~/local/bin
	cp $(CLIENT) ~/local/bin
	cp $(CTL) ~/local/bin
	cp config/pshell.conf ~/local/etc
	@echo "deploy complete."
	$(DAEMON) -c config/pshell.conf -f

help:
	@echo "pshell - Portable Programmable Shell"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build all binaries (default)"
	@echo "  clean    - Remove build artifacts"
	@echo "  test     - Run unit tests"
	@echo "  check    - Run integration tests"
	@echo "  install  - Install to system"
	@echo "  help     - Show this help"
	@echo ""
	@echo "Binaries:"
	@echo "  $(DAEMON)  - Main daemon"
	@echo "  $(CLIENT)  - HTTP client tool"
	@echo "  $(CTL)     - Socket control utility"
