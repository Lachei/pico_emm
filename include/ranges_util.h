#pragma once

#include <algorithm>

template<typename T, class P = std::identity>
struct contains { T e; P p = {}; };
template<std::ranges::input_range R, typename T, typename P>
constexpr bool operator|(R&& r, const contains<T, P>& c) { return std::ranges::contains(std::forward<R>(r), c.e, c.p); };

template<typename T>
struct find {T f;};
template<typename S, typename T>
decltype(std::declval<S>().storage)::pointer operator|(S &l, find<T> r) {
	for(auto &e: l)
		if(r.f(e))
			return &e;
	return nullptr;
}

