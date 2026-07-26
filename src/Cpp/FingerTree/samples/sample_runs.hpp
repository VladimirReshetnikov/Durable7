/// The sample programs' shared entry points, so the tests can run them too.

#pragma once

#include <iosfwd>

namespace durable7::finger_tree::samples::showcase {

/// Runs the suite's cases and reports each result.
void run(std::ostream& output);

} // namespace durable7::finger_tree::samples::showcase

namespace durable7::finger_tree::samples::persistent_snapshots {

/// Runs the suite's cases and reports each result.
void run(std::ostream& output);

} // namespace durable7::finger_tree::samples::persistent_snapshots
