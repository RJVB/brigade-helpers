/*
 *  wininix.h
 *
 *  Created by René J.V. Bertin on 20100719.
 *  Copyright 2010 __MyCompanyName__. All rights reserved.
 *
 */

#if (defined(_WINDOWS) || defined(WIN32) || defined(_MSC_VER)) && !defined(_WININIX_H)

// see https://code.google.com/p/msinttypes/ !

#ifdef _MSC_VER

#	define	_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES	1
#	include	<stdio.h>
#	include <errno.h>

	static FILE *_fopen_(const char *name, const char *mode)
	{ FILE *fp = NULL;
		errno = fopen_s( &fp, name, mode );
		return fp;
	}
#	define fopen(name,mode)	_fopen_((name),(mode))
#endif


#define	snprintf					_snprintf

#ifndef _MSC_VER
// strings.h is included in the Quicktime SDK!
#	include <strings.h>
#endif

#define	strncasecmp(a,b,n)			_strnicmp((a),(b),(n))


#define _WININIX_H
#endif