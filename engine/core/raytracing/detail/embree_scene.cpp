// The Embree backend, compiled only into a build that has Embree: engine/core/raytracing/CMakeLists.txt defines
// RAYTRACING_WITH_EMBREE when deps/3rdparty/embree provides the embree target.
#if defined(RAYTRACING_WITH_EMBREE)

#include "scene_backend.h"

#include <logging/logging.h>

#include <embree4/rtcore.h>

#include <algorithm>
#include <array>
#include <utility>

namespace raytracing::detail
{
namespace
{

/// Embree reports errors through this callback only: a failed call otherwise just does nothing.
void report_device_error(void* /*user*/, RTCError code, const char* message)
{
    APPLOG_ERROR("Embree error {}: {}", int(code), message != nullptr ? message : "");
}

/// One device for the process. Its builds run on the calling thread ("threads=1"): the bakes already run many of
/// them at once on their own pool, and Embree's task scheduler would oversubscribe the machine on top of it.
auto get_device() -> RTCDevice
{
    static const RTCDevice device = []() -> RTCDevice
    {
        RTCDevice created = rtcNewDevice("threads=1");
        if(created == nullptr)
        {
            APPLOG_ERROR("Embree device creation failed (error {})", int(rtcGetDeviceError(nullptr)));
            return nullptr;
        }
        rtcSetDeviceErrorFunction(created, report_device_error, nullptr);
        return created;
    }();
    return device;
}

/// The triangle a query ignores, carried to the filter in the context Embree hands it.
struct skip_query_context
{
    ///< First member: the filter receives a pointer to it and casts back.
    RTCRayQueryContext context;
    uint32_t skip_triangle = triangle_scene::invalid_triangle;
};

void skip_triangle_filter(const RTCFilterFunctionNArguments* arguments)
{
    const auto* query = reinterpret_cast<const skip_query_context*>(arguments->context);
    for(unsigned int i = 0; i < arguments->N; ++i)
    {
        if(arguments->valid[i] != 0 && RTCHitN_primID(arguments->hit, arguments->N, i) == query->skip_triangle)
        {
            arguments->valid[i] = 0;
        }
    }
}

/// Traces up to triangle_scene::packet_size rays as one packet; the lanes past their count stay inactive.
void intersect_packet(RTCScene scene, hpp::span<const ray> rays, hpp::span<ray_hit> hits)
{
    alignas(32) std::array<int, triangle_scene::packet_size> valid{};
    RTCRayHit8 query{};
    for(size_t i = 0; i < triangle_scene::packet_size; ++i)
    {
        query.hit.geomID[i] = RTC_INVALID_GEOMETRY_ID;
        query.hit.instID[0][i] = RTC_INVALID_GEOMETRY_ID;
        if(i >= rays.size())
        {
            continue;
        }
        const ray& lane = rays[i];
        valid[i] = -1;
        query.ray.org_x[i] = lane.origin.x;
        query.ray.org_y[i] = lane.origin.y;
        query.ray.org_z[i] = lane.origin.z;
        query.ray.dir_x[i] = lane.direction.x;
        query.ray.dir_y[i] = lane.direction.y;
        query.ray.dir_z[i] = lane.direction.z;
        query.ray.tnear[i] = lane.t_near;
        query.ray.tfar[i] = lane.t_far;
        query.ray.mask[i] = 0xFFFFFFFFu;
    }
    RTCIntersectArguments arguments;
    rtcInitIntersectArguments(&arguments);
    rtcIntersect8(valid.data(), scene, &query, &arguments);
    for(size_t i = 0; i < rays.size(); ++i)
    {
        ray_hit& hit = hits[i];
        hit = {};
        hit.t = rays[i].t_far;
        if(query.hit.geomID[i] != RTC_INVALID_GEOMETRY_ID)
        {
            hit.t = query.ray.tfar[i];
            hit.triangle = query.hit.primID[i];
            hit.is_hit = true;
        }
    }
}

/// What a triangle collection is looking for, and what it has found so far.
struct triangle_collection
{
    const float* vertices = nullptr;
    const uint32_t* triangles = nullptr;
    math::bbox region;
    float reach_squared = 0.0f;
    hpp::span<uint32_t> out;
    uint32_t count = 0;
};

/// Squared distance between two boxes, zero when they overlap.
auto distance_squared_between_bounds(const math::bbox& a, const math::bbox& b) -> float
{
    const math::vec3 gap = math::max(math::max(a.min - b.max, b.min - a.max), math::vec3(0.0f));
    return math::dot(gap, gap);
}

/// Embree's point query callback for collect_triangles, called for each triangle of every leaf the query sphere
/// reaches.
auto collect_triangle(RTCPointQueryFunctionArguments* arguments) -> bool
{
    auto* collection = static_cast<triangle_collection*>(arguments->userPtr);
    const uint32_t capacity = uint32_t(collection->out.size());
    if(collection->count > capacity)
    {
        return false;
    }
    const uint32_t triangle = arguments->primID;
    math::bbox bounds;
    bounds.reset();
    for(uint32_t corner = 0; corner < 3; ++corner)
    {
        const float* vertex = collection->vertices + size_t(collection->triangles[size_t(triangle) * 3 + corner]) * 3;
        bounds.add_point(math::vec3(vertex[0], vertex[1], vertex[2]));
    }
    if(distance_squared_between_bounds(bounds, collection->region) > collection->reach_squared)
    {
        return false;
    }
    // A high quality build splits large triangles across leaves, so the same triangle can come back.
    const auto found = collection->out.begin() + collection->count;
    if(std::find(collection->out.begin(), found, triangle) != found)
    {
        return false;
    }
    if(collection->count == capacity)
    {
        // Full: flag the overflow and shrink the query to nothing, which ends the traversal's descent.
        collection->count = capacity + 1;
        arguments->query->radius = 0.0f;
        return true;
    }
    collection->out[collection->count++] = triangle;
    return false;
}

/// The Embree backend: one scene holding one triangle geometry.
class embree_scene final : public scene_backend
{
public:
    embree_scene() = default;
    embree_scene(const embree_scene&) = delete;
    auto operator=(const embree_scene&) -> embree_scene& = delete;

    ~embree_scene() override
    {
        if(scene_ != nullptr)
        {
            rtcReleaseScene(scene_);
        }
    }

    auto build(const std::vector<math::vec3>& positions, const std::vector<uint32_t>& indices) -> bool override
    {
        const size_t triangle_count = indices.size() / 3;
        RTCDevice device = get_device();
        if(device == nullptr)
        {
            return false;
        }
        RTCScene scene = rtcNewScene(device);
        // A bake queries one scene millions of times, so the slower, tighter build pays for itself.
        rtcSetSceneBuildQuality(scene, RTC_BUILD_QUALITY_HIGH);
        RTCGeometry geometry = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_TRIANGLE);
        rtcSetGeometryBuildQuality(geometry, RTC_BUILD_QUALITY_HIGH);
        auto* vertices = static_cast<float*>(rtcSetNewGeometryBuffer(geometry,
                                                                     RTC_BUFFER_TYPE_VERTEX,
                                                                     0,
                                                                     RTC_FORMAT_FLOAT3,
                                                                     3 * sizeof(float),
                                                                     positions.size()));
        auto* triangles = static_cast<uint32_t*>(rtcSetNewGeometryBuffer(geometry,
                                                                         RTC_BUFFER_TYPE_INDEX,
                                                                         0,
                                                                         RTC_FORMAT_UINT3,
                                                                         3 * sizeof(uint32_t),
                                                                         triangle_count));
        if(vertices == nullptr || triangles == nullptr)
        {
            rtcReleaseGeometry(geometry);
            rtcReleaseScene(scene);
            return false;
        }
        for(size_t i = 0; i < positions.size(); ++i)
        {
            vertices[i * 3 + 0] = positions[i].x;
            vertices[i * 3 + 1] = positions[i].y;
            vertices[i * 3 + 2] = positions[i].z;
        }
        std::copy(indices.begin(), indices.begin() + std::ptrdiff_t(triangle_count * 3), triangles);
        // Lets a query pass its own filter (skip_triangle_filter) through its arguments.
        rtcSetGeometryEnableFilterFunctionFromArguments(geometry, true);
        rtcCommitGeometry(geometry);
        rtcAttachGeometry(scene, geometry);
        rtcReleaseGeometry(geometry);
        rtcCommitScene(scene);
        if(rtcGetDeviceError(device) != RTC_ERROR_NONE)
        {
            rtcReleaseScene(scene);
            return false;
        }
        scene_ = scene;
        vertices_ = vertices;
        triangles_ = triangles;
        return true;
    }

    auto intersect(const math::vec3& origin,
                   const math::vec3& direction,
                   float t_near,
                   uint32_t skip_triangle,
                   float t_far) const -> ray_hit override
    {
        ray_hit result;
        result.t = t_far;
        RTCRayHit query{};
        query.ray.org_x = origin.x;
        query.ray.org_y = origin.y;
        query.ray.org_z = origin.z;
        query.ray.dir_x = direction.x;
        query.ray.dir_y = direction.y;
        query.ray.dir_z = direction.z;
        query.ray.tnear = t_near;
        query.ray.tfar = t_far;
        query.ray.mask = 0xFFFFFFFFu;
        query.hit.geomID = RTC_INVALID_GEOMETRY_ID;
        query.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
        skip_query_context context;
        rtcInitRayQueryContext(&context.context);
        context.skip_triangle = skip_triangle;
        RTCIntersectArguments arguments;
        rtcInitIntersectArguments(&arguments);
        arguments.context = &context.context;
        if(skip_triangle != triangle_scene::invalid_triangle)
        {
            arguments.filter = skip_triangle_filter;
        }
        rtcIntersect1(scene_, &query, &arguments);
        if(query.hit.geomID != RTC_INVALID_GEOMETRY_ID)
        {
            result.t = query.ray.tfar;
            result.triangle = query.hit.primID;
            result.is_hit = true;
        }
        return result;
    }

    void intersect(hpp::span<const ray> rays, hpp::span<ray_hit> hits) const override
    {
        for(size_t first = 0; first < rays.size(); first += triangle_scene::packet_size)
        {
            const size_t count = std::min<size_t>(triangle_scene::packet_size, rays.size() - first);
            if(count == 1)
            {
                // A lone ray is cheaper on its own than as a packet of one.
                const ray& lone = rays[first];
                hits[first] =
                    intersect(lone.origin, lone.direction, lone.t_near, triangle_scene::invalid_triangle, lone.t_far);
                continue;
            }
            intersect_packet(scene_, rays.subspan(first, count), hits.subspan(first, count));
        }
    }

    auto collect_triangles(const math::bbox& region, float reach, hpp::span<uint32_t> out) const -> uint32_t override
    {
        triangle_collection collection;
        collection.vertices = vertices_;
        collection.triangles = triangles_;
        collection.region = region;
        collection.reach_squared = reach * reach;
        collection.out = out;
        // The sphere around the region's centre that holds every point within reach of the region.
        const math::vec3 center = region.get_center();
        RTCPointQuery query{};
        query.x = center.x;
        query.y = center.y;
        query.z = center.z;
        query.radius = math::length(region.max - center) + reach;
        RTCPointQueryContext context;
        rtcInitPointQueryContext(&context);
        rtcPointQuery(scene_, &query, &context, collect_triangle, &collection);
        return collection.count;
    }

private:
    RTCScene scene_ = nullptr;
    ///< Embree's copies of the vertex and index buffers, alive as long as @ref scene_.
    const float* vertices_ = nullptr;
    const uint32_t* triangles_ = nullptr;
};

} // namespace

auto make_embree_scene() -> std::unique_ptr<scene_backend>
{
    return std::make_unique<embree_scene>();
}

} // namespace raytracing::detail

#endif // RAYTRACING_WITH_EMBREE
