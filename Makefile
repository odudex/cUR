# Root Makefile for UR C implementation testing

CC = gcc
CFLAGS = -Wall -Wextra -Werror=unused-result -std=c99 -g
# Library objects only: catches accidental float→double promotion in the
# float-typed progress code (test files stay exempt — printf varargs
# promote floats by design).
LIB_WARNFLAGS = -Wdouble-promotion
LDFLAGS =
INCLUDES = -Isrc
SRCDIR = src
OBJDIR = src/obj
UR_CRC32_SLICE_BY_8 ?= 0

# DEBUG=1 switches to -O0 with AddressSanitizer + UndefinedBehaviorSanitizer.
# Requires a full rebuild when toggling (sanitized and non-sanitized objects
# cannot link together): `make clean && make DEBUG=1 test`.
ifeq ($(DEBUG),1)
  CFLAGS  += -O0 -fno-omit-frame-pointer -fsanitize=address,undefined
  LDFLAGS += -fsanitize=address,undefined
else
  CFLAGS  += -O2
endif

# Defaults to the small CRC table; set UR_CRC32_SLICE_BY_8=1 for slice-by-8.
ifeq ($(UR_CRC32_SLICE_BY_8),1)
  CFLAGS += -DUR_CRC32_SLICE_BY_8
endif

# Source files (exclude test files)
SOURCES = utils.c bytewords.c fountain_decoder.c fountain_encoder.c fountain_utils.c crc32.c ur_decoder.c ur_encoder.c ur.c sha256/sha256.c \
          types/byte_buffer.c types/cbor_data.c types/cbor_encoder.c types/cbor_decoder.c types/registry.c types/bytes_type.c types/psbt.c types/bip39.c \
          types/keypath.c types/hd_key.c types/multi_key.c types/output.c

# Object files
OBJECTS = $(SOURCES:%.c=$(OBJDIR)/%.o)

# Target library
TARGET = $(SRCDIR)/libur.a

# Test utilities + harness — linked into every test binary.
TEST_UTILS_OBJECT = tests/test_utils.o
TEST_HARNESS_OBJECT = tests/test_harness.o
TEST_SUPPORT_OBJECTS = $(TEST_UTILS_OBJECT) $(TEST_HARNESS_OBJECT)

# Test program stems — each matches a tests/test_ur_<stem>.c source.
# The user-facing target name is `test-<stem-with-dashes>` (e.g. test-bytes-decoder).
TEST_STEMS = bytes_decoder bytes_encoder output_decoder output_encoder \
             PSBT_decoder PSBT_encoder bip39_decoder \
             account_descriptor_decoder output_descriptor_roundtrip \
             weighted_progress gaussian negative envelope_api

TEST_BINS = $(TEST_STEMS:%=tests/test_ur_%)
TEST_TARGETS = $(foreach s,$(TEST_STEMS),test-$(subst _,-,$(s)))

CROSS_REDUCTION_BUILDDIR = build/cross-reduction
CROSS_REDUCTION_OBJDIR = $(CROSS_REDUCTION_BUILDDIR)/obj
CROSS_REDUCTION_OBJECTS = $(SOURCES:%.c=$(CROSS_REDUCTION_OBJDIR)/%.o)
CROSS_REDUCTION_DEPS = $(CROSS_REDUCTION_OBJECTS:.o=.d)
CROSS_REDUCTION_LIBRARY = $(CROSS_REDUCTION_BUILDDIR)/libur.a
CROSS_REDUCTION_TEST = $(CROSS_REDUCTION_BUILDDIR)/test_ur_gaussian

.PHONY: all clean test check check-warn-unused-result \
        check-cross-reduction coverage $(TEST_TARGETS)

all: $(TARGET)

# Run all tests. `check` is a GNU-convention alias.
test: check-warn-unused-result check-cross-reduction $(TEST_TARGETS)
	@echo "All tests passed."

check: test

# These calls must fail to compile. They exercise forwarding wrappers and
# allocator declarations separately so one annotation cannot mask the other.
#
# Each probe is compiled twice, because "the compile failed" on its own proves
# nothing: a broken include path, a renamed header or a mistyped -D name (which
# trips the probe's own #error) would make the guard report success while
# testing nothing. So the probe must first build cleanly with the -Werror flag
# dropped, and then fail with a diagnostic that names warn_unused_result.
WUR_PROBE_SRC = tests/warn_unused_result_probe.c
WUR_PROBE_DEFS = UR_WUR_PROBE_APPEND_BYTE UR_WUR_PROBE_CBOR_ALLOCATOR
WUR_PROBE_CFLAGS = $(filter-out -Werror=unused-result,$(CFLAGS)) -Wno-unused-result

check-warn-unused-result:
	@for probe in $(WUR_PROBE_DEFS); do \
		if ! $(CC) $(WUR_PROBE_CFLAGS) -O0 $(INCLUDES) -D$$probe \
			-c $(WUR_PROBE_SRC) -o /dev/null; then \
			echo "$@: $$probe does not compile at all - the guard is testing nothing"; \
			exit 1; \
		fi; \
		out=`$(CC) $(CFLAGS) -O0 $(INCLUDES) -D$$probe \
			-c $(WUR_PROBE_SRC) -o /dev/null 2>&1`; \
		if [ $$? -eq 0 ]; then \
			echo "$@: $$probe compiled - the warn_unused_result annotation is missing"; \
			exit 1; \
		fi; \
		case "$$out" in \
			*warn_unused_result*) ;; \
			*) echo "$@: $$probe failed for an unrelated reason:"; \
			   echo "$$out"; exit 1;; \
		esac; \
	done
	@echo "warn_unused_result annotations enforced."

# Build the decoder a second time with the opt-in mixed-against-mixed path.
# Keeping it in a separate object tree avoids stale flag-dependent objects and
# lets the ordinary `make test` exercise both decoder configurations.
check-cross-reduction: $(CROSS_REDUCTION_TEST)
	./$(CROSS_REDUCTION_TEST)

# Rebuilds the library with gcov instrumentation, runs every test, and
# produces coverage_html/. Requires gcov (from gcc) and lcov.
coverage:
	./scripts/coverage.sh

$(TARGET): $(OBJECTS)
	ar rcs $@ $^

$(CROSS_REDUCTION_LIBRARY): $(CROSS_REDUCTION_OBJECTS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LIB_WARNFLAGS) $(INCLUDES) -c $< -o $@

$(CROSS_REDUCTION_OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DENABLE_CROSS_REDUCTION -MMD -MP \
		$(LIB_WARNFLAGS) $(INCLUDES) \
		-c $< -o $@

$(CROSS_REDUCTION_TEST): tests/test_ur_gaussian.c \
                         $(CROSS_REDUCTION_LIBRARY)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DENABLE_CROSS_REDUCTION $(INCLUDES) $< \
		$(CROSS_REDUCTION_LIBRARY) $(LDFLAGS) -o $@

$(TEST_UTILS_OBJECT): tests/test_utils.c tests/test_utils.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TEST_HARNESS_OBJECT): tests/test_harness.c tests/test_harness.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Pattern rule: build each test binary.
tests/test_ur_%: tests/test_ur_%.c $(TEST_SUPPORT_OBJECTS) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(TEST_SUPPORT_OBJECTS) -L$(SRCDIR) -lur $(LDFLAGS) -o $@

# Generate a `test-<name>` phony target per stem that runs the corresponding binary.
define TEST_RUN_RULE
test-$(subst _,-,$(1)): tests/test_ur_$(1)
	./tests/test_ur_$(1)
endef
$(foreach s,$(TEST_STEMS),$(eval $(call TEST_RUN_RULE,$(s))))

clean:
	rm -rf $(OBJDIR) $(TARGET) $(TEST_SUPPORT_OBJECTS) $(TEST_BINS) \
		$(CROSS_REDUCTION_BUILDDIR)

# Dependencies
$(OBJDIR)/utils.o: $(SRCDIR)/utils.c $(SRCDIR)/utils.h $(SRCDIR)/xor_internal.h
$(OBJDIR)/crc32.o: $(SRCDIR)/crc32.c $(SRCDIR)/crc32.h $(SRCDIR)/crc32_slice_table.h
$(OBJDIR)/bytewords.o: $(SRCDIR)/bytewords.c $(SRCDIR)/bytewords.h $(SRCDIR)/utils.h $(SRCDIR)/crc32.h
$(OBJDIR)/fountain_utils.o: $(SRCDIR)/fountain_utils.c $(SRCDIR)/fountain_utils.h $(SRCDIR)/fountain_decoder.h $(SRCDIR)/fountain_types.h $(SRCDIR)/utils.h $(SRCDIR)/sha256/sha256_compat.h $(SRCDIR)/sha256/sha256.h
$(OBJDIR)/fountain_decoder.o: $(SRCDIR)/fountain_decoder.c $(SRCDIR)/fountain_decoder.h $(SRCDIR)/fountain_utils.h $(SRCDIR)/fountain_types.h $(SRCDIR)/crc32.h $(SRCDIR)/utils.h $(SRCDIR)/xor_internal.h
$(OBJDIR)/fountain_encoder.o: $(SRCDIR)/fountain_encoder.c $(SRCDIR)/fountain_encoder.h $(SRCDIR)/fountain_decoder.h $(SRCDIR)/fountain_utils.h $(SRCDIR)/fountain_types.h $(SRCDIR)/crc32.h $(SRCDIR)/utils.h $(SRCDIR)/xor_internal.h
$(OBJDIR)/types/byte_buffer.o: $(SRCDIR)/types/byte_buffer.c $(SRCDIR)/types/byte_buffer.h $(SRCDIR)/sha256/sha256_compat.h $(SRCDIR)/sha256/sha256.h $(SRCDIR)/utils.h
$(OBJDIR)/types/output.o: $(SRCDIR)/types/output.c $(SRCDIR)/types/output.h $(SRCDIR)/types/byte_buffer.h $(SRCDIR)/sha256/sha256_compat.h $(SRCDIR)/utils.h
$(OBJDIR)/ur_decoder.o: $(SRCDIR)/ur_decoder.c $(SRCDIR)/ur_decoder.h $(SRCDIR)/fountain_decoder.h $(SRCDIR)/bytewords.h $(SRCDIR)/utils.h
$(OBJDIR)/ur_encoder.o: $(SRCDIR)/ur_encoder.c $(SRCDIR)/ur_encoder.h $(SRCDIR)/fountain_encoder.h $(SRCDIR)/bytewords.h $(SRCDIR)/utils.h
$(OBJDIR)/ur.o: $(SRCDIR)/ur.c $(SRCDIR)/ur.h $(SRCDIR)/ur_decoder.h $(SRCDIR)/utils.h
$(OBJDIR)/sha256/sha256.o: $(SRCDIR)/sha256/sha256.c $(SRCDIR)/sha256/sha256.h

-include $(CROSS_REDUCTION_DEPS)
