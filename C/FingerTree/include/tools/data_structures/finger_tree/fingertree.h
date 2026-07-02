#ifndef TOOLS_DATA_STRUCTURES_FINGER_TREE_C_FINGERTREE_H
#define TOOLS_DATA_STRUCTURES_FINGER_TREE_C_FINGERTREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ft_status {
    FT_STATUS_OK = 0,
    FT_STATUS_INVALID_ARGUMENT = 1,
    FT_STATUS_OUT_OF_RANGE = 2,
    FT_STATUS_EMPTY = 3,
    FT_STATUS_NOT_FOUND = 4,
    FT_STATUS_NO_MEMORY = 5,
    FT_STATUS_OVERFLOW = 6
} ft_status;

typedef void (*ft_copy_fn)(void* destination, const void* source, void* context);
typedef void (*ft_destroy_fn)(void* value, void* context);
typedef int (*ft_compare_fn)(const void* left, const void* right, void* context);

typedef struct ft_value_type {
    size_t size;
    ft_copy_fn copy;
    ft_destroy_fn destroy;
    void* context;
} ft_value_type;

typedef struct ft_measure_policy {
    size_t size;
    void (*identity)(void* destination, void* context);
    void (*measure)(void* destination, const void* value, void* context);
    void (*combine)(void* destination, const void* left, const void* right, void* context);
    void* context;
} ft_measure_policy;

typedef struct ft_tree_policy {
    ft_value_type value;
    ft_measure_policy measure;
} ft_tree_policy;

typedef struct ft_tree_rep ft_tree_rep;

typedef struct ft_tree {
    const ft_tree_policy* policy;
    ft_tree_rep* rep;
} ft_tree;

typedef bool (*ft_measure_predicate_fn)(const void* measure, void* context);
typedef void (*ft_visit_fn)(const void* value, void* context);

typedef struct ft_tree_split_result {
    ft_tree left;
    ft_tree right;
} ft_tree_split_result;

void ft_value_type_init(ft_value_type* type, size_t size);
void ft_size_measure_policy_init(ft_measure_policy* policy);
void ft_tree_policy_init_size(ft_tree_policy* policy, const ft_value_type* value_type);

ft_status ft_tree_init(ft_tree* tree, const ft_tree_policy* policy);
ft_status ft_tree_copy(const ft_tree* source, ft_tree* destination);
void ft_tree_dispose(ft_tree* tree);

bool ft_tree_empty(const ft_tree* tree);
size_t ft_tree_size(const ft_tree* tree);
ft_status ft_tree_measure(const ft_tree* tree, void* destination);

ft_status ft_tree_front(const ft_tree* tree, void* destination);
ft_status ft_tree_back(const ft_tree* tree, void* destination);
ft_status ft_tree_at(const ft_tree* tree, size_t index, void* destination);

ft_status ft_tree_push_front(const ft_tree* tree, const void* value, ft_tree* result);
ft_status ft_tree_push_back(const ft_tree* tree, const void* value, ft_tree* result);
ft_status ft_tree_pop_front(const ft_tree* tree, void* value, ft_tree* rest);
ft_status ft_tree_pop_back(const ft_tree* tree, void* value, ft_tree* rest);
ft_status ft_tree_concat(const ft_tree* left, const ft_tree* right, ft_tree* result);
ft_status ft_tree_split_at(const ft_tree* tree, size_t index, ft_tree_split_result* result);
ft_status ft_tree_insert_at(const ft_tree* tree, size_t index, const void* value, ft_tree* result);
ft_status ft_tree_remove_at(const ft_tree* tree, size_t index, ft_tree* result);

ft_status ft_tree_locate(
    const ft_tree* tree,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    void* measure_before,
    void* value);

ft_status ft_tree_split(
    const ft_tree* tree,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    ft_tree* left,
    void* value,
    ft_tree* right);

ft_status ft_tree_visit(const ft_tree* tree, ft_visit_fn visitor, void* context);

typedef ft_tree ft_persistent_deque;

#define ft_persistent_deque_init ft_tree_init
#define ft_persistent_deque_copy ft_tree_copy
#define ft_persistent_deque_dispose ft_tree_dispose
#define ft_persistent_deque_empty ft_tree_empty
#define ft_persistent_deque_size ft_tree_size
#define ft_persistent_deque_front ft_tree_front
#define ft_persistent_deque_back ft_tree_back
#define ft_persistent_deque_at ft_tree_at
#define ft_persistent_deque_push_front ft_tree_push_front
#define ft_persistent_deque_push_back ft_tree_push_back
#define ft_persistent_deque_pop_front ft_tree_pop_front
#define ft_persistent_deque_pop_back ft_tree_pop_back
#define ft_persistent_deque_concat ft_tree_concat
#define ft_persistent_deque_split_at ft_tree_split_at
#define ft_persistent_deque_insert_at ft_tree_insert_at
#define ft_persistent_deque_remove_at ft_tree_remove_at
#define ft_persistent_deque_visit ft_tree_visit

typedef struct ft_reversible_deque {
    ft_tree tree;
    bool reversed;
} ft_reversible_deque;

ft_status ft_reversible_deque_init(ft_reversible_deque* deque, const ft_tree_policy* policy);
ft_status ft_reversible_deque_copy(const ft_reversible_deque* source, ft_reversible_deque* destination);
void ft_reversible_deque_dispose(ft_reversible_deque* deque);
bool ft_reversible_deque_empty(const ft_reversible_deque* deque);
size_t ft_reversible_deque_size(const ft_reversible_deque* deque);
ft_status ft_reversible_deque_reverse(const ft_reversible_deque* deque, ft_reversible_deque* result);
ft_status ft_reversible_deque_at(const ft_reversible_deque* deque, size_t index, void* destination);
ft_status ft_reversible_deque_front(const ft_reversible_deque* deque, void* destination);
ft_status ft_reversible_deque_back(const ft_reversible_deque* deque, void* destination);
ft_status ft_reversible_deque_push_front(const ft_reversible_deque* deque, const void* value, ft_reversible_deque* result);
ft_status ft_reversible_deque_push_back(const ft_reversible_deque* deque, const void* value, ft_reversible_deque* result);
ft_status ft_reversible_deque_pop_front(const ft_reversible_deque* deque, void* value, ft_reversible_deque* rest);
ft_status ft_reversible_deque_pop_back(const ft_reversible_deque* deque, void* value, ft_reversible_deque* rest);

typedef struct ft_sorted_multiset {
    ft_tree tree;
    ft_compare_fn compare;
    void* compare_context;
} ft_sorted_multiset;

typedef ft_sorted_multiset ft_sorted_set;

ft_status ft_sorted_multiset_init(
    ft_sorted_multiset* set,
    const ft_tree_policy* policy,
    ft_compare_fn compare,
    void* compare_context);
ft_status ft_sorted_multiset_copy(const ft_sorted_multiset* source, ft_sorted_multiset* destination);
void ft_sorted_multiset_dispose(ft_sorted_multiset* set);
size_t ft_sorted_multiset_size(const ft_sorted_multiset* set);
bool ft_sorted_multiset_empty(const ft_sorted_multiset* set);
ft_status ft_sorted_multiset_add(const ft_sorted_multiset* set, const void* value, ft_sorted_multiset* result);
ft_status ft_sorted_multiset_remove_one(const ft_sorted_multiset* set, const void* value, ft_sorted_multiset* result);
bool ft_sorted_multiset_contains(const ft_sorted_multiset* set, const void* value);
size_t ft_sorted_multiset_count_of(const ft_sorted_multiset* set, const void* value);
ft_status ft_sorted_multiset_at(const ft_sorted_multiset* set, size_t index, void* destination);
ft_status ft_sorted_multiset_visit(const ft_sorted_multiset* set, ft_visit_fn visitor, void* context);

ft_status ft_sorted_set_init(
    ft_sorted_set* set,
    const ft_tree_policy* policy,
    ft_compare_fn compare,
    void* compare_context);
ft_status ft_sorted_set_copy(const ft_sorted_set* source, ft_sorted_set* destination);
void ft_sorted_set_dispose(ft_sorted_set* set);
size_t ft_sorted_set_size(const ft_sorted_set* set);
bool ft_sorted_set_empty(const ft_sorted_set* set);
ft_status ft_sorted_set_add(const ft_sorted_set* set, const void* value, ft_sorted_set* result);
ft_status ft_sorted_set_remove(const ft_sorted_set* set, const void* value, ft_sorted_set* result);
bool ft_sorted_set_contains(const ft_sorted_set* set, const void* value);
ft_status ft_sorted_set_at(const ft_sorted_set* set, size_t index, void* destination);
ft_status ft_sorted_set_visit(const ft_sorted_set* set, ft_visit_fn visitor, void* context);

typedef struct ft_priority_queue_entry_context ft_priority_queue_entry_context;

typedef struct ft_priority_queue {
    ft_tree_policy policy;
    ft_tree tree;
    ft_value_type value_type;
    ft_value_type priority_type;
    ft_compare_fn compare_priority;
    void* compare_context;
    uint64_t next_ordinal;
    ft_priority_queue_entry_context* entry_context;
} ft_priority_queue;

ft_status ft_priority_queue_init(
    ft_priority_queue* queue,
    const ft_value_type* value_type,
    const ft_value_type* priority_type,
    ft_compare_fn compare_priority,
    void* compare_context);
ft_status ft_priority_queue_copy(const ft_priority_queue* source, ft_priority_queue* destination);
void ft_priority_queue_dispose(ft_priority_queue* queue);
bool ft_priority_queue_empty(const ft_priority_queue* queue);
size_t ft_priority_queue_size(const ft_priority_queue* queue);
ft_status ft_priority_queue_push(
    const ft_priority_queue* queue,
    const void* value,
    const void* priority,
    ft_priority_queue* result);
ft_status ft_priority_queue_try_peek(
    const ft_priority_queue* queue,
    bool* found,
    void* value,
    void* priority);
ft_status ft_priority_queue_try_pop(
    const ft_priority_queue* queue,
    bool* found,
    void* value,
    void* priority,
    ft_priority_queue* rest);

typedef struct ft_interval_i64 {
    int64_t low;
    int64_t high;
} ft_interval_i64;

typedef struct ft_interval_tree_i64 {
    ft_tree_policy policy;
    ft_sorted_multiset intervals;
} ft_interval_tree_i64;

ft_status ft_interval_tree_i64_init(ft_interval_tree_i64* tree);
ft_status ft_interval_tree_i64_copy(const ft_interval_tree_i64* source, ft_interval_tree_i64* destination);
void ft_interval_tree_i64_dispose(ft_interval_tree_i64* tree);
bool ft_interval_tree_i64_empty(const ft_interval_tree_i64* tree);
size_t ft_interval_tree_i64_size(const ft_interval_tree_i64* tree);
ft_status ft_interval_tree_i64_insert(
    const ft_interval_tree_i64* tree,
    ft_interval_i64 interval,
    ft_interval_tree_i64* result);
ft_status ft_interval_tree_i64_remove_one(
    const ft_interval_tree_i64* tree,
    ft_interval_i64 interval,
    ft_interval_tree_i64* result);
bool ft_interval_tree_i64_contains(const ft_interval_tree_i64* tree, ft_interval_i64 interval);
ft_status ft_interval_tree_i64_try_find_overlap(
    const ft_interval_tree_i64* tree,
    ft_interval_i64 query,
    bool* found,
    ft_interval_i64* overlap);
size_t ft_interval_tree_i64_count_overlaps(const ft_interval_tree_i64* tree, ft_interval_i64 query);
ft_status ft_interval_tree_i64_at(const ft_interval_tree_i64* tree, size_t index, ft_interval_i64* destination);

typedef struct ft_text_rope {
    ft_tree_policy policy;
    ft_tree tree;
} ft_text_rope;

typedef struct ft_line_column {
    size_t line;
    size_t column;
} ft_line_column;

ft_status ft_text_rope_init(ft_text_rope* rope);
ft_status ft_text_rope_from_cstr(const char* text, ft_text_rope* rope);
ft_status ft_text_rope_copy(const ft_text_rope* source, ft_text_rope* destination);
void ft_text_rope_dispose(ft_text_rope* rope);
size_t ft_text_rope_size(const ft_text_rope* rope);
ft_status ft_text_rope_at(const ft_text_rope* rope, size_t index, char* value);
ft_status ft_text_rope_insert_char(const ft_text_rope* rope, size_t index, char value, ft_text_rope* result);
ft_status ft_text_rope_remove_at(const ft_text_rope* rope, size_t index, ft_text_rope* result);
size_t ft_text_rope_line_count(const ft_text_rope* rope);
ft_status ft_text_rope_line_column_of(const ft_text_rope* rope, size_t offset, ft_line_column* result);
ft_status ft_text_rope_visit(const ft_text_rope* rope, ft_visit_fn visitor, void* context);

#ifdef __cplusplus
}
#endif

#endif
