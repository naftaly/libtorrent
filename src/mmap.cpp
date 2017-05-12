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

#include "libtorrent/aux_/mmap.hpp"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/error_code.hpp"
#include <cstdint>

#include <sys/mman.h> // for mmap
#include <sys/stat.h>
#include <fcntl.h> // for open

#include "libtorrent/aux_/disable_warnings_push.hpp"
auto const map_failed = MAP_FAILED;
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {
namespace aux {
namespace {

	int file_flags(std::uint32_t const mode)
	{
		return (mode & open_mode_t::write)
			? O_RDWR | O_CREAT : O_RDONLY
#ifdef O_NOATIME
			| (mode & open_mode_t::noatime)
			? O_NOATIME : 0
#endif
			;
	}

	int mmap_prot(std::uint32_t const m)
	{
		return (m & open_mode_t::write)
			? (PROT_READ | PROT_WRITE)
			: PROT_READ;
	}

	int mmap_flags(std::uint32_t const m)
	{
		return ((m & open_mode_t::no_cache)
			? MAP_NOCACHE
			: 0)
			| MAP_FILE | MAP_SHARED;
	}

	std::size_t memory_map_size(std::uint32_t const mode
		, std::size_t const file_size, file_handle const& fh)
	{
		// if we're opening the file in write-mode, we'll always truncate it to
		// the right size, but in read mode, we should not map more than the
		// file size
		return (mode & open_mode_t::write)
			? file_size : std::min(std::size_t(fh.get_size()), file_size);
	}


} // anonymous

file_handle::file_handle(string_view name, std::size_t const size
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

file_handle::~file_handle() { if (m_fd >= 0) close(m_fd); }

file_handle& file_handle::operator=(file_handle&& rhs)
{
	if (m_fd >= 0) close(m_fd);
	m_fd = rhs.m_fd;
	rhs.m_fd = -1;
	return *this;
}

std::int64_t file_handle::get_size() const
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

// =========== file mapping ============

file_mapping::file_mapping(file_handle file, std::uint32_t const mode, std::size_t const file_size)
	: m_file(std::move(file))
	, m_size(memory_map_size(mode, file_size, m_file))
	, m_mapping(m_size > 0 ? mmap(nullptr, m_size, mmap_prot(mode), mmap_flags(mode), m_file.fd(), 0)
	: nullptr)
{
	// you can't create an mmap of size 0, so we just set it to null. We
	// still need to create the empty file.
	if (file_size > 0 && m_mapping == map_failed)
	{
		throw_ex<system_error>(error_code(errno, system_category()));
	}
}

file_mapping::file_mapping(file_mapping&& rhs)
	: m_file(std::move(rhs.m_file))
	, m_size(rhs.m_size)
	, m_mapping(rhs.m_mapping)
	{
		TORRENT_ASSERT(m_mapping);
		rhs.m_mapping = nullptr;
	}

	file_mapping& file_mapping::operator=(file_mapping&& rhs)
	{
		if (&rhs == this) return *this;
		if (m_mapping) munmap(m_mapping, m_size);
		m_file = std::move(rhs.m_file);
		m_mapping = rhs.m_mapping;
		m_size = rhs.m_size;
		rhs.m_mapping = nullptr;
		return *this;
	}

	file_mapping::~file_mapping()
	{
		if (m_mapping) munmap(m_mapping, m_size);
	}

	file_view file_mapping::view()
	{
		return file_view(shared_from_this());
	}


} // aux
} // libtorrent

