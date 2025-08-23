#ifndef GENERATOR_UTILS_HPP
#define GENERATOR_UTILS_HPP
#include <utility>
#include <type_traits>
namespace generator
{

/// Will have a type named "Type" that has same type as value returned by method
/// generate() of type Generator.
template<typename generator_t, typename = void>
struct generated_type
{
    // Default case - provide a dummy type for types without generate() method
    using type = void;
};

template<typename generator_t>
struct generated_type<generator_t, std::void_t<decltype(std::declval<const generator_t>().generate())>>
{
    using type = decltype(std::declval<const generator_t>().generate());
};

/// Will have a type named "Type" that has same type as value returned by method
/// edges() for type Primitive.
template<typename primitive_t, typename = void>
struct edge_generator_type
{
    // Default case - provide a dummy type for types without edges() method
    using type = void;
};

template<typename primitive_t>
struct edge_generator_type<primitive_t, std::void_t<decltype(std::declval<const primitive_t>().edges())>>
{
    using type = decltype(std::declval<const primitive_t>().edges());
};

/// Will have a type named "Type" that has same type as value returned by method
/// triangles() for type Primitive.
template<typename primitive_t, typename = void>
struct triangle_generator_type
{
    // Default case - provide a dummy type for types without triangles() method
    using type = void;
};

template<typename primitive_t>
struct triangle_generator_type<primitive_t, std::void_t<decltype(std::declval<const primitive_t>().triangles())>>
{
    using type = decltype(std::declval<const primitive_t>().triangles());
};

/// Will have a type named "Type" that has same type as value returned by method
/// vertices() for type Primitive.
template<typename primitive_t, typename = void>
struct vertex_generator_type
{
    // Default case - provide a dummy type for types without vertices() method
    using type = void;
};

template<typename primitive_t>
struct vertex_generator_type<primitive_t, std::void_t<decltype(std::declval<const primitive_t>().vertices())>>
{
    using type = decltype(std::declval<const primitive_t>().vertices());
};

/// Counts the number of steps left in the generator.
template<typename generator_t>
int count(const generator_t& generator) noexcept
{
    generator_t temp{generator};
    int c = 0;
    while(!temp.done())
    {
        ++c;
        temp.next();
    }
    return c;
}
} // namespace generator

#endif
