#pragma once

#include <algorithm>
#include <ranges>

constexpr int INV{std::numeric_limits<int>::min()};
constexpr inline std::ranges::iota_view<int, int> range(int s, int e = INV) {
	if (e == INV)
		return std::ranges::iota_view<int, int>{0, s};
	return std::ranges::iota_view<int, int>{s, e};
}
// usage: for (int i: range_inv(5)) -> 5, 4, 3, 2, 1, 0
struct range_inv {
	int start, fin = 0;
	struct iterator{
		int cur;
		int operator*() const { return cur; }
		iterator& operator++() { --cur; return *this; }
		bool operator==(const iterator &o) const { return cur == o.cur; }
	};
	iterator begin() const { return iterator{start}; }
	iterator end() const { return iterator{fin - 1}; }
};

struct Void{};
template<typename T, typename V = Void>
struct find {T f; V v{};};
template<typename S, typename T, typename V = Void>
S::value_type* operator|(S &l, find<T, V> r) {
	if constexpr (!std::is_same_v<V, Void>) {
		for (auto &e: l)
			if (e.*(r.f) == r.v)
				return &e;
	} else if constexpr (std::is_same_v<T, typename S::value_type>) {
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

