#pragma once

#include <algorithm>
#include <ranges>

constexpr int INV{std::numeric_limits<int>::min()};
constexpr inline std::ranges::iota_view<int, int> range(int s, int e = INV) {
	if (e == INV)
		return std::ranges::iota_view<int, int>{0, s};
	return std::ranges::iota_view<int, int>{s, e};
}

template<typename T, class P = std::identity>
struct contains { T e; P p = {}; };
template<std::ranges::input_range R, typename T, typename P>
constexpr bool operator|(R&& r, const contains<T, P>& c) { return std::ranges::contains(std::forward<R>(r), c.e, c.p); };

template<typename T>
struct find {T f;};
template<typename S, typename T>
S::value_type* operator|(S &l, find<T> r) {
	if constexpr (std::is_same_v<T, typename S::value_type>) {
		for(auto &e: l)
			if(r.f == e)
				return &e;
	} else {
		for(auto &e: l)
			if(r.f(e))
				return &e;
	}
	return nullptr;
}

