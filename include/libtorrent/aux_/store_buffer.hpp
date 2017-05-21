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

#ifndef TORRENT_STORE_BUFFER
#define TORRENT_STORE_BUFFER

#include <map>
#include <mutex>

namespace libtorrent {
namespace aux {

// uniquely identifies a torrent and offset. It is used as the key in the
// dictionary mapping locations to write jobs
struct torrent_location
{
	torrent_location(default_storage* t, piece_index_t p, int o)
		: torrent(t), piece(p), offset(o) {}
	// TODO: this should use the storage index instead
	default_storage* torrent;
	piece_index_t piece;
	int offset;
	bool operator<(torrent_location const& rhs) const
	{
		return std::tie(torrent, piece, offset)
			< std::tie(rhs.torrent, rhs.piece, rhs.offset);
	}
};

struct store_buffer
{
	template <typename Fun>
	bool get(torrent_location const loc, Fun f)
	{
		std::unique_lock<std::mutex> l(m_mutex);
		auto it = m_store_buffer.find(loc);
		if (it != m_store_buffer.end())
		{
			f(it->second);
			return true;
		}
		return false;
	}

	void insert(torrent_location const loc, char* buf)
	{
		std::lock_guard<std::mutex> l(m_mutex);
		m_store_buffer.insert({loc, buf});
	}

	void erase(torrent_location const loc)
	{
		std::lock_guard<std::mutex> l(m_mutex);
		auto it = m_store_buffer.find(loc);
		TORRENT_ASSERT(it != m_store_buffer.end());
		m_store_buffer.erase(it);
	}

private:

	std::mutex m_mutex;

	// TODO: this should be a hash table
	std::map<torrent_location, char*> m_store_buffer;
};

}
}

#endif

