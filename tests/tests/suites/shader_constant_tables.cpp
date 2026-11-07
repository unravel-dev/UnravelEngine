/*
 * Validation suite: no shipped shader indexes a constant table at run time.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "shader constant tables"
 *
 * Compiled to SPIR-V, which the Vulkan, OpenGL and WebGPU shaders come from, a static const table indexed at run time
 * becomes a function variable the shader fills with the whole table where it uses it: every pixel copies the table,
 * in a loop at every iteration. Computed values, a switch or a uniform array take a table's place. This pins:
 *   - the scan finds a function array of 16 or more elements filled from a constant, by initializer or by store,
 *     and leaves smaller arrays and arrays filled element by element;
 *   - no variant of a shader with a source in engine_data/data/shaders has one, in its committed SPIR-V binary;
 *   - ign16x16 (shaderlib.sh), computed in 32-bit integers, gives the 16x16 table it replaced, value for value.
 */

#include "../tests.h"

#include <engine/meta/rendering/shader_variants.hpp>

#include <filesystem/filesystem.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace unravel;

namespace
{

int g_checks = 0;
int g_failures = 0;

constexpr std::uint32_t SPIRV_MAGIC = 0x07230203u;
constexpr std::size_t SPIRV_HEADER_WORDS = 5;
constexpr std::uint32_t OP_TYPE_ARRAY = 28;
constexpr std::uint32_t OP_TYPE_POINTER = 32;
constexpr std::uint32_t OP_CONSTANT = 43;
constexpr std::uint32_t OP_CONSTANT_COMPOSITE = 44;
constexpr std::uint32_t OP_VARIABLE = 59;
constexpr std::uint32_t OP_STORE = 62;
constexpr std::uint32_t STORAGE_CLASS_FUNCTION = 7;
/// Smaller arrays cost little to fill, and short fixed lists (a few weights) stay readable as arrays.
constexpr std::uint32_t MIN_TABLE_ELEMENTS = 16;
/// The side of ign16x16's tile.
constexpr std::uint32_t IGN_TILE_SIZE = 16;
/// The table ign16x16 looked up before it computed its values: round(255 IGN(x, y)), row by row.
constexpr std::array<std::uint32_t, IGN_TILE_SIZE * IGN_TILE_SIZE> IGN_TABLE = {
      0, 142,  28, 170,  57, 199,  85, 227, 114,   0, 142,  29, 170,  57, 199,  90,
     79, 221, 107, 249, 136,  22, 164,  51, 193,  79, 221, 108, 249, 136,  23, 169,
    158,  44, 186,  73, 215, 101, 243, 130,  16, 158,  45, 187,  73, 215, 102, 248,
    237, 123,  10, 152,  38, 180,  67, 209,  95, 237, 124,  10, 152,  39, 180,  72,
     60, 202,  89, 231, 117,   4, 146,  32, 174,  61, 203,  89, 231, 118,   4, 150,
    139,  26, 168,  54, 196,  83, 225, 111, 253, 140,  26, 168,  55, 197,  83, 229,
    218, 105, 247, 133,  20, 162,  48, 190,  77, 219, 105, 247, 134,  20, 162,  53,
     42, 184,  70, 212,  99, 241, 127,  14, 156,  42, 184,  71, 213,  99, 241, 132,
    121,   8, 149,  36, 178,  64, 206,  93, 235, 121,   8, 150,  36, 178,  65, 211,
    200,  86, 228, 115,   2, 143,  30, 172,  58, 200,  87, 229, 115,   2, 144,  35,
     24, 165,  52, 194,  80, 222, 109, 251, 137,  24, 166,  52, 194,  81, 223, 114,
    103, 244, 131,  18, 159,  46, 188,  74, 216, 103, 245, 131,  18, 160,  51, 192,
    181,  68, 210,  96, 238, 125,  12, 153,  40, 182,  68, 210,  97, 239, 130,  16,
      5, 147,  34, 175,  62, 204,  90, 232, 119,   6, 147,  34, 176,  62, 208,  95,
     84, 226, 113, 254, 141,  28, 169,  56, 198,  84, 226, 113, 255, 141,  32, 174,
    163,  50, 191,  78, 220, 106, 248, 135,  22, 163,  50, 192,  78, 220, 111, 253,
};

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

/// The ids a SPIR-V module declares that a constant-filled table is made of.
struct table_ids
{
    std::unordered_map<std::uint32_t, std::uint32_t> constant_values;
    std::unordered_map<std::uint32_t, std::uint32_t> array_lengths;
    std::unordered_map<std::uint32_t, std::uint32_t> function_array_pointers;
    std::unordered_set<std::uint32_t> constant_composites;
    std::unordered_map<std::uint32_t, std::uint32_t> function_arrays;
};

/// Reads one instruction into @p ids; returns the element count of a table it fills from a constant, or 0.
auto read_instruction(std::uint32_t opcode, const std::uint32_t* operands, std::size_t operand_count, table_ids& ids)
    -> std::uint32_t
{
    switch(opcode)
    {
        case OP_CONSTANT:
            if(operand_count >= 3)
            {
                ids.constant_values[operands[1]] = operands[2];
            }
            return 0;
        case OP_CONSTANT_COMPOSITE:
            if(operand_count >= 2)
            {
                ids.constant_composites.insert(operands[1]);
            }
            return 0;
        case OP_TYPE_ARRAY:
            if(operand_count == 3 && ids.constant_values.contains(operands[2]))
            {
                ids.array_lengths[operands[0]] = ids.constant_values[operands[2]];
            }
            return 0;
        case OP_TYPE_POINTER:
            if(operand_count == 3 && operands[1] == STORAGE_CLASS_FUNCTION && ids.array_lengths.contains(operands[2]))
            {
                ids.function_array_pointers[operands[0]] = ids.array_lengths[operands[2]];
            }
            return 0;
        case OP_VARIABLE:
        {
            const auto pointer = operand_count >= 3 ? ids.function_array_pointers.find(operands[0])
                                                    : ids.function_array_pointers.end();
            if(pointer == ids.function_array_pointers.end() || pointer->second < MIN_TABLE_ELEMENTS)
            {
                return 0;
            }
            if(operand_count >= 4 && ids.constant_composites.contains(operands[3]))
            {
                return pointer->second;
            }
            ids.function_arrays[operands[1]] = pointer->second;
            return 0;
        }
        case OP_STORE:
        {
            const auto array = operand_count >= 2 ? ids.function_arrays.find(operands[0]) : ids.function_arrays.end();
            if(array != ids.function_arrays.end() && ids.constant_composites.contains(operands[1]))
            {
                return array->second;
            }
            return 0;
        }
        default:
            return 0;
    }
}

/// The element counts of the function arrays of @p words (a SPIR-V module) that are filled from a constant.
auto find_constant_filled_tables(const std::vector<std::uint32_t>& words) -> std::vector<std::uint32_t>
{
    std::vector<std::uint32_t> tables;
    table_ids ids;
    for(std::size_t at = SPIRV_HEADER_WORDS; at < words.size();)
    {
        const std::uint32_t word_count = words[at] >> 16;
        if(word_count == 0 || at + word_count > words.size())
        {
            break;
        }
        const std::uint32_t elements = read_instruction(words[at] & 0xFFFFu, &words[at + 1], word_count - 1, ids);
        if(elements != 0)
        {
            tables.push_back(elements);
        }
        at += word_count;
    }
    return tables;
}

/// The SPIR-V module in a bgfx shader binary: its code follows its size.
auto read_spirv_words(const std::vector<std::uint8_t>& bytes) -> std::vector<std::uint32_t>
{
    const std::uint8_t magic[4] = {0x03, 0x02, 0x23, 0x07};
    for(std::size_t at = sizeof(std::uint32_t); at + sizeof(magic) <= bytes.size(); ++at)
    {
        if(std::memcmp(bytes.data() + at, magic, sizeof(magic)) != 0)
        {
            continue;
        }
        std::uint32_t size = 0;
        std::memcpy(&size, bytes.data() + at - sizeof(size), sizeof(size));
        if(size % sizeof(std::uint32_t) != 0 || at + size > bytes.size())
        {
            continue;
        }
        std::vector<std::uint32_t> words(size / sizeof(std::uint32_t));
        std::memcpy(words.data(), bytes.data() + at, size);
        return words;
    }
    return {};
}

/// How a test module fills its function array.
enum class table_fill
{
    constant_store,
    other_store,
    constant_initializer,
};

/// A module with a function array of @p length elements of type 1, filled as @p fill says.
auto make_table_module(std::uint32_t length, table_fill fill) -> std::vector<std::uint32_t>
{
    constexpr std::uint32_t LENGTH_ID = 2;
    constexpr std::uint32_t ARRAY_ID = 3;
    constexpr std::uint32_t POINTER_ID = 4;
    constexpr std::uint32_t COMPOSITE_ID = 5;
    constexpr std::uint32_t VARIABLE_ID = 6;
    constexpr std::uint32_t ELEMENT_ID = 7;
    const auto op = [](std::uint32_t word_count, std::uint32_t opcode)
    {
        return (word_count << 16) | opcode;
    };
    std::vector<std::uint32_t> words = {SPIRV_MAGIC, 0, 0, 16, 0,
                                        op(4, OP_CONSTANT), 1, LENGTH_ID, length,
                                        op(4, OP_TYPE_ARRAY), ARRAY_ID, 1, LENGTH_ID,
                                        op(4, OP_TYPE_POINTER), POINTER_ID, STORAGE_CLASS_FUNCTION, ARRAY_ID,
                                        op(3, OP_CONSTANT_COMPOSITE), ARRAY_ID, COMPOSITE_ID,
                                        op(4, OP_CONSTANT), 1, ELEMENT_ID, 0};
    if(fill == table_fill::constant_initializer)
    {
        words.insert(words.end(), {op(5, OP_VARIABLE), POINTER_ID, VARIABLE_ID, STORAGE_CLASS_FUNCTION, COMPOSITE_ID});
        return words;
    }
    const std::uint32_t stored = fill == table_fill::constant_store ? COMPOSITE_ID : ELEMENT_ID;
    words.insert(words.end(),
                 {op(4, OP_VARIABLE), POINTER_ID, VARIABLE_ID, STORAGE_CLASS_FUNCTION, op(3, OP_STORE), VARIABLE_ID, stored});
    return words;
}

void test_the_scan_finds_constant_filled_tables()
{
    std::printf("test_the_scan_finds_constant_filled_tables\n");
    const auto stored = find_constant_filled_tables(make_table_module(64, table_fill::constant_store));
    check(stored.size() == 1 && stored.front() == 64, "a 64-element function array stored from a constant is found");
    const auto initialized = find_constant_filled_tables(make_table_module(32, table_fill::constant_initializer));
    check(initialized.size() == 1 && initialized.front() == 32, "and one a constant initializes");
    check(find_constant_filled_tables(make_table_module(MIN_TABLE_ELEMENTS - 1, table_fill::constant_store)).empty(),
          "a smaller one is left");
    check(find_constant_filled_tables(make_table_module(256, table_fill::other_store)).empty(),
          "and one stored from something else than a constant");
}

void test_shipped_shaders_fill_no_tables()
{
    std::printf("test_shipped_shaders_fill_no_tables\n");
#if !defined(ENGINE_DATA_DIR)
    std::printf("  SKIP: the engine_data location is not configured\n");
#else
    const fs::path data_dir = ENGINE_DATA_DIR;
    const fs::path source_dir = data_dir / "data" / "shaders";
    const fs::path compiled_dir = data_dir / "compiled" / "shaders";
    std::size_t scanned = 0;
    for(const auto& entry : fs::recursive_directory_iterator(source_dir))
    {
        const auto& path = entry.path();
        if(!entry.is_regular_file() || path.extension() != ".eshader")
        {
            continue;
        }
        const fs::path relative = fs::relative(path, source_dir);
        const fs::path compiled = compiled_dir / relative.parent_path() / (path.filename().string() + ".asset.spirv");
        const std::string shader = (relative.parent_path() / path.stem()).generic_string();
        std::vector<compiled_shader_variant> variants;
        load_from_file_bin(compiled.string(), variants);
        check(!variants.empty(), shader + " has a compiled SPIR-V binary");
        for(const auto& variant : variants)
        {
            ++scanned;
            const auto words = read_spirv_words(variant.bytes);
            check(!words.empty(), shader + " [" + variant.name + "] holds a SPIR-V module");
            for(const std::uint32_t elements : find_constant_filled_tables(words))
            {
                check(false,
                      shader + " [" + variant.name + "] fills a local table of " + std::to_string(elements) +
                          " elements from a constant: compute the values, use a switch or a uniform array");
            }
        }
    }
    check(scanned > 0, "shader variants were scanned");
    std::printf("  %zu variants scanned\n", scanned);
#endif
}

/// mulHigh32 of shaderlib.sh, step for step: the high word of a 32 x 32 bit product from 16-bit halves.
auto multiply_high_from_halves(std::uint32_t a, std::uint32_t b) -> std::uint32_t
{
    const std::uint32_t a_low = a & 0xFFFFu;
    const std::uint32_t a_high = a >> 16u;
    const std::uint32_t b_low = b & 0xFFFFu;
    const std::uint32_t b_high = b >> 16u;
    const std::uint32_t low_low = a_low * b_low;
    const std::uint32_t middle_high = a_high * b_low;
    const std::uint32_t middle_low = a_low * b_high;
    const std::uint32_t carry = ((low_low >> 16u) + (middle_high & 0xFFFFu) + (middle_low & 0xFFFFu)) >> 16u;
    return a_high * b_high + (middle_high >> 16u) + (middle_low >> 16u) + carry;
}

/// ign16x16 of shaderlib.sh, step for step in the same 32-bit integers, before its division by 256.
auto compute_ign_rank(std::uint32_t x, std::uint32_t y) -> std::uint32_t
{
    const std::uint32_t gradient = x * 288237660u + y * 25070368u;
    const std::uint32_t noise = gradient * 52u + multiply_high_from_halves(gradient, 4221604530u);
    return ((noise >> 24u) * 255u + (((noise & 0xFFFFFFu) * 255u) >> 24u) + 128u) >> 8u;
}

void test_ign_matches_its_table()
{
    std::printf("test_ign_matches_its_table\n");
    int mismatches = 0;
    for(std::uint32_t y = 0; y < IGN_TILE_SIZE; ++y)
    {
        for(std::uint32_t x = 0; x < IGN_TILE_SIZE; ++x)
        {
            mismatches += compute_ign_rank(x, y) == IGN_TABLE[y * IGN_TILE_SIZE + x] ? 0 : 1;
        }
    }
    check(mismatches == 0, "ign16x16 gives the table it replaced at every pixel of the tile");
    const std::uint32_t a = 0xDEADBEEFu;
    const std::uint32_t b = 4221604530u;
    check(multiply_high_from_halves(a, b) == std::uint32_t((std::uint64_t(a) * b) >> 32u),
          "its high product from 16-bit halves is the high word of the 64-bit product");
}

auto run_shader_constant_tables_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_the_scan_finds_constant_filled_tables();
    test_shipped_shaders_fill_no_tables();
    test_ign_matches_its_table();
    std::printf("\nshader constant tables: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("shader constant tables", run_shader_constant_tables_suite)
