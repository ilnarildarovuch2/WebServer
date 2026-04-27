CC         = clang
AS         = as
ISPC       = ispc
CFLAGS     = -w -O2 -std=c99 -D_GNU_SOURCE -fPIC
ASFLAGS    = --64
ISPC_FLAGS = -O2 --target=avx
LDFLAGS    = -lpthread -lmbedtls -lmbedcrypto -lmbedx509 -no-pie
DEBUG_CFLAGS = -g -O0 -DDEBUG

TARGET_BIN = server
TARGET_CGI = cgi/config.cgi
SOURCES    = server.c
ASM_SOURCE = server.asm
ASM_OBJECT = server_asm.o
C_OBJECT   = server.o
ISPC_OBJECT = server_optimize.o

.PHONY: all clean debug release help

all: release

release: CFLAGS += -DNDEBUG
release: $(TARGET_BIN) $(TARGET_CGI)

debug: CFLAGS += $(DEBUG_CFLAGS)
debug: $(TARGET_BIN) $(TARGET_CGI)

$(TARGET_BIN): $(C_OBJECT) $(ASM_OBJECT) $(ISPC_OBJECT)
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

$(ISPC_OBJECT): server.ispc
	@echo "[ISPC] Compiling $<"
	@$(ISPC) $(ISPC_FLAGS) -o $@ $<

clean:
	@echo "[CLEAN] Removing build artifacts..."
	@rm -f $(C_OBJECT) $(ASM_OBJECT) $(ISPC_OBJECT) $(TARGET_BIN) $(TARGET_CGI) config.o server.log
	@echo "[✓] Clean complete"

distclean: clean
	@echo "[DISTCLEAN] Removing generated files..."
	@rm -f server.log *.pem
	@echo "[✓] Distribution clean complete"

help:
	@echo "HTTPS Server Build System (clang + GNU as + ISPC)"
	@echo "================================================"
	@echo "Targets:"
	@echo "  make all          - Build release version (default)"
	@echo "  make release      - Build optimized release (x86-64 + SIMD)"
	@echo "  make debug        - Build with debug symbols (-g -O0)"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make distclean    - Remove all generated files including logs"
	@echo "  make help         - Show this help message"
	@echo ""
	@echo "Environment:"
	@echo "  CC         = $(CC)"
	@echo "  AS         = $(AS)"
	@echo "  ISPC       = $(ISPC)"
	@echo "  CFLAGS     = $(CFLAGS)"
	@echo "  ISPC_FLAGS = $(ISPC_FLAGS)"
	@echo "  LDFLAGS    = $(LDFLAGS)"
