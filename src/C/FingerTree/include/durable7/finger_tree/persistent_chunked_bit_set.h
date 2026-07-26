/*
 * A persistent bit set over the nonnegative int32 domain, stored as chunked words.
 *
 * Ascending nonzero 64-bit words are held in a measured tree whose measure caches population
 * counts, which turns rank and select into a descent rather than a scan. Runs of zeros cost nothing
 * because absent words are not represented. Every operation returns a new version and leaves its
 * inputs valid, sharing unchanged structure, so an edit copies a path rather than the whole
 * collection.
 */

#ifndef DURABLE7_FINGER_TREE_C_PERSISTENT_CHUNKED_BIT_SET_H
#define DURABLE7_FINGER_TREE_C_PERSISTENT_CHUNKED_BIT_SET_H

#include <durable7/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ft_chunked_bit_set_context;

typedef struct ft_persistent_chunked_bit_set {
    ft_tree chunks;
    struct ft_chunked_bit_set_context* context;
} ft_persistent_chunked_bit_set;

/* Immutable root-plus-population-rank gap cursor over present bits. */
typedef struct ft_persistent_chunked_bit_set_cursor {
    ft_persistent_chunked_bit_set set;
    uint64_t position;
} ft_persistent_chunked_bit_set_cursor;

typedef void (*ft_chunked_bit_set_visit_fn)(int32_t bit_index, void* context);

/* Initializes the bit set in place. */
ft_status ft_persistent_chunked_bit_set_init(ft_persistent_chunked_bit_set* set);
/* Initializes a bit set holding the given set bits, built in bulk rather than by repeated
 * insertion. */
ft_status ft_persistent_chunked_bit_set_from_array(
    ft_persistent_chunked_bit_set* set,
    const int32_t* bit_indexes,
    size_t count);
/* Initializes a second handle on the same bit set version, taking a reference rather than
 * copying. */
ft_status ft_persistent_chunked_bit_set_copy(
    const ft_persistent_chunked_bit_set* source,
    ft_persistent_chunked_bit_set* destination);
/* Relocates an initialized bit set into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_persistent_chunked_bit_set_move(
    ft_persistent_chunked_bit_set* destination,
    ft_persistent_chunked_bit_set* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_persistent_chunked_bit_set_dispose(ft_persistent_chunked_bit_set* set);

/* Whether the bit set holds no set bits. */
bool ft_persistent_chunked_bit_set_empty(const ft_persistent_chunked_bit_set* set);
/* Number of set bits in the bit set. */
uint64_t ft_persistent_chunked_bit_set_count(const ft_persistent_chunked_bit_set* set);
/* How many chunks are represented. */
size_t ft_persistent_chunked_bit_set_chunk_count(const ft_persistent_chunked_bit_set* set);
/* Whether the set bit is present. */
bool ft_persistent_chunked_bit_set_contains(
    const ft_persistent_chunked_bit_set* set,
    int32_t bit_index);

/* Produces a bit set containing the given set bit. */
ft_status ft_persistent_chunked_bit_set_add(
    const ft_persistent_chunked_bit_set* set,
    int32_t bit_index,
    ft_persistent_chunked_bit_set* result);
/* Produces a bit set without that set bit. */
ft_status ft_persistent_chunked_bit_set_remove(
    const ft_persistent_chunked_bit_set* set,
    int32_t bit_index,
    ft_persistent_chunked_bit_set* result);
/* How many bits are set at or below the given index. Cached chunk populations make this a descent
 * rather than a scan. */
ft_status ft_persistent_chunked_bit_set_rank(
    const ft_persistent_chunked_bit_set* set,
    int32_t bit_index,
    uint64_t* rank);
/* Reads the index of the nth set bit, reporting whether that many are set. */
ft_status ft_persistent_chunked_bit_set_try_select(
    const ft_persistent_chunked_bit_set* set,
    uint64_t rank,
    bool* found,
    int32_t* bit_index);
/* The index of the nth set bit, counting from zero. */
ft_status ft_persistent_chunked_bit_set_select(
    const ft_persistent_chunked_bit_set* set,
    uint64_t rank,
    int32_t* bit_index);

/* Produces the set bits of both bit sets. Subtrees the operands already share are adopted whole
 * rather than re-entered. */
ft_status ft_persistent_chunked_bit_set_union(
    const ft_persistent_chunked_bit_set* left,
    const ft_persistent_chunked_bit_set* right,
    ft_persistent_chunked_bit_set* result);
/* Produces the set bits present in both bit sets. */
ft_status ft_persistent_chunked_bit_set_intersect(
    const ft_persistent_chunked_bit_set* left,
    const ft_persistent_chunked_bit_set* right,
    ft_persistent_chunked_bit_set* result);
/* Produces this bit set's set bits that are absent from the other. */
ft_status ft_persistent_chunked_bit_set_except(
    const ft_persistent_chunked_bit_set* left,
    const ft_persistent_chunked_bit_set* right,
    ft_persistent_chunked_bit_set* result);
/* Produces the set bits present in exactly one of the two bit sets. */
ft_status ft_persistent_chunked_bit_set_symmetric_except(
    const ft_persistent_chunked_bit_set* left,
    const ft_persistent_chunked_bit_set* right,
    ft_persistent_chunked_bit_set* result);
/* Produces an empty bit set retaining the same policies. */
ft_status ft_persistent_chunked_bit_set_clear(
    const ft_persistent_chunked_bit_set* set,
    ft_persistent_chunked_bit_set* result);
/* Calls the visitor once per set bit, in the bit set's own order. */
ft_status ft_persistent_chunked_bit_set_visit(
    const ft_persistent_chunked_bit_set* set,
    ft_chunked_bit_set_visit_fn visitor,
    void* context);

/* Checks the bit set's structural invariants. For tests and diagnostics. */
bool ft_persistent_chunked_bit_set_debug_validate(
    const ft_persistent_chunked_bit_set* set);
/* Whether both handles reference the same root node. */
bool ft_persistent_chunked_bit_set_debug_shares_root(
    const ft_persistent_chunked_bit_set* left,
    const ft_persistent_chunked_bit_set* right);

/* Initializes a cursor at the given gap of the bit set. */
ft_status ft_persistent_chunked_bit_set_get_cursor(
    const ft_persistent_chunked_bit_set* set,
    uint64_t position,
    ft_persistent_chunked_bit_set_cursor* result);
/* Initializes a cursor before the first set bit at or after the probe. */
ft_status ft_persistent_chunked_bit_set_get_cursor_at_or_after(
    const ft_persistent_chunked_bit_set* set,
    int32_t bit_index,
    ft_persistent_chunked_bit_set_cursor* result);
/* Initializes a cursor at the set bit, reporting whether it was actually present; on a miss the
 * cursor sits at the insertion point. */
ft_status ft_persistent_chunked_bit_set_get_cursor_at_item(
    const ft_persistent_chunked_bit_set* set,
    int32_t bit_index,
    bool* found,
    ft_persistent_chunked_bit_set_cursor* result);
/* Initializes a second cursor at the same position on the same version. */
ft_status ft_persistent_chunked_bit_set_cursor_copy(
    const ft_persistent_chunked_bit_set_cursor* source,
    ft_persistent_chunked_bit_set_cursor* destination);
/* Relocates an initialized cursor into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_persistent_chunked_bit_set_cursor_move(
    ft_persistent_chunked_bit_set_cursor* destination,
    ft_persistent_chunked_bit_set_cursor* source);
/* Releases the cursor's reference on its version. */
void ft_persistent_chunked_bit_set_cursor_dispose(
    ft_persistent_chunked_bit_set_cursor* cursor);
/* Whether the cursor is initialized and still usable. */
bool ft_persistent_chunked_bit_set_cursor_valid(
    const ft_persistent_chunked_bit_set_cursor* cursor);
/* Whether the bit set version the cursor is positioned in holds no set bits. */
bool ft_persistent_chunked_bit_set_cursor_empty(
    const ft_persistent_chunked_bit_set_cursor* cursor);
/* Number of set bits in the bit set version the cursor is positioned in. */
uint64_t ft_persistent_chunked_bit_set_cursor_count(
    const ft_persistent_chunked_bit_set_cursor* cursor);
/* The cursor's gap position. */
uint64_t ft_persistent_chunked_bit_set_cursor_position(
    const ft_persistent_chunked_bit_set_cursor* cursor);
/* Whether the gap precedes the first set bit. */
ft_status ft_persistent_chunked_bit_set_cursor_is_at_start(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    bool* result);
/* Whether the gap follows the last set bit. */
ft_status ft_persistent_chunked_bit_set_cursor_is_at_end(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    bool* result);
/* Reads the set bit immediately before the gap, reporting whether one exists. */
ft_status ft_persistent_chunked_bit_set_cursor_try_peek_previous(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    bool* found,
    int32_t* bit_index);
/* Reads the set bit immediately after the gap, reporting whether one exists. */
ft_status ft_persistent_chunked_bit_set_cursor_try_peek_next(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    bool* found,
    int32_t* bit_index);
/* Moves the cursor one position earlier. */
ft_status ft_persistent_chunked_bit_set_cursor_move_previous(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    ft_persistent_chunked_bit_set_cursor* result);
/* Moves the cursor one position later. */
ft_status ft_persistent_chunked_bit_set_cursor_move_next(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    ft_persistent_chunked_bit_set_cursor* result);
/* Moves the cursor to the given rank within the same version. */
ft_status ft_persistent_chunked_bit_set_cursor_seek_rank(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    uint64_t position,
    ft_persistent_chunked_bit_set_cursor* result);
/* Produces a bit set containing the given set bit. */
ft_status ft_persistent_chunked_bit_set_cursor_add(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    int32_t bit_index,
    ft_persistent_chunked_bit_set_cursor* result);
/* Removes the set bit before the gap, producing a new version the cursor is then positioned in. */
ft_status ft_persistent_chunked_bit_set_cursor_delete_previous(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    ft_persistent_chunked_bit_set_cursor* result);
/* Removes the set bit after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_persistent_chunked_bit_set_cursor_delete_next(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    ft_persistent_chunked_bit_set_cursor* result);
/* Initializes a handle on the bit set version this cursor is positioned in. */
ft_status ft_persistent_chunked_bit_set_cursor_snapshot(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    ft_persistent_chunked_bit_set* result);

#ifdef __cplusplus
}
#endif

#endif
