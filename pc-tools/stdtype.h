#ifndef CST_STDTYPE_H
#define CST_STDTYPE_H

#ifdef HAVE_STDINT

#include <stdint.h>

typedef uint8_t	UINT8;
typedef  int8_t	 INT8;
typedef uint16_t	UINT16;
typedef  int16_t	 INT16;
typedef uint32_t	UINT32;
typedef  int32_t	 INT32;
typedef uint64_t	UINT64;
typedef  int64_t	 INT64;

#else	// ! HAVE_STDINT

typedef unsigned char		UINT8;
typedef   signed char 		 INT8;
typedef unsigned short		UINT16;
typedef   signed short		 INT16;

#ifndef _WINDOWS_H
typedef unsigned int		UINT32;
typedef   signed int		 INT32;
#ifdef _MSC_VER
typedef unsigned __int64	UINT64;
typedef   signed __int64	 INT64;
#else
__extension__ typedef unsigned long long	UINT64;
__extension__ typedef   signed long long	 INT64;
#endif
#endif	// _WINDOWS_H

#endif	// HAVE_STDINT

#endif	// CST_STDTYPE_H
