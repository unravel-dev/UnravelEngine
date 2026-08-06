#pragma once

/*
 * GI v2 Phase 0 harness entry (plan: tasks/gi_rewrite_plan.md, sections 9-10).
 * Called from gi_bake_tests.cpp's main so the whole suite stays one executable and one summary.
 */

namespace unravel::gi_v2_tests
{

/// Runs the Phase 0 suite, accumulating into the shared counters.
void run(int& checks, int& failures);

} // namespace unravel::gi_v2_tests
