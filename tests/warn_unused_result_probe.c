#include "types/byte_buffer.h"
#include "types/cbor_data.h"

#if defined(UR_WUR_PROBE_APPEND_BYTE)
void probe_warn_unused_append_byte(byte_buffer_t *buffer) {
  byte_buffer_append_byte(buffer, 0);
}
#elif defined(UR_WUR_PROBE_CBOR_ALLOCATOR)
void probe_warn_unused_cbor_allocator(void) { cbor_value_new_array(); }
#else
#error "select a warn_unused_result probe"
#endif
