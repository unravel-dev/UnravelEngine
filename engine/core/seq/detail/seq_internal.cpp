#include "seq_internal.h"

#include "../seq_manager.h"
#include <vector>

namespace seq
{
namespace detail
{

auto get_global_manager() -> seq_manager&
{
    static seq_manager manager_;
    return manager_;
}

auto get_manager_stack() -> std::vector<seq_manager*>&
{
    static std::vector<seq_manager*> stack{&get_global_manager()};
    return stack;
}

auto get_manager() -> seq_manager&
{
    auto& stack = get_manager_stack();
    return *stack.back();
}

void push(seq_manager& mgr)
{
    auto& stack = get_manager_stack();
    stack.push_back(&mgr);
}
void pop()
{
    auto& stack = get_manager_stack();
    if(stack.size() > 1)
    {
        stack.pop_back();
    }
}

} // namespace detail
} // namespace seq
