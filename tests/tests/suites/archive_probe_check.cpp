/*
 * Compile-time check that every associative input archive can answer "is this name here"
 * without throwing.
 *
 * associative_archive.h selects exactly one archive (simdjson today) and only that branch
 * is ever compiled, so the other three rot silently: they are perfectly good code that
 * nothing instantiates until someone flips SER20_ASSOCIATIVE_ARCHIVE, at which point the
 * breakage lands on whoever did the flipping.
 *
 * The static_assert in associative_archive.h guards the *selected* archive. This file
 * guards the rest, by pulling them into a translation unit that is compiled on every
 * build of the unravel-tests runner.
 *
 * Why it matters: try_serialize_direct falls back to catching search()'s exception when an
 * archive lacks hasNextName, so a missing implementation is not a correctness bug - which
 * is exactly the problem. Loading an entity probes every serializable component type by
 * name and most of those miss, so the fallback costs a stack unwind per absent component:
 * measured at ~2 us each, ~30 per entity, a 6x slowdown on every scene load, play start
 * and script recompile. Nothing fails. It just gets slow, quietly.
 *
 * hasNextName is a membership test over the current level, NOT a comparison against
 * getNodeName(). Readers skip fields without consuming them - a suppressed prefab
 * override, a component should_load_component declines to read - which leaves the cursor
 * behind the name being asked for. A cursor comparison would report those as absent and
 * silently drop every field after the first skip. See tasks/serialization_prefab_audit.md.
 */

#include <serialization/serialization.h>

#include <ser20/archives/json.hpp>
#include <ser20/archives/xml.hpp>
#include <serialization/archives/yaml.hpp>
#include <serialization/associative_archive.h>

namespace
{

static_assert(can_probe_names<ser20::simd::JSONInputArchive>,
              "simdjson input archive lost its non-throwing hasNextName");
static_assert(can_probe_names<ser20::JSONInputArchive>,
              "rapidjson input archive lost its non-throwing hasNextName");
static_assert(can_probe_names<ser20::XMLInputArchive>,
              "xml input archive lost its non-throwing hasNextName");
static_assert(can_probe_names<ser20::YAMLInputArchive>,
              "yaml input archive lost its non-throwing hasNextName");

/// Output archives have no lookup to probe; the concept must not claim otherwise, or the
/// save path would start consulting a function that cannot answer.
static_assert(!can_probe_names<ser20::simd::JSONOutputArchive>,
              "output archives must not satisfy can_probe_names");

} // namespace
