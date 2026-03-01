#pragma once

#include <cmath>
#include <span>

#include "pico/flash.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"

#include "log_storage.h"
#include "mutex.h"
#include "settings.h"

constexpr uint32_t FLASH_SIZE{PICO_FLASH_SIZE_BYTES};

/** 
 * @brief Add new members always at the front and leave the ones in the back the same
 * as the elements at the back of the layout always stay in the same position
 */
struct persistent_storage_layout {
	settings persistent_settings;
	static_string<64> user_pwd;
	static_string<64> hostname;
	static_string<64> ssid_wifi;
	static_string<64> pwd_wifi;
};

static char *flash_begin{reinterpret_cast<char*>(uintptr_t(XIP_BASE))};

/** 
 * @brief  strcut to easily access/setup permanent storage with a static size and lots of compile time validations.
 * Sets up the storage at the very end of the memory range and acquires as many bytes as needed for the persistent_mem_layout struct
 * to fit
 * @usage
 * The usage is normally as follows:
 *
 * # declare the storage in a typed fashion
 * struct layout {
 *	std::array<char, 200> storage_a;
 *	int storage_b
 *	std::array<int, 400> storage_c;
 * };
 * using persistent_storage_t = persistent_storage<layout>;
 *
 * # reading and writing a value to the storage from memory
 * std::array<char, 200> mem_a;
 * persistent_storage_t::Default().write(mem_a, &layout::storage_a);
 * persistent_storage_t::Default().write_array_range(mem_a.data(), &layout::storage_a, 10, 20)
 * persistent_storage_t::Default().read(&layout::storage_a, mem_a);
 * persistent_storage_t::Default().read_array_range(&layout::storage_a, 10, 20, mem_a);
 *
 * int mem_b;
 * persistent_storage_t::Default().write(mem_b, &layout::storage_b);
 * persistent_storage_t::Default().read(&layout::storage_b, mem_b);
 */

template<typename persistent_mem_layout, int MAX_WRITE_SIZE = 2 * FLASH_SECTOR_SIZE>
struct persistent_storage {
	static constexpr uint32_t begin_offset{FLASH_SIZE - sizeof(persistent_mem_layout)}; // flash page alignment is done only when writing
	const char *storage_begin{flash_begin + begin_offset};
	const char *storage_end{flash_begin + FLASH_SIZE}; // 2MB after flash start is end

	static persistent_storage& Default() {
		static persistent_storage p{};
		return p;
	}

	mutex _memory_mutex{};
	std::array<char, MAX_WRITE_SIZE> _write_buffer{};

	template<typename M>
	using mem_t = std::decay_t<decltype(std::declval<persistent_mem_layout>().*std::declval<M>())>;

	/** @brief To be used with member pointers: int Struct:: *member = &Struct::member_a; */
	template<typename M, typename T = mem_t<M>> requires (std::islessequal(sizeof(T), MAX_WRITE_SIZE))
	err_t write(const T &data, M member) {
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wstrict-aliasing"
		uint32_t start_idx_data = begin_offset + *reinterpret_cast<uintptr_t*>(&member);
		uint32_t start_idx_paged = start_idx_data / FLASH_SECTOR_SIZE * FLASH_SECTOR_SIZE;
		uint32_t end_idx_data = start_idx_data + sizeof(T);
		uint32_t end_idx_paged = (end_idx_data + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE * FLASH_SECTOR_SIZE;
		if (end_idx_paged - start_idx_paged > MAX_WRITE_SIZE) {
			LogError("persistent_storage::write() too large data to write, abort.");
			return PICO_ERROR_GENERIC;
		}
		scoped_lock lock{_memory_mutex};
		memcpy(_write_buffer.data() + start_idx_data - start_idx_paged, &data, sizeof(T));
		#pragma GCC diagnostic pop
		return _write_impl(start_idx_paged, start_idx_data, end_idx_data, end_idx_paged);
	}
	/** @brief Range based write overload, see write() for usage. Size has to be given in bytes written */
	template<typename M, typename T = mem_t<M>::value_t>
	err_t write_array_range(const T *data, M member, uint32_t start_idx, uint32_t end_idx) {
		if (start_idx == end_idx)
			return PICO_OK;
		if (end_idx > view(member).size() || start_idx > view(member).size() || start_idx > end_idx) {
			LogError("persistent_storage::write() indices out of bounds, abort.");
			return PICO_ERROR_GENERIC;
		}
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wstrict-aliasing"
		uint32_t start_idx_data = begin_offset + *reinterpret_cast<uintptr_t*>(&member) + start_idx * sizeof(T);
		uint32_t start_idx_paged = start_idx_data / FLASH_SECTOR_SIZE * FLASH_SECTOR_SIZE;
		uint32_t end_idx_data = start_idx_data + sizeof(T) * (end_idx - start_idx);
		uint32_t end_idx_paged = (end_idx_data + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE * FLASH_SECTOR_SIZE;
		if (end_idx_paged - start_idx_paged > MAX_WRITE_SIZE) {
			LogError("persistent_storage::write() too large data to write, abort.");
			return PICO_ERROR_GENERIC;
		}
		LogInfo("before lock write_array_range");
		scoped_lock lock{_memory_mutex};
		LogInfo("after lock");
		memcpy(_write_buffer.data() + start_idx_data - start_idx_paged, data, end_idx_data - start_idx_data);
		#pragma GCC diagnostic pop
		return _write_impl(start_idx_paged, start_idx_data, end_idx_data, end_idx_paged);
	}
	template<typename M, typename T = mem_t<M>> requires (std::islessequal(sizeof(T), MAX_WRITE_SIZE))
	void read(M member, T& out) const {
		// read is a simple copy from flash memory
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wstrict-aliasing"
		scoped_lock lock{_memory_mutex};
		memcpy(&out, storage_begin + *reinterpret_cast<uintptr_t*>(&member), sizeof(T));
		#pragma GCC diagnostic pop
	}
	template<typename M, typename T = mem_t<M>::value_type>
	void read_array_range(M member, uint32_t start_idx, uint32_t end_idx, T* out) const {
		// read is a simple copy from flash memory
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wstrict-aliasing"
		scoped_lock lock{_memory_mutex};
		memcpy(out, storage_begin + *reinterpret_cast<uintptr_t*>(&member) + start_idx * sizeof(T), sizeof(T) * (end_idx - start_idx));
		#pragma GCC diagnostic pop
	}
	template<typename M, typename T = mem_t<M>>
	const T& view(M member) const {
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wstrict-aliasing"
		scoped_lock lock{_memory_mutex};
		return *reinterpret_cast<const T*>(storage_begin + *reinterpret_cast<uintptr_t*>(&member));
		#pragma GCC diagnostic pop
	}
	template<typename M, typename T = mem_t<M>::value_type>
	std::span<T> view(M member, uint32_t start_idx, uint32_t end_idx) const {
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wstrict-aliasing"
		scoped_lock lock{_memory_mutex};
		return {(T*)(storage_begin + *reinterpret_cast<uintptr_t*>(&member) + start_idx * sizeof(T)), end_idx - start_idx};
		#pragma GCC diagnostic pop
	}

	/*INTERNAL*/ struct _write_data {const char *src_start, *src_end; uint32_t dst_offset;}; // dst offset is the offset of the flash begin
	/*INTERNAL*/ err_t _write_impl(uint32_t start_paged, uint32_t start_data, uint32_t end_data, uint32_t end_paged) {
		if (start_data != start_paged)
			memcpy(_write_buffer.data(), flash_begin + start_paged, start_data - start_paged);	
		if (end_data != end_paged)
			memcpy(_write_buffer.data() + end_data - start_paged, flash_begin + end_data, end_paged - end_data);	
		_write_data write_data{.src_start = _write_buffer.data(), 
					.src_end = _write_buffer.data() + end_paged - start_paged, 
					.dst_offset = start_paged};
		// first erase as flash_range_program only allows to change 1s to 0s, but not the other way around
		flash_safe_execute(_flash_erase, (void*)&write_data, UINT32_MAX);
		flash_safe_execute(_flash_program, (void*)&write_data, UINT32_MAX);
		return PICO_OK;
	}
	/*INTERNAL*/ static void __no_inline_not_in_flash_func(_flash_erase)(void *d) {
		const _write_data &data = *reinterpret_cast<const _write_data*>(d);
		const uint32_t write_size = data.src_end - data.src_start;
		if (write_size % FLASH_SECTOR_SIZE != 0) {
			LogError("_flash_erase(): write range must be a multiple of the FLASH_SECTOR_SIZE. Ignoreing write");
			return;
		}
		if (write_size + data.dst_offset > FLASH_SIZE) {
			LogError("_flash_erase(): write range overflows storage. Ignoring write.");
			return;
		}
		flash_range_erase(data.dst_offset, write_size);
	}
	/*INTERNAL*/ static void __no_inline_not_in_flash_func(_flash_program)(void *d) {
		const _write_data &data = *reinterpret_cast<const _write_data*>(d);
		const uint32_t write_size = data.src_end - data.src_start;
		if (write_size % FLASH_SECTOR_SIZE != 0) {
			LogError("_flash_program(): write range must be a multiple of the FLASH_SECTOR_SIZE. Ignoreing write");
			return;
		}
		if (write_size + data.dst_offset > FLASH_SIZE) {
			LogError("_flash_program(): write range overflows storage. Ignoring write.");
			return;
		}
		flash_range_program(data.dst_offset, reinterpret_cast<const uint8_t*>(data.src_start), write_size);
	}
};

using persistent_storage_t = persistent_storage<persistent_storage_layout>;

