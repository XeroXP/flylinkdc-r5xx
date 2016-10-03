/*

Copyright (c) 2003-2016, Arvid Norberg
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

#ifndef TORRENT_IO_HPP_INCLUDED
#define TORRENT_IO_HPP_INCLUDED

#include <cstdint>
#include <string>
#include <algorithm> // for copy
#include <cstring> // for memcpy

namespace libtorrent
{
	namespace detail
	{
		template <class T> struct type {};

		// reads an integer from a byte stream
		// in big endian byte order and converts
		// it to native endianess
		template <class T, class InIt>
		inline T read_impl(InIt& start, type<T>)
		{
			T ret = 0;
			for (int i = 0; i < int(sizeof(T)); ++i)
			{
				ret <<= 8;
				ret |= static_cast<std::uint8_t>(*start);
				++start;
			}
			return ret;
		}

		template <class InIt>
		std::uint8_t read_impl(InIt& start, type<std::uint8_t>)
		{
			return static_cast<std::uint8_t>(*start++);
		}

		template <class InIt>
		std::int8_t read_impl(InIt& start, type<std::int8_t>)
		{
			return static_cast<std::int8_t>(*start++);
		}

		template <class T, class OutIt>
		inline void write_impl(T val, OutIt& start)
		{
			for (int i = int(sizeof(T))-1; i >= 0; --i)
			{
				*start = static_cast<unsigned char>((val >> (i * 8)) & 0xff);
				++start;
			}
		}

		// -- adaptors

		template <class InIt>
		std::int64_t read_int64(InIt& start)
		{ return read_impl(start, type<std::int64_t>()); }

		template <class InIt>
		std::uint64_t read_uint64(InIt& start)
		{ return read_impl(start, type<std::uint64_t>()); }

		template <class InIt>
		std::uint32_t read_uint32(InIt& start)
		{ return read_impl(start, type<std::uint32_t>()); }

		template <class InIt>
		std::int32_t read_int32(InIt& start)
		{ return read_impl(start, type<std::int32_t>()); }

		template <class InIt>
		std::int16_t read_int16(InIt& start)
		{ return read_impl(start, type<std::int16_t>()); }

		template <class InIt>
		std::uint16_t read_uint16(InIt& start)
		{ return read_impl(start, type<std::uint16_t>()); }

		template <class InIt>
		std::int8_t read_int8(InIt& start)
		{ return read_impl(start, type<std::int8_t>()); }

		template <class InIt>
		std::uint8_t read_uint8(InIt& start)
		{ return read_impl(start, type<std::uint8_t>()); }


		template <class OutIt>
		void write_uint64(std::uint64_t val, OutIt& start)
		{ write_impl(val, start); }

		template <class OutIt>
		void write_int64(std::int64_t val, OutIt& start)
		{ write_impl(val, start); }

		template <class OutIt>
		void write_uint32(std::uint32_t val, OutIt& start)
		{ write_impl(val, start); }

		template <class OutIt>
		void write_int32(std::int32_t val, OutIt& start)
		{ write_impl(val, start); }

		template <class OutIt>
		void write_uint16(std::uint16_t val, OutIt& start)
		{ write_impl(val, start); }

		template <class OutIt>
		void write_int16(std::int16_t val, OutIt& start)
		{ write_impl(val, start); }

		template <class OutIt>
		void write_uint8(std::uint8_t val, OutIt& start)
		{ write_impl(val, start); }

		template <class OutIt>
		void write_int8(std::int8_t val, OutIt& start)
		{ write_impl(val, start); }

		inline int write_string(std::string const& str, char*& start)
		{
			std::memcpy(reinterpret_cast<void*>(start), str.c_str(), str.size());
			start += str.size();
			return int(str.size());
		}

		template <class OutIt>
		int write_string(std::string const& val, OutIt& out)
		{
			for (std::string::const_iterator i = val.begin()
				, end(val.end()); i != end; ++i)
				*out++ = *i;
			return int(val.length());
		}
	}
}

#endif // TORRENT_IO_HPP_INCLUDED