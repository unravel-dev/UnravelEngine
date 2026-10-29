/*
 * Validation suite for the draw and input order of screen-space UI documents.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "ui document order"
 *
 * Documents carry a sort order: the lowest draws first and the highest takes input first. This pins:
 *   - documents come back ordered by sort order, lowest first, whatever their creation order;
 *   - documents of equal order keep the registry's order (a stable sort);
 *   - a list passed in again is refilled, not appended to.
 */

#include "../tests.h"

#include <engine/ui/ecs/components/ui_document_component.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace unravel;

namespace
{

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void test_documents_follow_sort_order()
{
    std::printf("test_documents_follow_sort_order\n");
    entt::registry registry;
    const std::vector<int> orders{10, -5, 0, 3, 0};
    std::vector<entt::entity> created;
    for(const int order : orders)
    {
        const entt::entity entity = registry.create();
        registry.emplace<ui_document_component>(entity).sort_order = order;
        created.push_back(entity);
    }
    std::vector<entt::entity> sorted;
    get_documents_by_sort_order(registry, sorted);
    check(sorted.size() == orders.size(), "every document is listed once");
    bool is_ascending = true;
    for(size_t i = 1; i < sorted.size(); ++i)
    {
        is_ascending &= registry.get<ui_document_component>(sorted[i - 1]).sort_order <=
                        registry.get<ui_document_component>(sorted[i]).sort_order;
    }
    check(is_ascending, "documents come lowest sort order first");
    check(registry.get<ui_document_component>(sorted.front()).sort_order == -5, "the lowest order draws first");
    check(registry.get<ui_document_component>(sorted.back()).sort_order == 10, "the highest order draws last, on top");

    std::vector<entt::entity> registry_order;
    registry.view<ui_document_component>().each(
        [&](entt::entity entity, const ui_document_component&)
        {
            if(registry.get<ui_document_component>(entity).sort_order == 0)
            {
                registry_order.push_back(entity);
            }
        });
    std::vector<entt::entity> sorted_zeros;
    for(const entt::entity entity : sorted)
    {
        if(registry.get<ui_document_component>(entity).sort_order == 0)
        {
            sorted_zeros.push_back(entity);
        }
    }
    check(sorted_zeros == registry_order, "documents of equal order keep the registry's order");
    registry.destroy(created.front());
    get_documents_by_sort_order(registry, sorted);
    check(sorted.size() == orders.size() - 1u, "a list passed in again is refilled");
}

auto run_ui_document_order_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_documents_follow_sort_order();
    std::printf("ui document order: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("ui document order", run_ui_document_order_suite)
