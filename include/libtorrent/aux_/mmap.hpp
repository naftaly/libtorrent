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

#ifndef TORRENT_MMAP_HPP
#define TORRENT_MMAP_HPP

#include "libtorrent/config.hpp"
#include "libtorrent/disk_interface.hpp" // for open_file_state

namespace libtorrent {

// for now
using byte = char;

namespace aux {

	enum open_mode_t : std::uint32_t
	{ write = 1, no_cache = 2, truncate = 4, no_atime = 8 };

	struct TORRENT_EXTRA_EXPORT file_handle
	{
		file_handle(string_view name, std::size_t const size, std::uint32_t const mode);
		file_handle(file_handle const& rhs) = delete;
		file_handle& operator=(file_handle const& rhs) = delete;

		file_handle(file_handle&& rhs) : m_fd(rhs.m_fd) { rhs.m_fd = -1; }
		file_handle& operator=(file_handle&& rhs);

		~file_handle();

		std::int64_t get_size() const;

		int fd() const { return m_fd; }
	private:
		int m_fd;
	};

	struct file_view;

	struct TORRENT_EXTRA_EXPORT file_mapping : std::enable_shared_from_this<file_mapping>
	{
		friend struct file_view;

		file_mapping(file_handle file, std::uint32_t const mode, std::size_t const file_size);

		// non-copyable
		file_mapping(file_mapping const&) = delete;
		file_mapping& operator=(file_mapping const&) = delete;

		file_mapping(file_mapping&& rhs);
		file_mapping& operator=(file_mapping&& rhs);
		~file_mapping();

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

} // aux
} // libtorrent

#endif

