#pragma once

namespace seq
{
struct seq_manager;

namespace detail
{
auto get_manager() -> seq_manager&;

void push(seq_manager& mgr);
void pop();

} // namespace detail
} // namespace seq
