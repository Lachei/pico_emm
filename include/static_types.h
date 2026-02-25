#pragma once

#include <array>
#include <string_view>
#include <format>

template<int N>
struct static_string {
	int cur_size{};
	std::array<char, N> storage{};
	constexpr static_string() = default;
	constexpr static_string(std::string_view d) {
		cur_size = std::min(d.size(), storage.size());
		std::copy_n(d.begin(), cur_size, storage.begin());
	}
	constexpr std::string_view sv() const { return std::string_view{storage.data(), static_cast<size_t>(cur_size)}; }
	constexpr void set_size(int s) { cur_size = s; }
	constexpr void fill(std::string_view d) { 
		cur_size = std::min(d.size(), storage.size());
		std::copy_n(d.begin(), cur_size, storage.begin());
	}
	constexpr void append(std::string_view d) { 
		size_t s = std::min<size_t>(d.size(), storage.size() - cur_size);
		std::copy_n(d.begin(), s, storage.data() + cur_size);
		cur_size += s;
	}
	constexpr void append(char c) {
		if (cur_size == storage.size())
			return;
		storage[cur_size++] = c;
	}
	template<typename... Args>
	constexpr int fill_formatted(std::format_string<Args...> fmt, Args&&... args) { 
		auto info = std::format_to_n(storage.data(), storage.size(), fmt, std::forward<Args>(args)...); 
		cur_size = info.size;
		return cur_size;
	}
	template<typename... Args>
	constexpr int append_formatted(std::format_string<Args...> fmt, Args&&... args) { 
		int write_size = storage.size() - cur_size;
		auto info = std::format_to_n(storage.data() + cur_size, write_size, fmt, std::forward<Args>(args)...); 
		write_size = std::min(info.size, write_size); 
		cur_size += write_size;
		return write_size;
	}
	constexpr const char* data() const { return storage.data(); }
	constexpr char* data() { return storage.data(); }
	constexpr const char* end() const { return storage.data() + cur_size; }
	constexpr void clear() { cur_size = 0; }
	constexpr bool empty() const { return cur_size == 0; }
	constexpr int size() const { return cur_size; }
	constexpr void make_c_str_safe() { if (static_cast<uint32_t>(cur_size) < storage.size()) storage[cur_size] = '\0'; }
	constexpr void sanitize() { if (cur_size > N || cur_size < 0) cur_size = 0; }
	constexpr bool operator==(const static_string &o) const { return sv() == o.sv(); }
};

template<typename T, int N>
struct static_vector {
	std::array<T, N> storage{};
	int cur_size{};
	constexpr T& operator[](int i) { return storage[std::min(i, N)]; }
	constexpr T* begin() { return storage.begin(); }
	constexpr T* end() { return storage.begin() + cur_size; }
	constexpr const T* begin() const { return storage.begin(); }
	constexpr const T* end() const { return storage.begin() + cur_size; }
	constexpr T* push() { if (cur_size >= N) return {}; return storage.data() + cur_size++; }
	constexpr bool push(const T& e) { if (cur_size == N) return false; storage[cur_size++] = e; return true; }
	constexpr bool push(T&& e) { if (cur_size == N) return false; storage[cur_size++] = std::move(e); return true; }
	constexpr T* pop() { cur_size -= 1; return cur_size >= 0 ? begin() + cur_size: nullptr; }
	template<typename F>
	constexpr void remove_if(F &&f) { for (int i = cur_size - 1; i >= 0; --i) if( f(storage[i]) ) { std::swap(storage[i], storage[cur_size - 1]); --cur_size; } }
	constexpr void clear() { cur_size = 0; }
	constexpr bool empty() const { return cur_size == 0; }
	constexpr int size() const { return cur_size; }
	constexpr void sanitize() { if (cur_size > N || cur_size < 0) cur_size = 0; }
	constexpr int rev_start_idx() const { return cur_size - 1; }
};

template<typename T, int N>
struct static_ring_buffer {
	std::array<T, N> storage{};
	int cur_start{};
	int cur_write{};
	bool full{false};
	constexpr auto begin() { return iterator{*this, cur_start}; }
	constexpr auto end() { return iterator{*this, cur_write}; }
	constexpr auto begin() const { return iterator{*this, cur_start}; }
	constexpr auto end() const { return iterator{*this, cur_write}; }
	constexpr T* push() {T* ret = storage.data() + cur_write; 
		if (cur_start == cur_write && full) cur_start = (cur_start + 1) % N; 
		cur_write = (cur_write + 1) % N; 
		full = cur_start == cur_write; 
		return ret; }
	constexpr bool push(const T& e) { *push() = e; return true; }
	constexpr bool push(T&& e) { *push() = std::move(e); return true; }
	constexpr void clear() { cur_start = 0; cur_write = 0; full = false; }
	constexpr bool empty() const { return cur_start == cur_write  && !full; }
	constexpr int size() const { return full? N: cur_write - cur_start + (cur_start > cur_write ? N: 0); }
	template <typename SR>
	struct iterator {
		SR &_p;
		int _cur;
		bool _start{true};
		iterator& operator++() { _cur = (_cur + 1) % N; _start = false; return *this; }
		iterator operator++(int) const { iterator r{*this}; ++(*this); return r; }
		bool operator==(const iterator &o) const { return (!_p.full || !_start) && _cur == o._cur && &_p == &o._p; }
		bool operator!=(const iterator &o) const { return !(*this == o); }
		auto& operator*() const { return _p.storage[_cur]; }
	};
};

template<int N, typename... Args>
static std::string_view static_format(std::format_string<Args...> fmt, Args&&... args) {
	static static_string<N> string{};
	string.fill_formatted(fmt, std::forward<Args>(args)...);
	return string.sv();
}

template<typename... Args>
static int format_to_sv(std::string_view dest, std::format_string<Args...> fmt, Args&&... args) {
	if (!dest.data())
		return 0;
	return std::format_to_n(const_cast<char*>(dest.data()), dest.size(), fmt, std::forward<Args>(args)...).size;
}

