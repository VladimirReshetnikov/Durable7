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

ft_status ft_persistent_chunked_bit_set_init(ft_persistent_chunked_bit_set* set);
ft_status ft_persistent_chunked_bit_set_from_array(
    ft_persistent_chunked_bit_set* set,
    const int32_t* bit_indexes,
    size_t count);
ft_status ft_persistent_chunked_bit_set_copy(
    const ft_persistent_chunked_bit_set* source,
    ft_persistent_chunked_bit_set* destination);
void ft_persistent_chunked_bit_set_move(
    ft_persistent_chunked_bit_set* destination,
    ft_persistent_chunked_bit_set* source);
void ft_persistent_chunked_bit_set_dispose(ft_persistent_chunked_bit_set* set);

bool ft_persistent_chunked_bit_set_empty(const ft_persistent_chunked_bit_set* set);
uint64_t ft_persistent_chunked_bit_set_count(const ft_persistent_chunked_bit_set* set);
size_t ft_persistent_chunked_bit_set_chunk_count(const ft_persistent_chunked_bit_set* set);
bool ft_persistent_chunked_bit_set_contains(
    const ft_persistent_chunked_bit_set* set,
    int32_t bit_index);

ft_status ft_persistent_chunked_bit_set_add(
    const ft_persistent_chunked_bit_set* set,
    int32_t bit_index,
    ft_persistent_chunked_bit_set* result);
ft_status ft_persistent_chunked_bit_set_remove(
    const ft_persistent_chunked_bit_set* set,
    int32_t bit_index,
    ft_persistent_chunked_bit_set* result);
ft_status ft_persistent_chunked_bit_set_rank(
    const ft_persistent_chunked_bit_set* set,
    int32_t bit_index,
    uint64_t* rank);
ft_status ft_persistent_chunked_bit_set_try_select(
    const ft_persistent_chunked_bit_set* set,
    uint64_t rank,
    bool* found,
    int32_t* bit_index);
ft_status ft_persistent_chunked_bit_set_select(
    const ft_persistent_chunked_bit_set* set,
    uint64_t rank,
    int32_t* bit_index);

ft_status ft_persistent_chunked_bit_set_union(
    const ft_persistent_chunked_bit_set* left,
    const ft_persistent_chunked_bit_set* right,
    ft_persistent_chunked_bit_set* result);
ft_status ft_persistent_chunked_bit_set_intersect(
    const ft_persistent_chunked_bit_set* left,
    const ft_persistent_chunked_bit_set* right,
    ft_persistent_chunked_bit_set* result);
ft_status ft_persistent_chunked_bit_set_except(
    const ft_persistent_chunked_bit_set* left,
    const ft_persistent_chunked_bit_set* right,
    ft_persistent_chunked_bit_set* result);
ft_status ft_persistent_chunked_bit_set_symmetric_except(
    const ft_persistent_chunked_bit_set* left,
    const ft_persistent_chunked_bit_set* right,
    ft_persistent_chunked_bit_set* result);
ft_status ft_persistent_chunked_bit_set_clear(
    const ft_persistent_chunked_bit_set* set,
    ft_persistent_chunked_bit_set* result);
ft_status ft_persistent_chunked_bit_set_visit(
    const ft_persistent_chunked_bit_set* set,
    ft_chunked_bit_set_visit_fn visitor,
    void* context);

bool ft_persistent_chunked_bit_set_debug_validate(
    const ft_persistent_chunked_bit_set* set);
bool ft_persistent_chunked_bit_set_debug_shares_root(
    const ft_persistent_chunked_bit_set* left,
    const ft_persistent_chunked_bit_set* right);

ft_status ft_persistent_chunked_bit_set_get_cursor(
    const ft_persistent_chunked_bit_set* set,
    uint64_t position,
    ft_persistent_chunked_bit_set_cursor* result);
ft_status ft_persistent_chunked_bit_set_get_cursor_at_or_after(
    const ft_persistent_chunked_bit_set* set,
    int32_t bit_index,
    ft_persistent_chunked_bit_set_cursor* result);
ft_status ft_persistent_chunked_bit_set_get_cursor_at_item(
    const ft_persistent_chunked_bit_set* set,
    int32_t bit_index,
    bool* found,
    ft_persistent_chunked_bit_set_cursor* result);
ft_status ft_persistent_chunked_bit_set_cursor_copy(
    const ft_persistent_chunked_bit_set_cursor* source,
    ft_persistent_chunked_bit_set_cursor* destination);
void ft_persistent_chunked_bit_set_cursor_move(
    ft_persistent_chunked_bit_set_cursor* destination,
    ft_persistent_chunked_bit_set_cursor* source);
void ft_persistent_chunked_bit_set_cursor_dispose(
    ft_persistent_chunked_bit_set_cursor* cursor);
bool ft_persistent_chunked_bit_set_cursor_valid(
    const ft_persistent_chunked_bit_set_cursor* cursor);
bool ft_persistent_chunked_bit_set_cursor_empty(
    const ft_persistent_chunked_bit_set_cursor* cursor);
uint64_t ft_persistent_chunked_bit_set_cursor_count(
    const ft_persistent_chunked_bit_set_cursor* cursor);
uint64_t ft_persistent_chunked_bit_set_cursor_position(
    const ft_persistent_chunked_bit_set_cursor* cursor);
ft_status ft_persistent_chunked_bit_set_cursor_is_at_start(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    bool* result);
ft_status ft_persistent_chunked_bit_set_cursor_is_at_end(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    bool* result);
ft_status ft_persistent_chunked_bit_set_cursor_try_peek_previous(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    bool* found,
    int32_t* bit_index);
ft_status ft_persistent_chunked_bit_set_cursor_try_peek_next(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    bool* found,
    int32_t* bit_index);
ft_status ft_persistent_chunked_bit_set_cursor_move_previous(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    ft_persistent_chunked_bit_set_cursor* result);
ft_status ft_persistent_chunked_bit_set_cursor_move_next(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    ft_persistent_chunked_bit_set_cursor* result);
ft_status ft_persistent_chunked_bit_set_cursor_seek_rank(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    uint64_t position,
    ft_persistent_chunked_bit_set_cursor* result);
ft_status ft_persistent_chunked_bit_set_cursor_add(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    int32_t bit_index,
    ft_persistent_chunked_bit_set_cursor* result);
ft_status ft_persistent_chunked_bit_set_cursor_delete_previous(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    ft_persistent_chunked_bit_set_cursor* result);
ft_status ft_persistent_chunked_bit_set_cursor_delete_next(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    ft_persistent_chunked_bit_set_cursor* result);
ft_status ft_persistent_chunked_bit_set_cursor_snapshot(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    ft_persistent_chunked_bit_set* result);

#ifdef __cplusplus
}
#endif

#endif
