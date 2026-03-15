CC         = clang
AS         = as
CFLAGS     = -w -O2 -std=c99 -D_GNU_SOURCE -fPIC
ASFLAGS    = --64
LDFLAGS    = -lpthread -lmbedtls -lmbedcrypto -lmbedx509 -no-pie
DEBUG_CFLAGS = -g -O0 -DDEBUG

TARGET_BIN = server
TARGET_CGI = cgi/config.cgi
SOURCES    = server.c
ASM_SOURCE = server.asm
ASM_OBJECT = server_asm.o
C_OBJECT   = server.o

.PHONY: all clean debug release help install

all: release

release: CFLAGS += -DNDEBUG
release: $(TARGET_BIN) $(TARGET_CGI)

debug: CFLAGS += $(DEBUG_CFLAGS)
debug: $(TARGET_BIN) $(TARGET_CGI)

$(TARGET_BIN): $(C_OBJECT) $(ASM_OBJECT)
	@echo "[LD] Linking $@"
	@$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@
	@echo "[✓] Binary: $@"

$(TARGET_CGI): config.c
	@mkdir -p cgi
	@echo "[LD] Linking CGI: $@"
	@$(CC) $(CFLAGS) $^ -o $@
	@echo "[✓] CGI: $@"

$(C_OBJECT): server.c
	@echo "[CC] Compiling $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(ASM_OBJECT): $(ASM_SOURCE)
	@echo "[AS] Assembling $<"
	@$(AS) $(ASFLAGS) $< -o $@

clean:
	@echo "[CLEAN] Removing build artifacts..."
	@rm -f $(C_OBJECT) $(ASM_OBJECT) $(TARGET_BIN) $(TARGET_CGI) config.o server.log
	@echo "[✓] Clean complete"

distclean: clean
	@echo "[DISTCLEAN] Removing generated files..."
	@rm -f server.log *.pem
	@echo "[✓] Distribution clean complete"

help:
	@echo "HTTPS Server Build System (clang + GNU as)"
	@echo "========================================="
	@echo "Targets:"
	@echo "  make all          - Build release version (default)"
	@echo "  make release      - Build optimized release (x86-64)"
	@echo "  make debug        - Build with debug symbols (-g -O0)"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make distclean    - Remove all generated files including logs"
	@echo "  make help         - Show this help message"
	@echo ""
	@echo "Environment:"
	@echo "  CC         = $(CC)"
	@echo "  AS         = $(AS)"
	@echo "  CFLAGS     = $(CFLAGS)"
	@echo "  ASFLAGS    = $(ASFLAGS)"
	@echo "  LDFLAGS    = $(LDFLAGS)"
	@echo ""
	@echo "Build artifacts:"
	@echo "  Binary: $(TARGET_BIN)"
	@echo "  CGI:    $(TARGET_CGI)"

.PHONY: print-info
print-info:
	@echo "=== Build Configuration ==="
	@echo "Compiler: $(CC)"
	@echo "Version: $$($(CC) --version | head -1)"
	@echo "Assembler: $(AS)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "ASFLAGS: $(ASFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo ""
	@$(CC) --version