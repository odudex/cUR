#ifndef UR_ATTRIBUTES_H
#define UR_ATTRIBUTES_H

/*
 * UR_WARN_UNUSED_RESULT - the compiler warns at any call site that discards
 * this function's return value.
 *
 * Applied to everything that reports failure through its return: allocators,
 * container inserts, parsers. Several defects found by review were a single
 * ignored bool - a queue insert whose failure silently dropped a recovered
 * fragment, an append whose failure left a buffer unterminated for strlen(),
 * a key add whose failure emitted a multisig short one cosigner. None of
 * those needed a reader to spot; the compiler can point at every one.
 *
 * NOTE: under GCC a `(void)` cast does NOT suppress this - unlike an unused
 * variable. To ignore a result deliberately, consume it:
 *
 *     if (best_effort_call()) {
 *       // nothing to do either way
 *     }
 *
 * and say in a comment why failing is acceptable there.
 */
#if defined(__GNUC__) || defined(__clang__)
#define UR_WARN_UNUSED_RESULT __attribute__((__warn_unused_result__))
#else
#define UR_WARN_UNUSED_RESULT
#endif

#endif /* UR_ATTRIBUTES_H */
