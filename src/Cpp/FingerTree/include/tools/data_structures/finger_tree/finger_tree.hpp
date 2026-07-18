#pragma once

#include <tools/data_structures/finger_tree/built_in_measures.hpp>
#include <tools/data_structures/finger_tree/brodal_okasaki_heap.hpp>
#include <tools/data_structures/finger_tree/canonical_sorted_set.hpp>
#include <tools/data_structures/finger_tree/comparisons.hpp>
#include <tools/data_structures/finger_tree/daba_lite.hpp>
#include <tools/data_structures/finger_tree/detail/common.hpp>
#include <tools/data_structures/finger_tree/interval_tree.hpp>
#include <tools/data_structures/finger_tree/persistent_interval_map.hpp>
#include <tools/data_structures/finger_tree/persistent_chunked_bit_set.hpp>
#include <tools/data_structures/finger_tree/measure_predicates.hpp>
#include <tools/data_structures/finger_tree/measures.hpp>
#include <tools/data_structures/finger_tree/measured_finger_tree.hpp>
#include <tools/data_structures/finger_tree/measured_rope.hpp>
#include <tools/data_structures/finger_tree/ordered_search_cursors.hpp>
#include <tools/data_structures/finger_tree/persistent_deque.hpp>
#include <tools/data_structures/finger_tree/priority_queue.hpp>
#include <tools/data_structures/finger_tree/priority_search_queue.hpp>
#include <tools/data_structures/finger_tree/product_measure.hpp>
#include <tools/data_structures/finger_tree/range_update_sequence.hpp>
#include <tools/data_structures/finger_tree/reversible_deque.hpp>
#include <tools/data_structures/finger_tree/rope.hpp>
#include <tools/data_structures/finger_tree/rope_text.hpp>
#include <tools/data_structures/finger_tree/rrb_vector.hpp>
#include <tools/data_structures/finger_tree/sorted_bag.hpp>
#include <tools/data_structures/finger_tree/sorted_map.hpp>
#include <tools/data_structures/finger_tree/sorted_set.hpp>
#include <tools/data_structures/finger_tree/sum_measure.hpp>

namespace tools::data_structures::finger_tree {

// The public aggregate header grows with the port. Keeping it present from the
// first milestone gives consumer smoke tests a stable include path.

} // namespace tools::data_structures::finger_tree
