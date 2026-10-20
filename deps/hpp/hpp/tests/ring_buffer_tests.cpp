#include <hpp/ring_buffer.hpp>

#include <algorithm>
#include <iostream>
#include <vector>

namespace
{
using ring = hpp::stack_ringbuffer<int, 4>;

int failures = 0;

void check(bool condition, const char* what)
{
	if(!condition)
	{
		++failures;
		std::cout << "FAILED: " << what << std::endl;
	}
}

auto make_ring(std::initializer_list<int> values) -> ring
{
	ring buffer;
	for(const int value : values)
	{
		buffer.push_back(value);
	}
	return buffer;
}

auto to_vector(const ring& buffer) -> std::vector<int>
{
	return {buffer.begin(), buffer.end()};
}

void test_positions_before_wrapping()
{
	const ring buffer = make_ring({1, 2, 3});
	check(buffer.size() == 3 && buffer[0] == 1 && buffer[1] == 2 && buffer[2] == 3, "positions count from the oldest");
	check(buffer.front() == 1 && buffer.back() == 3, "front and back are the oldest and the newest");
}

void test_positions_after_wrapping()
{
	// Six into four slots: 1 and 2 are pushed out, and the oldest left sits in slot 2.
	ring buffer = make_ring({1, 2, 3, 4, 5, 6});
	check(buffer.size() == 4 && buffer.front_idx() == 2, "a full ring pushes its oldest out");
	check(buffer[0] == 3 && buffer[1] == 4 && buffer[2] == 5 && buffer[3] == 6, "[] reads positions, not slots");
	check(buffer.at(0) == 3 && buffer.at(3) == 6, "at() reads positions, not slots");
	check(buffer[4] == 3 && buffer[-1] == 6, "positions wrap around");
	check(to_vector(buffer) == std::vector<int>{3, 4, 5, 6}, "iteration runs oldest to newest");
	check(std::vector<int>(buffer.rbegin(), buffer.rend()) == std::vector<int>{6, 5, 4, 3}, "reverse iteration runs newest to oldest");
	check(buffer.front() == 3 && buffer.back() == 6, "front and back follow the wrap");
	check(std::count_if(buffer.begin(), buffer.end(), [](int value) { return value > 4; }) == 2, "standard algorithms walk the positions");
	buffer[1] = 40;
	check(buffer[1] == 40 && *(buffer.begin() + 1) == 40, "a write through [] lands at the position");
}

void test_pops()
{
	ring buffer = make_ring({1, 2, 3, 4, 5, 6});
	check(buffer.pop_front() == 3 && buffer[0] == 4 && buffer.size() == 3, "pop_front takes the oldest");
	// Four in four slots: the newest is in the last slot, so the slot after it is slot 0.
	ring full = make_ring({1, 2, 3, 4});
	check(full.pop_back() == 4 && full.size() == 3 && full[2] == 3, "pop_back takes the newest from the last slot");
}

void test_copies()
{
	const ring buffer = make_ring({1, 2, 3, 4, 5, 6});
	const ring copy = buffer;
	check(to_vector(copy) == std::vector<int>{3, 4, 5, 6} && copy[0] == 3, "a copy keeps the order");
	ring moved = make_ring({1, 2, 3, 4, 5});
	ring target = std::move(moved);
	check(to_vector(target) == std::vector<int>{2, 3, 4, 5} && target[0] == 2, "a move keeps the order");
}
} // namespace

auto run_ring_buffer_tests() -> int
{
	failures = 0;
	test_positions_before_wrapping();
	test_positions_after_wrapping();
	test_pops();
	test_copies();
	std::cout << "ring_buffer: " << (failures == 0 ? "passed" : "FAILED") << std::endl;
	return failures;
}
