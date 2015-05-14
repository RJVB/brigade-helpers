/*!
 *  @file win32popen.h
 *	header file for RJVB's adaptation of the MS Visual Studio Express 2010 _popen function
 *
 *  Created by René J.V. Bertin on 20121201.
 *  Copyright 2012 RJVB. All rights reserved.
 *
 */

#ifndef _WIN32POPEN_H

#include <stdio.h>
#include <stdlib.h>

#if defined(__cplusplus)
#	include <windows.h>
	extern "C" {
#endif

/*!
	parameters for the popenEx function, which determine how the cmdstring passed to
	popenEx is executed via the CreateProcess function, and at what priority the command
	will be executed. The default setting (NULL pointers for commandShell and shellCommandArg)
	will cause either cmd.exe or command.exe to be used to launch the desired command.
	Set commandShell to the desired command and shellCommandArg to NULL in order to execute
	cmdstring as indicated.
 */
typedef struct POpenExProperties {
	const char *commandShell,		//!< if not a NULL or empty string, the absolute path to the command shell to use
								//!< defaults to either cmd.exe or command.exe
		*shellCommandArg;			//!< used only when commandShell is specified; the equivalent to the cmd.exe '/c' argument.
								//!< Can be NULL if commandShell is set to the command to be executed (otherwise, the
								//!< command line passed to CreateProcess will contain the command name twice)
#ifdef __cplusplus
	const HANDLE stderrH;			//!< open HANDLE to use for standard error instead of the default
#else
	// MSVC accepts a const char * as an lvalue but not a const HANDLE ??!
	HANDLE stderrH;				//!< open HANDLE to use for standard error instead of the default
#endif
	int showWindow;				//!< whether to let the child create a window. Defaults to off.
	unsigned short runPriorityClass,	//!< priority class to apply to the launched command
		finishPriorityClass;		//!< priority class to apply when waiting for the launched command to terminate, in pcloseEx()
	struct {
		double user, kernel;		//!< set in pcloseEx(); times spent by the launched command.
								//!< Irrelevant if using cmd.exe or command.exe ...!
	} times;
#ifdef __cplusplus
	POpenExProperties()
		: commandShell(NULL), shellCommandArg(NULL), stderrH(NULL)
	{
		runPriorityClass = 0;
		finishPriorityClass = 0;
		times.user = times.kernel = -1;
	}
	POpenExProperties( unsigned short runPrior, unsigned short finishPrior )
		: commandShell(NULL), shellCommandArg(NULL), stderrH(NULL)
	{
		runPriorityClass = runPrior;
		finishPriorityClass = finishPrior;
		times.user = times.kernel = -1;
	}
	POpenExProperties( char *cS, char *sHA )
		: commandShell((const char*)cS), shellCommandArg((const char*)sHA), stderrH(NULL)
	{
		runPriorityClass = 0;
		finishPriorityClass = 0;
		times.user = times.kernel = -1;
	}
	POpenExProperties( char *cS, char *sHA, unsigned short runPrior, unsigned short finishPrior, HANDLE errOutput )
		: commandShell((const char*)cS), shellCommandArg((const char*)sHA), stderrH((const HANDLE)errOutput)
	{
		runPriorityClass = runPrior;
		finishPriorityClass = finishPrior;
		times.user = times.kernel = -1;
	}
#endif
} POpenExProperties;

/*!
	Opens a pipe to (mode = "w") or from (mode = "r") the specified command, as in the POSIX popen()
	function. props can be NULL or a pointer to a structure in order to control the exact way
	the command is launched. Note that this pointer must have the same scope as the returned FILE pointer;
	it is referenced in pcloseEx().
 */
extern FILE *popenEx( const char *cmdstring, const char *mode, POpenExProperties *props );
extern int pcloseEx( FILE *pstream );

#if defined(__cplusplus)
}
#endif

#define _WIN32POPEN_H
#endif
