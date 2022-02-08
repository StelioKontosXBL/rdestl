#ifndef RDESTL_COMMON_H
#define RDESTL_COMMON_H

#ifndef RDESTL_STANDALONE
#	define RDESTL_STANDALONE	1
#endif

#if RDESTL_STANDALONE

#	ifdef _MSC_VER
#		define NOMINMAX
#		undef min
#		undef max
#		define _ALLOW_RTCc_IN_STL
#		include <cassert>
#		include <cstring>
#		define RDE_FORCEINLINE	__forceinline
#	else
#		include <assert.h>
#		include <cstdlib>
#		include <cstring>
#		define RDE_FORCEINLINE	inline
#	endif

#	ifdef _DEBUG
#		undef RDE_DEBUG
#		define RDE_DEBUG	1
#	endif

#	define RDE_ASSERT	assert

// NOOB
#include <cstdint>

namespace rde
{

	typedef unsigned char		uint8;
	typedef unsigned short		uint16;
	typedef signed long			int32;
	typedef unsigned long		uint32;
	#ifdef _MSC_VER
	typedef unsigned __int64	uint64;
	#else
	typedef unsigned long long	uint64;
	#endif

	namespace Sys
	{

		RDE_FORCEINLINE void MemCpy(void* to, const void* from, size_t bytes)
		{
			std::memcpy(to, from, bytes);
		}
		RDE_FORCEINLINE void MemMove(void* to, const void* from, size_t bytes)
		{
			std::memmove(to, from, bytes);
		}
		RDE_FORCEINLINE void MemSet(void* buf, unsigned char value, size_t bytes)
		{
			std::memset(buf, value, bytes);
		}

	} // namespace Sys
} // namespace rde

#endif // #if RDESTL_STANDALONE

namespace rde
{

enum e_noinitialize { noinitialize };

} // namespace rde

//-----------------------------------------------------------------------------
#endif // #ifndef RDESTL_COMMON_H
