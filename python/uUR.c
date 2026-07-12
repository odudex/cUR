// CPython binding for cUR — the desktop/CPython counterpart of the MicroPython
// `uUR.c` at the repo root. It exposes the SAME module surface (module name
// `uUR`) so business logic that does `__import__("uUR")` gets the native
// BC-UR encoder/decoder on CPython (e.g. Raspberry Pi) exactly as it does on
// the ESP32 MicroPython firmware:
//
//   uUR.UR(type, cbor)                       -> .type, .cbor
//   uUR.URDecoder()                          -> receive_part, estimated_percent_complete,
//                                               .result, .state, .expected_part_count,
//                                               .processed_parts_count
//   uUR.UREncoder(ur, max_fragment_len,      -> next_part, is_complete, is_single_part,
//                 first_seq_num=0,              .fountain_encoder(.seq_len())
//                 min_fragment_len=10)
//   uUR.Types.{bytes,psbt}_{from,to}_cbor, bip39_words_from_cbor,
//             output_from_cbor, output_from_cbor_account, + CRYPTO_* tag/type constants
//   uUR.DECODER_*                            -> ur_decoder_state_t mirror constants
//
// Memory model: cUR allocates output strings/buffers with libc malloc on the
// host build (bundled src/sha256/sha256.c; no UR_USE_MBEDTLS_SHA256), so this
// binding frees them with free(). The decoder owns its ur_result_t — we copy it
// into a fresh UR object rather than freeing it here.
//
// Keep this in sync with the MicroPython uUR.c: same class/method/constant
// names and semantics, so the two runtimes present one module contract.

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "src/types/bip39.h"
#include "src/types/bytes_type.h"
#include "src/types/output.h"
#include "src/types/psbt.h"
#include "src/ur.h"
#include "src/ur_decoder.h"
#include "src/ur_encoder.h"

// ---------------------------------------------------------------------------
// UR
// ---------------------------------------------------------------------------

typedef struct {
  PyObject_HEAD ur_t *ur;
} uUR_UR;

static PyTypeObject uUR_URType;

// Build a UR wrapper around a fresh, owned copy of (type, cbor). Used both by
// UR.__init__ and by URDecoder.result. Returns a new reference or NULL (with an
// exception set).
static PyObject *ur_wrap_copy(const char *type, const uint8_t *cbor,
                              size_t cbor_len) {
  uUR_UR *self = PyObject_New(uUR_UR, &uUR_URType);
  if (!self) {
    return NULL;
  }
  self->ur = ur_new(type, cbor, cbor_len);
  if (!self->ur) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_MemoryError, "Failed to create UR object");
    return NULL;
  }
  return (PyObject *)self;
}

static int UR_init(uUR_UR *self, PyObject *args, PyObject *kwds) {
  static char *kwlist[] = {"type", "cbor", NULL};
  const char *type;
  Py_buffer cbor;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "sy*", kwlist, &type, &cbor)) {
    return -1;
  }
  self->ur = ur_new(type, (const uint8_t *)cbor.buf, (size_t)cbor.len);
  PyBuffer_Release(&cbor);
  if (!self->ur) {
    PyErr_SetString(PyExc_MemoryError, "Failed to create UR object");
    return -1;
  }
  return 0;
}

static void UR_dealloc(uUR_UR *self) {
  if (self->ur) {
    ur_free(self->ur);
    self->ur = NULL;
  }
  Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *UR_repr(uUR_UR *self) {
  if (self->ur) {
    return PyUnicode_FromFormat("UR(type='%s')", ur_get_type(self->ur));
  }
  return PyUnicode_FromString("UR(invalid)");
}

static PyObject *UR_get_type(uUR_UR *self, void *closure) {
  (void)closure;
  if (!self->ur) {
    Py_RETURN_NONE;
  }
  return PyUnicode_FromString(ur_get_type(self->ur));
}

// Mirrors the MicroPython binding: .cbor is a (mutable) bytearray of the raw
// CBOR payload.
static PyObject *UR_get_cbor(uUR_UR *self, void *closure) {
  (void)closure;
  if (!self->ur) {
    Py_RETURN_NONE;
  }
  return PyByteArray_FromStringAndSize((const char *)ur_get_cbor(self->ur),
                                       (Py_ssize_t)ur_get_cbor_len(self->ur));
}

static PyGetSetDef UR_getset[] = {
    {"type", (getter)UR_get_type, NULL, "UR type string", NULL},
    {"cbor", (getter)UR_get_cbor, NULL, "Raw CBOR payload (bytearray)", NULL},
    {NULL},
};

static PyTypeObject uUR_URType = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "uUR.UR",
    .tp_basicsize = sizeof(uUR_UR),
    .tp_dealloc = (destructor)UR_dealloc,
    .tp_repr = (reprfunc)UR_repr,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Uniform Resource: a typed CBOR payload.",
    .tp_getset = UR_getset,
    .tp_init = (initproc)UR_init,
    .tp_new = PyType_GenericNew,
};

// ---------------------------------------------------------------------------
// URDecoder
// ---------------------------------------------------------------------------

typedef struct {
  PyObject_HEAD ur_decoder_t *decoder;
} uUR_URDecoder;

static int URDecoder_init(uUR_URDecoder *self, PyObject *args, PyObject *kwds) {
  static char *kwlist[] = {NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "", kwlist)) {
    return -1;
  }
  self->decoder = ur_decoder_new();
  if (!self->decoder) {
    PyErr_SetString(PyExc_MemoryError, "Failed to create URDecoder");
    return -1;
  }
  return 0;
}

static void URDecoder_dealloc(uUR_URDecoder *self) {
  if (self->decoder) {
    ur_decoder_free(self->decoder);
    self->decoder = NULL;
  }
  Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *URDecoder_repr(uUR_URDecoder *self) {
  if (!self->decoder) {
    return PyUnicode_FromString("URDecoder(closed)");
  }
  float progress = ur_decoder_estimated_percent_complete(self->decoder);
  return PyUnicode_FromFormat("URDecoder(state=%d, progress=%d%%)",
                              (int)ur_decoder_get_state(self->decoder),
                              (int)(progress * 100.0f));
}

// receive_part(part) -> int decoder state (one of the module's DECODER_*
// constants). Decode errors are RETURNED, not raised: junk/misread frames are
// expected in a QR scan loop. NOTE: DECODER_OK == 0 is falsy in Python — compare
// the return against the DECODER_* constants, never use it as a boolean.
static PyObject *URDecoder_receive_part(uUR_URDecoder *self, PyObject *part) {
  if (!self->decoder) {
    PyErr_SetString(PyExc_RuntimeError, "URDecoder is closed");
    return NULL;
  }
  const char *part_cstr = PyUnicode_AsUTF8(part);
  if (!part_cstr) {
    return NULL; // TypeError: part must be str
  }
  return PyLong_FromLong(
      (long)ur_decoder_receive_part(self->decoder, part_cstr));
}

// estimated_percent_complete(weight_mixed_frames=False) -> float in [0.0, 1.0].
// weight_mixed_frames=True selects the weighted-mixed-frames method (partial
// credit for fragments still only present inside mixed/XOR'd frames); default
// returns the reference estimate byte-for-byte.
static PyObject *URDecoder_estimated_percent_complete(uUR_URDecoder *self,
                                                      PyObject *args,
                                                      PyObject *kwds) {
  static char *kwlist[] = {"weight_mixed_frames", NULL};
  int weight = 0;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|p", kwlist, &weight)) {
    return NULL;
  }
  if (!self->decoder) {
    return PyFloat_FromDouble(0.0);
  }
  float pct = weight
                  ? ur_decoder_estimated_percent_complete_weighted(self->decoder)
                  : ur_decoder_estimated_percent_complete(self->decoder);
  return PyFloat_FromDouble((double)pct);
}

static PyMethodDef URDecoder_methods[] = {
    {"receive_part", (PyCFunction)URDecoder_receive_part, METH_O,
     "receive_part(part: str) -> int decoder state (a DECODER_* constant)."},
    {"estimated_percent_complete",
     (PyCFunction)URDecoder_estimated_percent_complete,
     METH_VARARGS | METH_KEYWORDS,
     "estimated_percent_complete(weight_mixed_frames=False) -> float in "
     "[0.0, 1.0]."},
    {NULL},
};

static PyObject *URDecoder_get_result(uUR_URDecoder *self, void *closure) {
  (void)closure;
  if (!self->decoder) {
    Py_RETURN_NONE;
  }
  ur_result_t *result = ur_decoder_get_result(self->decoder);
  if (!result) {
    Py_RETURN_NONE;
  }
  // Copy into an owned UR; the ur_result_t stays owned by the decoder.
  return ur_wrap_copy(result->type, result->cbor_data, result->cbor_len);
}

static PyObject *URDecoder_get_state(uUR_URDecoder *self, void *closure) {
  (void)closure;
  // get_state(NULL) yields DECODER_ERR_NULL_POINTER, matching the C API.
  return PyLong_FromLong((long)ur_decoder_get_state(self->decoder));
}

static PyObject *URDecoder_get_expected_part_count(uUR_URDecoder *self,
                                                   void *closure) {
  (void)closure;
  if (!self->decoder) {
    return PyLong_FromLong(0);
  }
  return PyLong_FromSize_t(ur_decoder_expected_part_count(self->decoder));
}

static PyObject *URDecoder_get_processed_parts_count(uUR_URDecoder *self,
                                                     void *closure) {
  (void)closure;
  if (!self->decoder) {
    return PyLong_FromLong(0);
  }
  return PyLong_FromSize_t(ur_decoder_processed_parts_count(self->decoder));
}

static PyGetSetDef URDecoder_getset[] = {
    {"result", (getter)URDecoder_get_result, NULL,
     "Decoded UR once complete, else None.", NULL},
    {"state", (getter)URDecoder_get_state, NULL,
     "Current decoder state (a DECODER_* constant).", NULL},
    {"expected_part_count", (getter)URDecoder_get_expected_part_count, NULL,
     "Total expected parts (0 until known).", NULL},
    {"processed_parts_count", (getter)URDecoder_get_processed_parts_count, NULL,
     "Parts processed so far.", NULL},
    {NULL},
};

static PyTypeObject uUR_URDecoderType = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "uUR.URDecoder",
    .tp_basicsize = sizeof(uUR_URDecoder),
    .tp_dealloc = (destructor)URDecoder_dealloc,
    .tp_repr = (reprfunc)URDecoder_repr,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Multi-part BC-UR fountain decoder.",
    .tp_methods = URDecoder_methods,
    .tp_getset = URDecoder_getset,
    .tp_init = (initproc)URDecoder_init,
    .tp_new = PyType_GenericNew,
};

// ---------------------------------------------------------------------------
// UREncoder + FountainEncoder
// ---------------------------------------------------------------------------

typedef struct {
  PyObject_HEAD ur_encoder_t *encoder;
} uUR_UREncoder;

static PyTypeObject uUR_UREncoderType;
static PyTypeObject uUR_FountainEncoderType;

// FountainEncoder is a thin view exposing seq_len(). It holds a strong
// reference to its parent UREncoder (whose ->encoder it reads at call time), so
// the parent cannot be freed while the view is alive. Unlike the MicroPython
// binding it is NOT cached on the parent: a fresh view is returned per access,
// which avoids a parent<->view reference cycle (no cyclic GC needed) while
// staying behaviorally identical.
typedef struct {
  PyObject_HEAD PyObject *parent; // uUR_UREncoder, strong ref
} uUR_FountainEncoder;

static void FountainEncoder_dealloc(uUR_FountainEncoder *self) {
  Py_XDECREF(self->parent);
  Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *FountainEncoder_seq_len(uUR_FountainEncoder *self,
                                         PyObject *ignored) {
  (void)ignored;
  if (self->parent) {
    uUR_UREncoder *parent = (uUR_UREncoder *)self->parent;
    if (parent->encoder && parent->encoder->fountain_encoder) {
      return PyLong_FromSize_t(ur_encoder_seq_len(parent->encoder));
    }
  }
  return PyLong_FromLong(0);
}

static PyMethodDef FountainEncoder_methods[] = {
    {"seq_len", (PyCFunction)FountainEncoder_seq_len, METH_NOARGS,
     "seq_len() -> number of fountain parts in the sequence."},
    {NULL},
};

static PyTypeObject uUR_FountainEncoderType = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "uUR.FountainEncoder",
    .tp_basicsize = sizeof(uUR_FountainEncoder),
    .tp_dealloc = (destructor)FountainEncoder_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "View over a UREncoder's fountain encoder.",
    .tp_methods = FountainEncoder_methods,
};

static int UREncoder_init(uUR_UREncoder *self, PyObject *args, PyObject *kwds) {
  static char *kwlist[] = {"ur", "max_fragment_len", "first_seq_num",
                           "min_fragment_len", NULL};
  PyObject *ur_obj;
  Py_ssize_t max_fragment_len;
  unsigned int first_seq_num = 0;
  Py_ssize_t min_fragment_len = 10;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "On|In", kwlist, &ur_obj,
                                   &max_fragment_len, &first_seq_num,
                                   &min_fragment_len)) {
    return -1;
  }

  if (!PyObject_TypeCheck(ur_obj, &uUR_URType)) {
    PyErr_SetString(PyExc_TypeError, "First argument must be a UR object");
    return -1;
  }
  uUR_UR *ur = (uUR_UR *)ur_obj;
  if (!ur->ur) {
    PyErr_SetString(PyExc_ValueError, "Invalid UR object");
    return -1;
  }

  // Clamp fragment-length params from Python. 10..2048 covers any real QR
  // payload; values outside drive unbounded fountain allocations.
  if (max_fragment_len < 10 || max_fragment_len > 2048 ||
      min_fragment_len < 1 || min_fragment_len > max_fragment_len) {
    PyErr_SetString(PyExc_ValueError,
                    "fragment_len out of range (min 1..max, max 10..2048)");
    return -1;
  }

  self->encoder = ur_encoder_new(
      ur_get_type(ur->ur), ur_get_cbor(ur->ur), ur_get_cbor_len(ur->ur),
      (size_t)max_fragment_len, (uint32_t)first_seq_num,
      (size_t)min_fragment_len);
  if (!self->encoder) {
    PyErr_SetString(PyExc_MemoryError, "Failed to create UREncoder");
    return -1;
  }
  return 0;
}

static void UREncoder_dealloc(uUR_UREncoder *self) {
  if (self->encoder) {
    ur_encoder_free(self->encoder);
    self->encoder = NULL;
  }
  Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *UREncoder_repr(uUR_UREncoder *self) {
  if (!self->encoder) {
    return PyUnicode_FromString("UREncoder(invalid)");
  }
  return PyUnicode_FromFormat(
      "UREncoder(seq_len=%zu, complete=%s)",
      ur_encoder_seq_len(self->encoder),
      ur_encoder_is_complete(self->encoder) ? "True" : "False");
}

// next_part() -> str: the next UR part string (sequential; wraps to fountain
// parts once past the pure sequence).
static PyObject *UREncoder_next_part(uUR_UREncoder *self, PyObject *ignored) {
  (void)ignored;
  if (!self->encoder) {
    PyErr_SetString(PyExc_RuntimeError, "UREncoder is closed");
    return NULL;
  }
  char *part = NULL;
  bool ok = ur_encoder_next_part(self->encoder, &part);
  if (!ok) {
    free(part);
    PyErr_SetString(PyExc_RuntimeError, "Failed to generate next part");
    return NULL;
  }
  if (!part) {
    PyErr_SetString(PyExc_RuntimeError, "Generated NULL part");
    return NULL;
  }
  PyObject *result = PyUnicode_FromString(part);
  free(part);
  return result;
}

static PyObject *UREncoder_is_complete(uUR_UREncoder *self, PyObject *ignored) {
  (void)ignored;
  if (!self->encoder) {
    Py_RETURN_FALSE;
  }
  return PyBool_FromLong(ur_encoder_is_complete(self->encoder));
}

static PyObject *UREncoder_is_single_part(uUR_UREncoder *self,
                                          PyObject *ignored) {
  (void)ignored;
  if (!self->encoder) {
    Py_RETURN_FALSE;
  }
  return PyBool_FromLong(ur_encoder_is_single_part(self->encoder));
}

static PyMethodDef UREncoder_methods[] = {
    {"next_part", (PyCFunction)UREncoder_next_part, METH_NOARGS,
     "next_part() -> str: the next UR part."},
    {"is_complete", (PyCFunction)UREncoder_is_complete, METH_NOARGS,
     "is_complete() -> bool: True once the pure sequence has been emitted."},
    {"is_single_part", (PyCFunction)UREncoder_is_single_part, METH_NOARGS,
     "is_single_part() -> bool: True if the payload fits one part."},
    {NULL},
};

static PyObject *UREncoder_get_fountain_encoder(uUR_UREncoder *self,
                                                void *closure) {
  (void)closure;
  if (!self->encoder || !self->encoder->fountain_encoder) {
    Py_RETURN_NONE;
  }
  uUR_FountainEncoder *view =
      PyObject_New(uUR_FountainEncoder, &uUR_FountainEncoderType);
  if (!view) {
    return NULL;
  }
  Py_INCREF(self);
  view->parent = (PyObject *)self;
  return (PyObject *)view;
}

static PyGetSetDef UREncoder_getset[] = {
    {"fountain_encoder", (getter)UREncoder_get_fountain_encoder, NULL,
     "FountainEncoder view (seq_len()), or None.", NULL},
    {NULL},
};

static PyTypeObject uUR_UREncoderType = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "uUR.UREncoder",
    .tp_basicsize = sizeof(uUR_UREncoder),
    .tp_dealloc = (destructor)UREncoder_dealloc,
    .tp_repr = (reprfunc)UREncoder_repr,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Multi-part BC-UR fountain encoder.",
    .tp_methods = UREncoder_methods,
    .tp_getset = UREncoder_getset,
    .tp_init = (initproc)UREncoder_init,
    .tp_new = PyType_GenericNew,
};

// ---------------------------------------------------------------------------
// Types namespace: CBOR payload codecs + tag/type constants
// ---------------------------------------------------------------------------

// Fetch a read-only byte buffer from a bytes-like argument. Returns 0 on
// success (caller must PyBuffer_Release), -1 with an exception set.
static int get_read_buffer(PyObject *obj, Py_buffer *buf) {
  return PyObject_GetBuffer(obj, buf, PyBUF_SIMPLE);
}

static PyObject *Types_bytes_from_cbor(PyObject *module, PyObject *arg) {
  (void)module;
  Py_buffer buf;
  if (get_read_buffer(arg, &buf) < 0) {
    return NULL;
  }
  bytes_data_t *bytes = bytes_from_cbor((const uint8_t *)buf.buf, buf.len);
  PyBuffer_Release(&buf);
  if (!bytes) {
    PyErr_SetString(PyExc_ValueError, "Failed to decode Bytes from CBOR");
    return NULL;
  }
  size_t len;
  const uint8_t *data = bytes_get_data(bytes, &len);
  PyObject *result = PyBytes_FromStringAndSize((const char *)data, len);
  bytes_free(bytes);
  return result;
}

static PyObject *Types_bytes_to_cbor(PyObject *module, PyObject *arg) {
  (void)module;
  Py_buffer buf;
  if (get_read_buffer(arg, &buf) < 0) {
    return NULL;
  }
  bytes_data_t *bytes = bytes_new((const uint8_t *)buf.buf, buf.len);
  PyBuffer_Release(&buf);
  if (!bytes) {
    PyErr_SetString(PyExc_MemoryError, "Failed to create Bytes");
    return NULL;
  }
  size_t cbor_len;
  uint8_t *cbor = bytes_to_cbor(bytes, &cbor_len);
  if (!cbor) {
    bytes_free(bytes);
    PyErr_SetString(PyExc_RuntimeError, "Failed to encode Bytes to CBOR");
    return NULL;
  }
  PyObject *result = PyBytes_FromStringAndSize((const char *)cbor, cbor_len);
  free(cbor);
  bytes_free(bytes);
  return result;
}

static PyObject *Types_psbt_from_cbor(PyObject *module, PyObject *arg) {
  (void)module;
  Py_buffer buf;
  if (get_read_buffer(arg, &buf) < 0) {
    return NULL;
  }
  psbt_data_t *psbt = psbt_from_cbor((const uint8_t *)buf.buf, buf.len);
  PyBuffer_Release(&buf);
  if (!psbt) {
    PyErr_SetString(PyExc_ValueError, "Failed to decode PSBT from CBOR");
    return NULL;
  }
  size_t len;
  const uint8_t *data = psbt_get_data(psbt, &len);
  PyObject *result = PyBytes_FromStringAndSize((const char *)data, len);
  psbt_free(psbt);
  return result;
}

static PyObject *Types_psbt_to_cbor(PyObject *module, PyObject *arg) {
  (void)module;
  Py_buffer buf;
  if (get_read_buffer(arg, &buf) < 0) {
    return NULL;
  }
  psbt_data_t *psbt = psbt_new((const uint8_t *)buf.buf, buf.len);
  PyBuffer_Release(&buf);
  if (!psbt) {
    PyErr_SetString(PyExc_MemoryError, "Failed to create PSBT");
    return NULL;
  }
  size_t cbor_len;
  uint8_t *cbor = psbt_to_cbor(psbt, &cbor_len);
  if (!cbor) {
    psbt_free(psbt);
    PyErr_SetString(PyExc_RuntimeError, "Failed to encode PSBT to CBOR");
    return NULL;
  }
  PyObject *result = PyBytes_FromStringAndSize((const char *)cbor, cbor_len);
  free(cbor);
  psbt_free(psbt);
  return result;
}

static PyObject *Types_bip39_words_from_cbor(PyObject *module, PyObject *arg) {
  (void)module;
  Py_buffer buf;
  if (get_read_buffer(arg, &buf) < 0) {
    return NULL;
  }
  bip39_data_t *bip39 = bip39_from_cbor((const uint8_t *)buf.buf, buf.len);
  PyBuffer_Release(&buf);
  if (!bip39) {
    PyErr_SetString(PyExc_ValueError, "Failed to decode BIP39 from CBOR");
    return NULL;
  }
  size_t word_count;
  char **words = bip39_get_words(bip39, &word_count);
  PyObject *list = PyList_New(0);
  if (!list) {
    bip39_free(bip39);
    return NULL;
  }
  for (size_t i = 0; i < word_count; i++) {
    PyObject *word = PyUnicode_FromString(words[i]);
    if (!word || PyList_Append(list, word) < 0) {
      Py_XDECREF(word);
      Py_DECREF(list);
      bip39_free(bip39);
      return NULL;
    }
    Py_DECREF(word);
  }
  bip39_free(bip39);
  return list;
}

static PyObject *Types_output_from_cbor(PyObject *module, PyObject *arg) {
  (void)module;
  Py_buffer buf;
  if (get_read_buffer(arg, &buf) < 0) {
    return NULL;
  }
  output_data_t *output = output_from_cbor((const uint8_t *)buf.buf, buf.len);
  PyBuffer_Release(&buf);
  if (!output) {
    PyErr_SetString(PyExc_ValueError, "Failed to decode Output from CBOR");
    return NULL;
  }
  char *descriptor = output_descriptor(output, true);
  if (!descriptor) {
    output_free(output);
    PyErr_SetString(PyExc_RuntimeError, "Failed to generate output descriptor");
    return NULL;
  }
  PyObject *result = PyUnicode_FromString(descriptor);
  free(descriptor);
  output_free(output);
  return result;
}

static PyObject *Types_output_from_cbor_account(PyObject *module,
                                                PyObject *arg) {
  (void)module;
  Py_buffer buf;
  if (get_read_buffer(arg, &buf) < 0) {
    return NULL;
  }
  char *descriptor =
      output_descriptor_from_cbor_account((const uint8_t *)buf.buf, buf.len);
  PyBuffer_Release(&buf);
  if (!descriptor) {
    PyErr_SetString(PyExc_ValueError,
                    "Failed to extract output descriptor from Account CBOR");
    return NULL;
  }
  PyObject *result = PyUnicode_FromString(descriptor);
  free(descriptor);
  return result;
}

static PyMethodDef Types_methods[] = {
    {"bytes_from_cbor", Types_bytes_from_cbor, METH_O,
     "bytes_from_cbor(cbor: bytes) -> bytes"},
    {"bytes_to_cbor", Types_bytes_to_cbor, METH_O,
     "bytes_to_cbor(data: bytes) -> bytes"},
    {"psbt_from_cbor", Types_psbt_from_cbor, METH_O,
     "psbt_from_cbor(cbor: bytes) -> bytes"},
    {"psbt_to_cbor", Types_psbt_to_cbor, METH_O,
     "psbt_to_cbor(data: bytes) -> bytes"},
    {"bip39_words_from_cbor", Types_bip39_words_from_cbor, METH_O,
     "bip39_words_from_cbor(cbor: bytes) -> list[str]"},
    {"output_from_cbor", Types_output_from_cbor, METH_O,
     "output_from_cbor(cbor: bytes) -> str descriptor"},
    {"output_from_cbor_account", Types_output_from_cbor_account, METH_O,
     "output_from_cbor_account(cbor: bytes) -> str descriptor"},
    {NULL},
};

static struct PyModuleDef Types_moduledef = {
    PyModuleDef_HEAD_INIT, "uUR.Types",
    "BC-UR typed CBOR payload codecs.", -1, Types_methods,
};

static PyObject *build_types_module(void) {
  PyObject *m = PyModule_Create(&Types_moduledef);
  if (!m) {
    return NULL;
  }
  if (PyModule_AddIntConstant(m, "CRYPTO_PSBT_TAG", CRYPTO_PSBT_TAG) < 0 ||
      PyModule_AddIntConstant(m, "CRYPTO_BIP39_TAG", CRYPTO_BIP39_TAG) < 0 ||
      PyModule_AddIntConstant(m, "CRYPTO_ACCOUNT_TAG", CRYPTO_ACCOUNT_TAG) < 0 ||
      PyModule_AddIntConstant(m, "CRYPTO_OUTPUT_TAG", CRYPTO_OUTPUT_TAG) < 0 ||
      PyModule_AddStringConstant(m, "CRYPTO_PSBT_TYPE", "crypto-psbt") < 0 ||
      PyModule_AddStringConstant(m, "CRYPTO_BIP39_TYPE", "crypto-bip39") < 0 ||
      PyModule_AddStringConstant(m, "CRYPTO_OUTPUT_TYPE", "crypto-output") < 0 ||
      PyModule_AddStringConstant(m, "CRYPTO_ACCOUNT_TYPE", "crypto-account") <
          0) {
    Py_DECREF(m);
    return NULL;
  }
  return m;
}

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

static int add_decoder_state_constants(PyObject *m) {
  return (PyModule_AddIntConstant(m, "DECODER_OK", UR_DECODER_OK) == 0 &&
          PyModule_AddIntConstant(m, "DECODER_PROCESSING",
                                  UR_DECODER_PROCESSING) == 0 &&
          PyModule_AddIntConstant(m, "DECODER_NO_RESULT",
                                  UR_DECODER_NO_RESULT) == 0 &&
          PyModule_AddIntConstant(m, "DECODER_ERR_INVALID_SCHEME",
                                  UR_DECODER_ERROR_INVALID_SCHEME) == 0 &&
          PyModule_AddIntConstant(m, "DECODER_ERR_INVALID_TYPE",
                                  UR_DECODER_ERROR_INVALID_TYPE) == 0 &&
          PyModule_AddIntConstant(m, "DECODER_ERR_INVALID_PATH_LENGTH",
                                  UR_DECODER_ERROR_INVALID_PATH_LENGTH) == 0 &&
          PyModule_AddIntConstant(
              m, "DECODER_ERR_INVALID_SEQUENCE_COMPONENT",
              UR_DECODER_ERROR_INVALID_SEQUENCE_COMPONENT) == 0 &&
          PyModule_AddIntConstant(m, "DECODER_ERR_INVALID_FRAGMENT",
                                  UR_DECODER_ERROR_INVALID_FRAGMENT) == 0 &&
          PyModule_AddIntConstant(m, "DECODER_ERR_INVALID_PART",
                                  UR_DECODER_ERROR_INVALID_PART) == 0 &&
          PyModule_AddIntConstant(m, "DECODER_ERR_INVALID_CHECKSUM",
                                  UR_DECODER_ERROR_INVALID_CHECKSUM) == 0 &&
          PyModule_AddIntConstant(m, "DECODER_ERR_MEMORY",
                                  UR_DECODER_ERROR_MEMORY) == 0 &&
          PyModule_AddIntConstant(m, "DECODER_ERR_NULL_POINTER",
                                  UR_DECODER_ERROR_NULL_POINTER) == 0)
             ? 0
             : -1;
}

static struct PyModuleDef uUR_moduledef = {
    PyModuleDef_HEAD_INIT,
    "uUR",
    "Native BC-UR (Uniform Resources) encoder/decoder — CPython counterpart of "
    "the MicroPython uUR module.",
    -1,
    NULL, // no top-level functions; codecs live under uUR.Types
};

PyMODINIT_FUNC PyInit_uUR(void) {
  if (PyType_Ready(&uUR_URType) < 0 ||
      PyType_Ready(&uUR_URDecoderType) < 0 ||
      PyType_Ready(&uUR_UREncoderType) < 0 ||
      PyType_Ready(&uUR_FountainEncoderType) < 0) {
    return NULL;
  }

  PyObject *m = PyModule_Create(&uUR_moduledef);
  if (!m) {
    return NULL;
  }

  Py_INCREF(&uUR_URType);
  Py_INCREF(&uUR_URDecoderType);
  Py_INCREF(&uUR_UREncoderType);
  if (PyModule_AddObject(m, "UR", (PyObject *)&uUR_URType) < 0 ||
      PyModule_AddObject(m, "URDecoder", (PyObject *)&uUR_URDecoderType) < 0 ||
      PyModule_AddObject(m, "UREncoder", (PyObject *)&uUR_UREncoderType) < 0) {
    Py_DECREF(&uUR_URType);
    Py_DECREF(&uUR_URDecoderType);
    Py_DECREF(&uUR_UREncoderType);
    Py_DECREF(m);
    return NULL;
  }

  PyObject *types = build_types_module();
  if (!types || PyModule_AddObject(m, "Types", types) < 0) {
    Py_XDECREF(types);
    Py_DECREF(m);
    return NULL;
  }

  if (add_decoder_state_constants(m) < 0) {
    Py_DECREF(m);
    return NULL;
  }

  return m;
}
