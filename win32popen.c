/*!
*	@file popen.c - initiate a pipe and a child command
*
*       Copyright (c) Microsoft Corporation. All rights reserved.
*		Adapted 2012 RJVB to open any console windows invisibly.
*
*Purpose:
*       Defines popenEx() and pcloseEx().
*
*******************************************************************************/


// RJVB:
#ifndef __MINGW32__
#	include <crtdbg.h>
#endif

#include <malloc.h>
#include <process.h>
#include <io.h>
#include <fcntl.h>
//#include <internal.h>
// RJVB : from internal.h:
extern int  __cdecl _mtinitlocknum(_In_ int);
__forceinline
void _invoke_watson_if_error(
    errno_t _ExpressionError,
    _In_opt_z_ const wchar_t *_Expression,
    _In_opt_z_ const wchar_t *_Function,
    _In_opt_z_ const wchar_t *_File,
    unsigned int _Line,
    uintptr_t _Reserved
    )
{
    if (_ExpressionError == 0)
    {
        return;
    }
    _invoke_watson(_Expression, _Function, _File, _Line, _Reserved);
}
/* Invoke Watson if _ExpressionError is not 0 and equal to _ErrorValue1 or _ErrorValue2; otherwise simply return _EspressionError */
__forceinline
errno_t _invoke_watson_if_oneof(
    errno_t _ExpressionError,
    errno_t _ErrorValue1,
    errno_t _ErrorValue2,
    _In_opt_z_ const wchar_t *_Expression,
    _In_opt_z_ const wchar_t *_Function,
    _In_opt_z_ const wchar_t *_File,
    unsigned int _Line,
    uintptr_t _Reserved
    )
{
    if (_ExpressionError == 0 || (_ExpressionError != _ErrorValue1 && _ExpressionError != _ErrorValue2))
    {
        return _ExpressionError;
    }
    _invoke_watson(_Expression, _Function, _File, _Line, _Reserved);
    return _ExpressionError;
}
#define _INVOKE_WATSON_IF_ONEOF(expr, errvalue1, errvalue2) _invoke_watson_if_oneof(expr, (errvalue1), (errvalue2), __STR2WSTR(#expr), __FUNCTIONW__, __FILEW__, __LINE__, 0)
#define _INVOKE_WATSON_IF_ERROR(expr) _invoke_watson_if_error((expr), __STR2WSTR(#expr), __FUNCTIONW__, __FILEW__, __LINE__, 0)
#define _ERRCHECK_EINVAL(e)	_INVOKE_WATSON_IF_ONEOF(e, EINVAL, EINVAL)
#define _ERRCHECK(e)		_INVOKE_WATSON_IF_ERROR(e)

#include <errno.h>

//#include <mtdll.h>
// RJVB : from mtdll:
#define _POPEN_LOCK		9	/* lock for _popen/_pclose database */
#define _mlock(l)		_lock(l)
extern void __cdecl _lock(_In_ int _File);
#define _munlock(l)		_unlock(l)
void __cdecl _unlock(_Inout_ int _File);

#include <tchar.h>

// RJVB
#include <windows.h>
extern char * __cdecl _getPATH(_In_z_ const char * _Src, _Out_z_cap_(_SizeInChars) char * _Dst, _In_ size_t _SizeInChars);

#include "win32popen.h"

/* size for pipe buffer */
#define PSIZE     1024

#define STDIN     0
#define STDOUT    1

#define SLASH _T("\\")
#define SLASHCHAR _T('\\')
#define XSLASHCHAR _T('/')
#define DELIMITER _T(";")

// RJVB:
#ifndef _INVALID_PARAMETER
#	ifdef _DEBUG
#		define _INVALID_PARAMETER(s)	_invalid_parameter((s), __FUNCTIONW__, __FILEW__, __LINE__, 0);
#	else
#		define _INVALID_PARAMETER(s)	;
#	endif
#endif
#ifndef _VALIDATE_RETURN
#define _VALIDATE_RETURN( expr, errorcode, retexpr )                           \
    {                                                                          \
        int _Expr_val=!!(expr);                                                \
        _ASSERT_EXPR( ( _Expr_val ), _CRT_WIDE(#expr) );                       \
        if ( !( _Expr_val ) )                                                  \
        {                                                                      \
            errno = errorcode;                                                 \
            _INVALID_PARAMETER(_CRT_WIDE(#expr) );                             \
            return ( retexpr );                                                \
        }                                                                      \
    }
#endif  /* _VALIDATE_RETURN */


/* definitions for table of stream pointer - process handle pairs. the table
 * is created, maintained and accessed by the idtab function. _popen and
 * _pclose gain access to table entries only by calling idtab. Note that the
 * table is expanded as necessary (by idtab) and free table entries are reused
 * (an entry is free if its stream field is NULL), but the table is never
 * contracted.
 */

typedef struct {
	FILE *stream;
	intptr_t prochnd;
	// RJVB
	POpenExProperties *properties;
} IDpair;

/* number of entries in idpairs table
 */
#ifndef _UNICODE
	static unsigned __idtabsiz = 0;
#else  /* _UNICODE */
	extern unsigned __idtabsiz;
#endif  /* _UNICODE */

/* pointer to first table entry
 */
#ifndef _UNICODE
	static IDpair *__idpairs = NULL;
#else  /* _UNICODE */
	extern IDpair *__idpairs;
#endif  /* _UNICODE */

/* function to find specified table entries. also, creates and maintains
 * the table.
 */
static IDpair * __cdecl idtab(FILE *);


/*!
*	FILE *popenEx(cmdstring,type,props) - initiate a pipe and a child command
*
*Purpose:
*       Creates a pipe and asynchronously executes a child copy of the command
*       processor with cmdstring (see system()). If the type string contains
*       an 'r', the calling process can read child command's standard output
*       via the returned stream. If the type string contains a 'w', the calling
*       process can write to the child command's standard input via the
*       returned stream.
*
*Entry:
*       _TSCHAR *cmdstring - command to be executed
*       _TSCHAR *type   - string of the form "r|w[b|t]", determines the mode
*                         of the returned stream (i.e., read-only vs write-only,
*                         binary vs text mode)
*       POpenExProperties *props - structure which configures the exact behaviour
*
*Exit:
*       If successful, returns a stream associated with one end of the created
*       pipe (the other end of the pipe is associated with either the child
*       command's standard input or standard output).
*
*       If an error occurs, NULL is returned.
*
*Exceptions:
*
*******************************************************************************/

FILE *popenEx( const char *cmdstring, const char *type, POpenExProperties *props )
{ int phdls[2];             /* I/O handles for pipe */
  int ph_open[2];           /* flags, set if correspond phdls is open */
  int i1;                   /* index into phdls[] */
  int i2;                   /* index into phdls[] */

  int tm = 0;               /* flag indicating text or binary mode */

  int stdhdl;               /* either STDIN or STDOUT */

  HANDLE newhnd;            /* ...in calls to DuplicateHandle API */

  FILE *pstream = NULL;     /* stream to be associated with pipe */

  HANDLE prochnd;           /* handle for current process */

  _TSCHAR *cmdexe;          /* pathname for the command processor */
  _TSCHAR *argsep = "/c";
  _TSCHAR *envbuf = NULL;   /* buffer for the env variable */
  intptr_t childhnd;        /* handle for child process (cmd.exe) */

  IDpair *locidpair;        /* pointer to IDpair table entry */
  _TSCHAR *buf = NULL, *pfin, *env;
  _TSCHAR *CommandLine;
  size_t CommandLineSize = 0;
  _TSCHAR _type[3] = {0, 0, 0};
  DWORD procFlags = 0;

	/* Info for spawning the child. */
	STARTUPINFO StartupInfo;  /* Info for spawning a child */
	BOOL childstatus = 0;
	PROCESS_INFORMATION ProcessInfo; /* child process information */

	errno_t save_errno;
	
	int fh_lock_held = 0;
	int popen_lock_held = 0;

	/* first check for errors in the arguments */
	_VALIDATE_RETURN( (cmdstring != NULL), EINVAL, NULL );
	_VALIDATE_RETURN( (type != NULL), EINVAL, NULL );

	while( *type == _T(' ') ){
		type++;
	}
	_VALIDATE_RETURN( ((*type == _T('w')) || (*type == _T('r'))), EINVAL, NULL );
	_type[0] = *type;
	++type;
	while( *type == _T(' ') ){
		++type;
	}
	_VALIDATE_RETURN( ((*type == 0) || (*type == _T('t')) || (*type == _T('b'))), EINVAL, NULL );
	_type[1] = *type;

	/* do the _pipe(). note that neither of the resulting handles will
	 * be inheritable.
	 */

	if(  _type[1] == _T('t') ){
		tm = _O_TEXT;
	}
	else if( _type[1] == _T('b') ){
		tm = _O_BINARY;
	}

	tm |= _O_NOINHERIT;

	if( _pipe( phdls, PSIZE, tm ) == -1 ){
		goto error1;
	}

	/* test _type[0] and set stdhdl, i1 and i2 accordingly. */
	if( _type[0] == _T('w') ){
		stdhdl = STDIN;
		i1 = 0;
		i2 = 1;
	}
	else{
		stdhdl = STDOUT;
		i1 = 1;
		i2 = 0;
	}

	/* ASSERT LOCK FOR IDPAIRS HERE!!!! */
//	if( !_mtinitlocknum(_POPEN_LOCK) ){
//		_close( phdls[0] );
//		_close( phdls[1] );
//		return NULL;
//	}
	_mlock( _POPEN_LOCK );
	__try {

		/* set flags to indicate pipe handles are open. note, these are only
		 * used for error recovery.
		 */
		ph_open[0] = ph_open[1] = 1;

		/* get the process handle, it will be needed in some API calls */
		prochnd = GetCurrentProcess();

		if( !DuplicateHandle( prochnd,
						  (HANDLE)_get_osfhandle( phdls[i1] ),
						  prochnd,
						  &newhnd,
						  0L,
						  TRUE,                    /* inheritable */
						  DUPLICATE_SAME_ACCESS )
		){
			goto error2;
		}
		(void) _close( phdls[i1] );
		ph_open[i1] = 0;

		/* associate a stream with phdls[i2]. note that if there are no
		 * errors, pstream is the return value to the caller.
		 */
		if( (pstream = _tfdopen( phdls[i2], _type )) == NULL ){
			goto error2;
		}

		/* next, set locidpair to a free entry in the idpairs table. */
		if( (locidpair = idtab(NULL)) == NULL ){
			goto error3;
		}
		locidpair->properties = props;

		if( props && props->commandShell && *props->commandShell ){
			cmdexe = props->commandShell;
			// NB: argsep can be NULL
			argsep = props->shellCommandArg;
		}
		else{
			/* Find what to use. command.com or cmd.exe */
			if( (_ERRCHECK_EINVAL( _dupenv_s( &envbuf, NULL, _T("COMSPEC")) ) != 0) || (envbuf == NULL) ){
				cmdexe = _T("cmd.exe");
			}
			else{
				cmdexe = envbuf;
			}
		}

		/*
		 * Initialise the variable for passing to CreateProcess
		 */

		memset( &StartupInfo, 0, sizeof(StartupInfo) );
		StartupInfo.cb = sizeof(StartupInfo);

		/* Used by os for duplicating the Handles. */

		StartupInfo.dwFlags = STARTF_USESTDHANDLES;
		StartupInfo.hStdInput = stdhdl == STDIN ? (HANDLE) newhnd : (HANDLE) _get_osfhandle(0);
		StartupInfo.hStdOutput = stdhdl == STDOUT ? (HANDLE) newhnd : (HANDLE) _get_osfhandle(1);
		StartupInfo.hStdError = (props && props->stderrH)? props->stderrH : (HANDLE) _get_osfhandle(2);

		if( argsep ){
			CommandLineSize = _tcslen(cmdexe) + _tcslen(_T(argsep)) + _tcslen(_T("  ")) + (_tcslen(cmdstring)) + 1;
			if( (CommandLine = calloc( CommandLineSize, sizeof(_TSCHAR))) == NULL ){
				goto error3;
			}
			_ERRCHECK( _tcscpy_s(CommandLine, CommandLineSize, cmdexe) );
			_ERRCHECK( _tcscat_s(CommandLine, CommandLineSize, _T(" ")) );
			if( *argsep ){
				_ERRCHECK( _tcscat_s(CommandLine, CommandLineSize, _T(argsep)) );
				_ERRCHECK( _tcscat_s(CommandLine, CommandLineSize, _T(" ")) );
			}
		}
		else{
			// cmdexe is supposed to be the same command as the command (first word) in cmdstring!
			CommandLineSize = (_tcslen(cmdstring)) + 1;
			if( (CommandLine = calloc( CommandLineSize, sizeof(_TSCHAR))) == NULL ){
				goto error3;
			}
		}
		_ERRCHECK( _tcscat_s(CommandLine, CommandLineSize, cmdstring) );
		if( props ){
			if( !props->showWindow ){
				procFlags |= CREATE_NO_WINDOW;
			}
			procFlags |= props->runPriorityClass;
		}
		else{
			procFlags |= CREATE_NO_WINDOW;
		}

		/* Check if cmdexe can be accessed. If yes CreateProcess else try
		 * searching path.
		 */
		save_errno = errno;
		if( _taccess_s(cmdexe, 0) == 0 ){
			childstatus = CreateProcess( (LPTSTR) cmdexe,
									   (LPTSTR) CommandLine,
									   NULL,
									   NULL,
									   TRUE,
									   procFlags,
									   NULL,
									   NULL,
									   &StartupInfo,
									   &ProcessInfo );
		}
		else{
		  TCHAR* envPath = NULL;
		  size_t envPathSize = 0;
			if( (buf = calloc(_MAX_PATH, sizeof(_TSCHAR))) == NULL ){
				free(buf);
				free(CommandLine);
				free(envbuf);
				cmdexe = NULL;
				errno = save_errno;
				goto error3;
			}
			if( _ERRCHECK_EINVAL( _dupenv_s(&envPath, NULL, _T("PATH")) ) != 0 ){
				free(envPath);
				free(buf);
				free(CommandLine);
				free(envbuf);
				cmdexe = NULL;
				errno = save_errno;
				goto error3;
			}
			env = envPath;

			while(
#ifdef WPRFLAG
				 (env = _wgetPATH(env, buf, _MAX_PATH -1))
#else  /* WPRFLAG */
				 (env = _getPATH(env, buf, _MAX_PATH -1))
#endif  /* WPRFLAG */
				 && (*buf)
			){
				pfin = buf + _tcslen(buf) -1;

#ifdef _MBCS
				if( *pfin == SLASHCHAR ){
					if( pfin != _mbsrchr(buf, SLASHCHAR) ){
						_ERRCHECK( strcat_s(buf, _MAX_PATH, SLASH) );
					}
				}
				else if( *pfin != XSLASHCHAR ){
					_ERRCHECK( strcat_s(buf, _MAX_PATH, SLASH) );
				}

#else  /* _MBCS */
				if( *pfin != SLASHCHAR && *pfin != XSLASHCHAR ){
					_ERRCHECK( _tcscat_s(buf, _MAX_PATH, SLASH) );
				}
#endif  /* _MBCS */
				/* check that the final path will be of legal size. if so,
				 * build it. otherwise, return to the caller (return value
				 * and errno rename set from initial call to _spawnve()).
				 */
				if( (_tcslen(buf) + _tcslen(cmdexe)) < _MAX_PATH ){
					_ERRCHECK( _tcscat_s(buf, _MAX_PATH, cmdexe) );
				}
				else{
					break;
				}

				/* Check if buf can be accessed. If yes CreateProcess else try
				 * again.
				 */
				if( _taccess_s(buf, 0) == 0 ){
					childstatus = CreateProcess( (LPTSTR) buf,
											   CommandLine,
											   NULL,
											   NULL,
											   TRUE,
											   procFlags,
											   NULL,
											   NULL,
											   &StartupInfo,
											   &ProcessInfo );
					break;
				}
			}
			free(envPath);
			if( buf && *buf ){
				free(buf);
			}
		}
		free(CommandLine);
		free(envbuf);
		cmdexe = NULL;
		CloseHandle((HANDLE)newhnd);
		errno = save_errno;

		/* check if the CreateProcess was sucessful.
		 */
		if( childstatus ){
			// RJVB: close hThread only if we were successful
			CloseHandle((HANDLE)ProcessInfo.hThread);
			childhnd = (intptr_t)ProcessInfo.hProcess;
		}
		else{
			goto error4;
		}
		locidpair->prochnd = childhnd;
		locidpair->stream = pstream;

		/* success, return the stream to the caller
		 */
		goto done;

		/**
		 * error handling code. all detected errors end up here, entering
		 * via a goto one of the labels. note that the logic is currently
		 * a straight fall-thru scheme (e.g., if entered at error4, the
		 * code for error4, error3,...,error1 is all executed).
		 **********************************************************************/

	error4:         /* make sure locidpair is reusable
				  */
		locidpair->stream = NULL;

	error3:         /* close pstream (also, clear ph_open[i2] since the stream
				  * close will also close the pipe handle)
				  */
		(void) fclose( pstream );
		ph_open[i2] = 0;
		pstream = NULL;

	error2:         /* close handles on pipe (if they are still open)
				  */
		if( ph_open[i1] ){
			_close( phdls[i1] );
		}
		if( ph_open[i2] ){
			_close( phdls[i2] );
		}
	done:
		/* done */;
	}
	__finally {
		_munlock(_POPEN_LOCK);
	}
	
error1:
	return pstream;
}

#ifndef _UNICODE

/*!
*int pcloseEx(pstream) - wait on a child command and close the stream on the
*   associated pipe
*
*Purpose:
*       Closes pstream then waits on the associated child command. The
*       argument, pstream, must be the return value from a previous call to
*       _popen. _pclose first looks up the process handle of child command
*       started by that _popen and does a cwait on it. Then, it closes pstream
*       and returns the exit status of the child command to the caller.
*
*Entry:
*       FILE *pstream - file stream returned by a previous call to _popen
*
*Exit:
*       If successful, _pclose returns the exit status of the child command.
*       The format of the return value is that same as for cwait, except that
*       the low order and high order bytes are swapped.
*
*       If an error occurs, -1 is returned.
*
*Exceptions:
*
*******************************************************************************/

int pcloseEx( FILE *pstream )
{ IDpair *locidpair;        /* pointer to entry in idpairs table */
  int termstat;             /* termination status word */
  int retval = -1;          /* return value (to caller) */
  errno_t save_errno;

	_VALIDATE_RETURN( (pstream != NULL), EINVAL, -1 );

//	if( !_mtinitlocknum(_POPEN_LOCK) ){
//		return -1;
//	}
	_mlock(_POPEN_LOCK);
	__try{
		if( (locidpair = idtab(pstream)) == NULL ){
			/* invalid pstream, exit with retval == -1
			 */
			errno = EBADF;
			goto done;
		}
		/* close pstream
		 */
		fflush(pstream);
		(void) fclose(pstream);

		// RJVB 20121201
		if( locidpair->properties ){
		  FILETIME startT, exitT, kernelT, userT;
			SetPriorityClass( (HANDLE) locidpair->prochnd, locidpair->properties->finishPriorityClass );
			if( GetProcessTimes( locidpair->prochnd, &startT, &exitT, &kernelT, &userT ) ){
			  ULARGE_INTEGER uT, kT;
				uT.LowPart = userT.dwLowDateTime, uT.HighPart = userT.dwHighDateTime;
				kT.LowPart = kernelT.dwLowDateTime, kT.HighPart = kernelT.dwHighDateTime;
				locidpair->properties->times.user = uT.QuadPart * 100e-9;
				locidpair->properties->times.kernel = kT.QuadPart * 100e-9;
			}
		}

		/* wait on the child (copy of the command processor) and all of its
		 * children.
		 */
		save_errno = errno;
		errno = 0;
		if( (_cwait( &termstat, locidpair->prochnd, _WAIT_GRANDCHILD ) != -1)
		   || (errno == EINTR)
		){
			retval = termstat;
		}
		errno = save_errno;

		/* Mark the IDpairtable entry as free (note: prochnd was closed by the
		 * preceding call to _cwait).
		 */
		locidpair->stream = NULL;
		locidpair->prochnd = 0;

		/* only return path!
		 */
	done:;
	}
	__finally {
		_munlock(_POPEN_LOCK);
	}

	return(retval);
}

#endif  /* _UNICODE */

/*!
* static IDpair * idtab(FILE *pstream) - find an idpairs table entry
*
*Purpose:
*   Find an entry in the idpairs table.  This function finds the entry the
*   idpairs table entry corresponding to pstream. In the case where pstream
*   is NULL, the entry being searched for is any free entry. In this case,
*   idtab will create the idpairs table if it doesn't exist, or expand it (by
*   exactly one entry) if there are no free entries.
*
*   [MTHREAD NOTE:  This routine assumes that the caller has acquired the
*   idpairs table lock.]
*
*Entry:
*   FILE *pstream - stream corresponding to table entry to be found (if NULL
*                   then find any free table entry)
*
*Exit:
*   if successful, returns a pointer to the idpairs table entry. otherwise,
*   returns NULL.
*
*Exceptions:
*
*******************************************************************************/

static IDpair * __cdecl idtab( FILE *pstream )
{ IDpair * pairptr;       /* ptr to entry */
  IDpair * newptr;        /* ptr to newly malloc'd memory */

	/* search the table. if table is empty, appropriate action should
	 * fall out automatically.
	 */
	for( pairptr = __idpairs ; pairptr < (__idpairs+__idtabsiz) ; pairptr++ ){
		if( pairptr->stream == pstream ){
			break;
		}
	}

	/* if we found an entry, return it. */
	if( pairptr < (__idpairs + __idtabsiz) ){
		return(pairptr);
	}

	/* did not find an entry in the table.  if pstream was NULL, then try
	 * creating/expanding the table. otherwise, return NULL. note that
	 * when the table is created or expanded, exactly one new entry is
	 * produced. this must not be changed unless code is added to mark
	 * the extra entries as being free (i.e., set their stream fields to
	 * to NULL).
	 */
	if( (pstream != NULL) ||
	    ((__idtabsiz + 1) < __idtabsiz) ||
	    ((__idtabsiz + 1) >= (SIZE_MAX / sizeof(IDpair))) ||
	    ((newptr = (IDpair *) _recalloc( (void *)__idpairs, (__idtabsiz + 1), sizeof(IDpair)) ) == NULL)
	){
		/* either pstream was non-NULL or the attempt to create/expand
		 * the table failed. in either case, return a NULL to indicate
		 * failure.
		 */
		return( NULL );
	}

	__idpairs = newptr;             /* new table ptr */
	pairptr = newptr + __idtabsiz;  /* first new entry */
	__idtabsiz++;                   /* new table size */

	return( pairptr );
}


