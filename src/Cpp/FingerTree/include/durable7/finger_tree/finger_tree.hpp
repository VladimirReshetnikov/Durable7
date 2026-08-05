/// The measured finger tree: a persistent sequence caching a monoidal measure at every node.
///
/// Because each node's measure is readable without descending into it, one generic split answers
/// any monotone question the measure can express without visiting the elements it skips. Nearly
/// every other collection in this workspace is this tree under a different measure. Every operation
/// returns a new version and leaves its inputs valid, sharing unchanged structure, so an edit
/// copies a path rather than the whole collection.

#pragma once

#include <durable7/finger_tree/ancestral_slice_queue.hpp>
#include <durable7/finger_tree/built_in_measures.hpp>
#include <durable7/finger_tree/bilateral_ancestral_deque.hpp>
#include <durable7/finger_tree/brodal_okasaki_heap.hpp>
#include <durable7/finger_tree/canonical_sorted_set.hpp>
#include <durable7/finger_tree/comparisons.hpp>
#include <durable7/finger_tree/contextual_rank_sequence.hpp>
#include <durable7/finger_tree/daba_lite.hpp>
#include <durable7/finger_tree/detail/common.hpp>
#include <durable7/finger_tree/incremental_ancestor.hpp>
#include <durable7/finger_tree/interval_tree.hpp>
#include <durable7/finger_tree/persistent_interval_map.hpp>
#include <durable7/finger_tree/persistent_chunked_bit_set.hpp>
#include <durable7/finger_tree/measure_predicates.hpp>
#include <durable7/finger_tree/measures.hpp>
#include <durable7/finger_tree/measured_finger_tree.hpp>
#include <durable7/finger_tree/measured_rope.hpp>
#include <durable7/finger_tree/ordered_search_cursors.hpp>
#include <durable7/finger_tree/persistent_delta_map.hpp>
#include <durable7/finger_tree/persistent_deque.hpp>
#include <durable7/finger_tree/persistent_monotone_action_heap.hpp>
#include <durable7/finger_tree/persistent_run_delta_vector.hpp>
#include <durable7/finger_tree/priority_queue.hpp>
#include <durable7/finger_tree/priority_search_queue.hpp>
#include <durable7/finger_tree/product_measure.hpp>
#include <durable7/finger_tree/range_update_sequence.hpp>
#include <durable7/finger_tree/reversible_deque.hpp>
#include <durable7/finger_tree/rope.hpp>
#include <durable7/finger_tree/rope_text.hpp>
#include <durable7/finger_tree/rrb_vector.hpp>
#include <durable7/finger_tree/sorted_bag.hpp>
#include <durable7/finger_tree/sorted_map.hpp>
#include <durable7/finger_tree/sorted_set.hpp>
#include <durable7/finger_tree/sum_measure.hpp>

namespace durable7::finger_tree {

// The public aggregate header grows with the port. Keeping it present from the
// first milestone gives consumer smoke tests a stable include path.

} // namespace durable7::finger_tree
