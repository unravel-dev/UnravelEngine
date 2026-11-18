#include "missing_asset_recorder.h"

#include <algorithm>

namespace unravel
{
namespace
{
/// The innermost recorder of this thread.
thread_local missing_asset_recorder* current_missing_asset_recorder = nullptr;
} // namespace

missing_asset_recorder::missing_asset_recorder()
    : previous_(current_missing_asset_recorder)
{
    current_missing_asset_recorder = this;
}

missing_asset_recorder::~missing_asset_recorder()
{
    current_missing_asset_recorder = previous_;
}

auto missing_asset_recorder::get_missing() const -> const std::vector<entry>&
{
    return missing_;
}

void missing_asset_recorder::record(const hpp::uuid& uid, hpp::string_view type)
{
    auto* recorder = current_missing_asset_recorder;
    if(recorder == nullptr || uid.is_nil())
    {
        return;
    }
    const bool is_known = std::any_of(recorder->missing_.begin(),
                                      recorder->missing_.end(),
                                      [&uid](const entry& item)
                                      {
                                          return item.uid == uid;
                                      });
    if(!is_known)
    {
        recorder->missing_.push_back({uid, std::string(type)});
    }
}

} // namespace unravel
