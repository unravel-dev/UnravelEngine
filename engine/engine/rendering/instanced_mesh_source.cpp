#include "instanced_mesh_source.h"

#include <algorithm>

namespace unravel
{

void instanced_run_list::clear()
{
    runs_.clear();
}

void instanced_run_list::add(const instanced_run& run)
{
    if(run.part != nullptr && run.count > 0)
    {
        runs_.push_back(run);
    }
}

void instanced_run_list::merge()
{
    if(runs_.size() < 2)
    {
        return;
    }
    std::sort(runs_.begin(),
              runs_.end(),
              [](const instanced_run& left, const instanced_run& right)
              {
                  if(left.part != right.part)
                  {
                      return left.part < right.part;
                  }
                  return left.first < right.first;
              });
    size_t kept = 0;
    for(size_t index = 1; index < runs_.size(); ++index)
    {
        auto& last = runs_[kept];
        const auto& next = runs_[index];
        const bool is_continued =
            next.part == last.part && next.buffer == last.buffer && next.first == last.first + last.count;
        if(is_continued)
        {
            last.count += next.count;
            continue;
        }
        runs_[++kept] = next;
    }
    runs_.resize(kept + 1);
}

auto instanced_run_list::get_runs() const -> const std::vector<instanced_run>&
{
    return runs_;
}

auto instanced_run_list::empty() const -> bool
{
    return runs_.empty();
}

} // namespace unravel
