/*

Copyright (c) 2017, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef TORRENT_FILE_VIEW_POOL_HPP
#define TORRENT_FILE_VIEW_POOL_HPP

#include <map>
#include <mutex>
#include <vector>
#include <memory>

#include "libtorrent/file.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/disk_interface.hpp" // for open_file_state
#include "libtorrent/aux_/throw.hpp"

#include <sys/mman.h>
#include <sys/stat.h>

#include "libtorrent/aux_/disable_warnings_push.hpp"
auto const map_failed = MAP_FAILED;

#define BOOST_BIND_NO_PLACEHOLDERS

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/member.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {

class file_storage;
struct open_file_state;

namespace aux {

	// for now
	using byte = char;

	namespace mi = boost::multi_index;

	enum open_mode_t : std::uint32_t
	{ write = 1, no_cache = 2, truncate = 4, no_atime = 8 };

	inline int file_flags(std::uint32_t const mode)
	{
		return (mode & open_mode_t::write)
			? O_RDWR | O_CREAT : O_RDONLY
#ifdef O_NOATIME
			| (mode & open_mode_t::noatime)
			? O_NOATIME : 0
#endif
			;
	}

	struct TORRENT_EXTRA_EXPORT file_handle
	{
		file_handle(string_view name, std::size_t const size
			, std::uint32_t const mode)
			: m_fd(open(name.to_string().c_str(), file_flags(mode), 0755))
		{
#ifdef O_NOATIME
			if (m_fd < 0 && (mode & open_mode_t::no_atime))
			{
				// NOATIME may not be allowed for certain files, it's best-effort,
				// so just try again without NOATIME
				m_fd = open(name.to_string().c_str()
					, file_flags(mode & ~open_mode_t::no_atime), 0755);
			}
#endif
			if (m_fd < 0) throw_ex<system_error>(error_code(errno, system_category()));
			if (mode & open_mode_t::truncate)
			{
				if (ftruncate(m_fd, static_cast<off_t>(size)) < 0) throw_ex<system_error>(error_code(errno, system_category()));
			}
		}
		file_handle(file_handle const& rhs) = delete;
		file_handle& operator=(file_handle const& rhs) = delete;

		file_handle(file_handle&& rhs) : m_fd(rhs.m_fd) { rhs.m_fd = -1; }
		file_handle& operator=(file_handle&& rhs)
		{
			if (m_fd >= 0) close(m_fd);
			m_fd = rhs.m_fd;
			rhs.m_fd = -1;
			return *this;
		}

		~file_handle() { if (m_fd >= 0) close(m_fd); }

		std::int64_t get_size() const
		{
#ifdef TORRENT_WINDOWS
			LARGE_INTEGER file_size;
			if (!GetFileSizeEx(fd(), &file_size))
				throw_ex<system_error>(error_code(GetLastError(), system_category()));
			return file_size.QuadPart;
#else
			struct ::stat fs;
			if (::fstat(fd(), &fs) != 0)
				throw_ex<system_error>(error_code(errno, system_category()));
			return fs.st_size;
#endif
		}

		int fd() const { return m_fd; }
	private:
		int m_fd;
	};

	inline int mmap_prot(std::uint32_t const m)
	{
		return (m & open_mode_t::write)
			? (PROT_READ | PROT_WRITE)
			: PROT_READ;
	}

	inline int mmap_flags(std::uint32_t const m)
	{
		return ((m & open_mode_t::no_cache)
			? MAP_NOCACHE
			: 0)
			| MAP_FILE | MAP_SHARED;
	}

	inline std::size_t memory_map_size(std::uint32_t const mode
		, std::size_t const file_size, file_handle const& fh)
	{
		// if we're opening the file in write-mode, we'll always truncate it to
		// the right size, but in read mode, we should not map more than the
		// file size
		return (mode & open_mode_t::write)
			? file_size : std::min(std::size_t(fh.get_size()), file_size);
	}

	struct file_view;

	struct TORRENT_EXTRA_EXPORT file_mapping : std::enable_shared_from_this<file_mapping>
	{
		friend struct file_view;

		file_mapping(file_handle file, std::uint32_t const mode, std::size_t const file_size)
			: m_file(std::move(file))
			, m_size(memory_map_size(mode, file_size, m_file))
			, m_mapping(m_size > 0
				? mmap(nullptr, m_size, mmap_prot(mode), mmap_flags(mode), m_file.fd(), 0)
				: nullptr)
		{
			// you can't create an mmap of size 0, so we just set it to null. We
			// still need to create the empty file.
			if (file_size > 0 && m_mapping == map_failed)
			{
				throw_ex<system_error>(error_code(errno, system_category()));
			}
		}

		// non-copyable
		file_mapping(file_mapping const&) = delete;
		file_mapping& operator=(file_mapping const&) = delete;

		file_mapping(file_mapping&& rhs)
			: m_file(std::move(rhs.m_file))
			, m_size(rhs.m_size)
			, m_mapping(rhs.m_mapping)
		{
			TORRENT_ASSERT(m_mapping);
			rhs.m_mapping = nullptr;
		}
		file_mapping& operator=(file_mapping&& rhs)
		{
			if (&rhs == this) return *this;
			if (m_mapping) munmap(m_mapping, m_size);
			m_file = std::move(rhs.m_file);
			m_mapping = rhs.m_mapping;
			m_size = rhs.m_size;
			rhs.m_mapping = nullptr;
			return *this;
		}

		~file_mapping()
		{
			if (m_mapping) munmap(m_mapping, m_size);
		}

		// ...
		file_view view();
	private:

		// the memory range this file has been mapped into
		span<byte volatile> memory()
		{
			TORRENT_ASSERT(m_mapping);
			return { static_cast<byte volatile*>(m_mapping), m_size };
		}

		file_handle m_file;
		std::size_t m_size;
		void* m_mapping;
	};

	struct TORRENT_EXTRA_EXPORT file_view
	{
		friend struct file_mapping;
		// TODO: 2 this is a hack. Use exceptions for error handling or
		// boost::optional
		file_view() {}
		file_view(file_view&&) = default;
		file_view& operator=(file_view&&) = default;

		span<byte const volatile> range() const
		{
			TORRENT_ASSERT(m_mapping);
			return m_mapping->memory();
		}

		span<byte volatile> range()
		{
			TORRENT_ASSERT(m_mapping);
			return m_mapping->memory();
		}

	private:
		file_view(std::shared_ptr<file_mapping> m) : m_mapping(std::move(m)) {}
		std::shared_ptr<file_mapping> m_mapping;
	};

	inline file_view file_mapping::view() { return file_view(shared_from_this()); }

	// this is an internal cache of open file mappings.
	struct TORRENT_EXTRA_EXPORT file_view_pool : boost::noncopyable
	{
		// ``size`` specifies the number of allowed files handles
		// to hold open at any given time.
		explicit file_view_pool(int size = 40);
		~file_view_pool();

		// return an open file handle to file at ``file_index`` in the
		// file_storage ``fs`` opened at save path ``p``. ``m`` is the
		// file open mode (see file::open_mode_t).
		file_view open_file(storage_index_t st, std::string const& p
			, file_index_t file_index, file_storage const& fs, std::uint32_t m);

		// release all file views belonging to the specified storage_interface
		// (``st``) the overload that takes ``file_index`` releases only the file
		// with that index in storage ``st``.
		void release();
		void release(storage_index_t st);
		void release(storage_index_t st, file_index_t file_index);

		// update the allowed number of open file handles to ``size``.
		void resize(int size);

		// returns the current limit of number of allowed open file views held
		// by the file_view_pool.
		int size_limit() const { return m_size; }

		std::vector<open_file_state> get_status(storage_index_t st) const;

	private:

		std::shared_ptr<file_mapping> remove_oldest(std::unique_lock<std::mutex>&);

		int m_size;

		using file_id = std::pair<storage_index_t, file_index_t>;

		struct file_entry
		{
			file_entry(file_id k
				, string_view name
				, std::uint32_t const m
				, std::size_t const size)
				: key(k)
				, mapping(std::make_shared<file_mapping>(file_handle(name, size, m), m, size))
				, mode(m)
			{}

			file_id key;
			std::shared_ptr<file_mapping> mapping;
			time_point last_use{aux::time_now()};
			std::uint32_t mode = 0;
		};

		using files_container = mi::multi_index_container<
			file_entry,
			mi::indexed_by<
			// look up files by (torrent, file) key
			mi::ordered_unique<mi::member<file_entry, file_id, &file_entry::key>>,
			// look up files by least recently used
			mi::sequenced<>
			>
		>;

		// maps storage pointer, file index pairs to the lru entry for the file
		files_container m_files;
		mutable std::mutex m_mutex;
	};
}
}

#endif
