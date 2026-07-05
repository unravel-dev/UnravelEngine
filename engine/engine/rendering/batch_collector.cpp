#include "batch_collector.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace unravel
{

void batch_stats::reset()
{
    total_batches = 0;
    total_instances = 0;
    draw_calls_saved = 0;
    collection_time_ms = 0.0f;
    preparation_time_ms = 0.0f;
    submission_time_ms = 0.0f;
    instance_buffer_memory_used = 0;
    average_batch_size = 0.0f;
    batching_efficiency = 0.0f;
    split_batches = 0;
}

void batch_stats::calculate_derived_stats()
{
    if(total_batches > 0)
    {
        average_batch_size = static_cast<float>(total_instances) / static_cast<float>(total_batches);
        batching_efficiency = average_batch_size;
    }
    else
    {
        average_batch_size = 0.0f;
        batching_efficiency = 0.0f;
    }

    draw_calls_saved = (total_instances > total_batches) ? (total_instances - total_batches) : 0;
}

auto batch_stats::to_string() const -> std::string
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "batch_stats{";
    oss << "batches=" << total_batches;
    oss << ", instances=" << total_instances;
    oss << ", saved_calls=" << draw_calls_saved;
    oss << ", efficiency=" << batching_efficiency;
    oss << ", avg_size=" << average_batch_size;
    oss << ", collection_time=" << collection_time_ms << "ms";
    oss << ", prep_time=" << preparation_time_ms << "ms";
    oss << ", memory=" << (instance_buffer_memory_used / 1024) << "KB";
    if(split_batches > 0)
    {
        oss << ", splits=" << split_batches;
    }
    oss << "}";
    return oss.str();
}

template class batch_collector_t<batch_key>;
template class batch_collector_t<shadow_batch_key>;

} // namespace unravel
