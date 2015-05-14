/*
 *  VODconvert.c
 *  VOBimportPB
 *
 *  Created by René J.V. Bertin on 20121023.
 *
 */

#ifdef _MSC_VER
#	define _CRT_SECURE_NO_WARNINGS
#endif

#include "winixdefs.h"
#include "copyright.h"

IDENTIFY("Brigade Electronics VOD video file converter (requires ffmpeg)");

#include <stdio.h>
#include <stdlib.h>
#if defined(WIN32) || defined(_MSC_VER) || defined(__MINGW32__)
#	include <sys/types.h>
#	include <sys/utime.h>
#	include <time.h>
#else
#	include <unistd.h>
#	include <libgen.h>
#	include <sys/time.h>
#endif
#include <string.h>
#include <errno.h>

#include "brigade.h"
#include "timing.h"

#if ! defined(_WINDOWS) && !defined(WIN32) && !defined(_MSC_VER)
#	include <unistd.h>
#	include <libgen.h>
#	ifdef __APPLE_CC__
		// Mac OS X basename() can modify the input string when not in 'legacy' mode on 10.6
		// and indeed it does. So we use our own which doesn't, and also doesn't require internal
		// storage.
		static const char *lbasename( const char *url )
		{ const char *c = NULL;
			if( url ){
				if( (c =  strrchr( url, '/' )) ){
					c++;
				}
				else{
					c = url;
				}
			}
			return c;
		}
#		define basename(p)	lbasename((p))
#	endif // __APPLE_CC__

#else // WINDOWS
#	include <windows.h>
#	include "win32popen.h"
	extern char *mkdtemp(char*);

	static const char *basename( const char *url )
	{ const char *c = NULL;
		if( url ){
			if( (c =  strrchr( url, '\\' )) ){
				c++;
			}
			else{
				c = url;
			}
		}
		return c;
	}
#endif

MGRIs mgri;
TIME_TABLEs time_table;
TIME_ELEMs time_elem;
VODFiles FP;
unsigned char *fpBase;
size_t VODlen= 1024*1024;

#define USE_VMGI
#define DUMP_JPGS	0
#define PRINT_POSITIONS	0

#ifdef USE_VMGI
#	define VMGICHECK	while
#else
#	define VMGICHECK	if
#endif

typedef struct VOB_Descrs2 {
	uint8_t Type,				// 0:video 1:audio
		Channel;
	uint32_t Offset;			// offset address relative to the current VMGI position (multiple of 2Mb)
} VOB_Descrs2;

long print_positions( FILE *out, VODFiles *fp, VMGIs *vmgi, unsigned long N, VOB_JPEG_Headers *jH,  VOB_JPEG_Headers *prev_jH,
				  FOFFSET jpgOffset, FOFFSET *prev_Offset, FOFFSET *fpos, FOFFSET *prev_fpos )
{ int current_vmgi;
  static int prev_vmgi, prev_VOBDes;
  char strbuf[512] = "\0";
  static char prevbuf[512];
  long skipped;
	if( fp->next_unread_VOBDes < fp->current_VOBDes ){
	  //  we just read a new VMGI
		current_vmgi = fp->current_vmgi-1;
	}
	else{
		current_vmgi = fp->current_vmgi;
#if PRINT_POSITIONS
		if( fp->current_VOBDes >= fp->vmgi.VOB_Ns ){
			fputc( '#', out );
		}
#endif
	}
#if PRINT_POSITIONS
	fprintf( out, "%d.%d\t", current_vmgi, fp->current_VOBDes );
#endif
	if( N ){
		skipped = (long)(*fpos - (*prev_Offset + prev_jH->Length - sizeof(VOB_JPEG_Headers)));
#if PRINT_POSITIONS
		snprintf( strbuf, sizeof(strbuf), "%lu\t%lu\t%lu\t%lu\t%ld\t%hu\t%hu\n",
			(unsigned long)*fpos, (unsigned long)(*fpos-*prev_fpos), (unsigned long) jpgOffset, jH->Length - sizeof(VOB_JPEG_Headers),
			skipped,
			vmgi->VOB_Des[ fp->current_VOBDes-1 ].Offset,
			vmgi->VOB_Des[ fp->current_VOBDes ].Offset
		);
		fprintf( out, strbuf );
		{ unsigned long jHPos = (unsigned long) (jpgOffset - sizeof(VOB_JPEG_Headers)),
				there = (unsigned long) (*prev_Offset + prev_jH->Length - sizeof(VOB_JPEG_Headers));
			fprintf( out, "##\tend prev JPEG @ 0x%lx ; jH@ 0x%lx ; +/- prev VOB_Des offset 0x%lx / 0x%lx ; +/ current VOB_Des offset 0x%lx / 0x%lx ; diff 0x%lx\n",
				    there, jHPos,
				    jHPos - vmgi->VOB_Des[ fp->current_VOBDes-1 ].Offset, jHPos + vmgi->VOB_Des[ fp->current_VOBDes-1 ].Offset,
				    jHPos - vmgi->VOB_Des[ fp->current_VOBDes ].Offset, jHPos + vmgi->VOB_Des[ fp->current_VOBDes ].Offset,
				    (unsigned long) (jpgOffset - *prev_Offset)
			);
		}
#endif
	}
	else{
#if PRINT_POSITIONS
		snprintf( strbuf, sizeof(strbuf), "%lu\tNA\t%lu\t%lu\tNA\tNA\t%hu\n",
			(unsigned long)*fpos, (unsigned long) jpgOffset, jH->Length,
			vmgi->VOB_Des[ fp->current_VOBDes ].Offset
		);
		fputs(strbuf, out );
		{ unsigned long jHPos = (unsigned long) (jpgOffset - sizeof(VOB_JPEG_Headers));
			fprintf( out, "##\tend prev JPEG @ XXXX ; jH@ 0x%lx ; +/ current VOB_Des offset 0x%lx / 0x%lx\n",
				    jHPos,
				    jHPos - vmgi->VOB_Des[ fp->current_VOBDes ].Offset, jHPos + vmgi->VOB_Des[ fp->current_VOBDes ].Offset
			);
		}
#endif
		skipped = -1;
	}
	prev_vmgi = current_vmgi;
	prev_VOBDes = fp->current_VOBDes;
	strcpy( prevbuf, strbuf );
	fflush( out );
	*prev_jH = *jH;
	*prev_Offset = jpgOffset;
	*prev_fpos = *fpos;
	*fpos = FFTELL(fp->theFile);
	// now it's safe to update the local vmgi copy:
	*vmgi = fp->vmgi;
	return skipped;
}

//#define VOBJPEGHDR_FLAG_MULTIPLEXED		(1)
//#define VOBJPEGHDR_FLAG_SLAVEREC			(1 << 1)
//#define VOBJPEGHDR_FLAG_EDOM				(1 << 2)
//#define VOBJPEGHDR_FLAG_HAS_AUDIO			(1 << 3)
//#define VOBJPEGHDR_FLAG_FIXFIELD			(1 << 4)
//#define VOBJPEGHDR_FLAG_PREALARM			(1 << 5)
//#define VOBJPEGHDR_FLAG_FIXFIELD_PREALARM	(1 << 6)
char *VOB_JPEG_Flags( VOB_JPEG_Headers *jH )
{ static char flags[512];
	flags[0] = '\0';
	if( jH->Flags & VOBJPEGHDR_FLAG_MULTIPLEXED ){
		strcat(flags, " MultiPlexed");
	}
	if( jH->Flags & VOBJPEGHDR_FLAG_SLAVEREC ){
		strcat(flags, " SlaveRec");
	}
	if( jH->Flags & VOBJPEGHDR_FLAG_EDOM ){
		strcat(flags, " EDom");
	}
	if( jH->Flags & VOBJPEGHDR_FLAG_HAS_AUDIO ){
		strcat(flags, " HasAudio");
	}
	if( jH->Flags & VOBJPEGHDR_FLAG_FIXFIELD ){
		strcat(flags, " FixField");
	}
	if( jH->Flags & VOBJPEGHDR_FLAG_PREALARM ){
		strcat(flags, " PreAlarm");
	}
	if( jH->Flags & VOBJPEGHDR_FLAG_FIXFIELD_PREALARM ){
		strcat(flags, " FixField_PreAlarm");
	}
	return flags;
}

unsigned long Read_VOD( char *theURL )
{ unsigned long N = 0;
  VODFiles *fp;
  FOFFSET jpgOffset, prev_Offset=0, prev_fpos, audioOffset;
  uint32_t VOBid;
  VOB_JPEG_Headers jH, prev_jH;
  VOB_Audio_Headers aH;
  int current_vmgi;
  int ok, nextFound;
  VMGIs vmgi;
  double tskipped = 0, audioDuration = 0;
  struct readtables {
	  int current_vmgi;
	  FOFFSET current_vmgiPos;
	  int VOB_Ns, vmgiAudioChunks, vmgiVideoFrames;
	  int frames;
	  int doubles;
  } *readtable, *rt;
  unsigned int readtableN = 0;
  char *mode = "rb";
  double frameRate;
  FILE *dumpFP = NULL;
  int pcloseDumpFP = 0, gotPipeSignal;
  size_t jpgFrameCount = 0, mp4FragCount = 0;
  char dirTemp[128] = "VODconverting-XXXXXX", *bURL = NULL;
  MPG4_Fragments mp4Frag;
  struct {
	  VOB_Dates StartDate;
	  VOB_Times StartTime;
	  int isSet;
#if defined(_MSC_VER) || defined(__MINGW32__)
	  struct _utimbuf ut;
#else
	  struct timeval t[2];
#endif
  } StartTimeStamp;
#if defined(_MSC_VER)
  POpenExProperties popProps;
#endif
#ifdef EXTRACTCHANNELVIEWS
  int ff;
  FILE *pfp[4] = {NULL, NULL, NULL, NULL};
#endif

	init_HRTime();
	HRTime_tic();
	if( (fp = open_VODFile( &FP, theURL, mode )) ){
		if( !validate_VODFile( fp, 0 ) ){
			fprintf( stderr, "Not a VOD file \"%s\"\n", theURL );
			close_VODFile(fp);
			return 0;
		}
		fprintf( stderr, "Opened VOD file \"%s\"\n", theURL );
#ifdef __APPLE_CC__
		fpBase = FP.theFile->_bf._base;
#endif
		readtable = (struct readtables*) calloc(1, sizeof(struct readtables));
		mkdtemp(dirTemp);
		bURL = strdup( basename(theURL) );
#if defined(WIN32) || defined(_MSC_VER) || defined(__MINGW32__)
		strcat( dirTemp, "\\" );
#else
		strcat( dirTemp, "/" );
#endif
		fprintf( stderr, "@@\t average frequency: %gHz\n", (frameRate = VODFile_Find_averageRate(fp)) );
		CLEAR_MPG4_FRAGMENT(&mp4Frag);
		memset( &StartTimeStamp, 0, sizeof(StartTimeStamp) );
		VMGICHECK( Read_VMGI( fp ) ){
		  FOFFSET fpos;
			if( fp->hasMPG4 && !dumpFP ){
			   char command[1024];
#if defined(_MSC_VER) || defined(WIN32) || defined(__MINGW32__)
			   const char *mode = "wb";
#else
			   const char *mode = "w";
#endif
				snprintf( command, sizeof(command),
					    "ffmpeg -y -v 1 -i - -vcodec copy \"%s.%4d%02d%02d%02d%02d.mov\"",
						theURL,
						fp->vmgi.StartDate.year, fp->vmgi.StartDate.month, fp->vmgi.StartDate.day,
						fp->vmgi.StartTime.hours, fp->vmgi.StartTime.minutes );
	//			snprintf( command, sizeof(command), "cat > %s.fragments.mov -", theURL );
				fprintf( stderr, "Sending content to \"%s\"\n", command );
#if defined(_MSC_VER)
				popProps.commandShell = "ffmpeg.exe";
				popProps.shellCommandArg = NULL;
				popProps.runPriorityClass = ABOVE_NORMAL_PRIORITY_CLASS;
				popProps.finishPriorityClass = HIGH_PRIORITY_CLASS;
				dumpFP = popenEx( command, mode, &popProps );
#else
				dumpFP = popen( command, mode );
#endif
				if( dumpFP ){
					SetupPipeSignalHandler(&gotPipeSignal);
					pcloseDumpFP = 1;
				}
#ifdef EXTRACTCHANNELVIEWS
				snprintf( command, sizeof(command),
					    "ffmpeg -y -v 1 -i - -vf crop=360:288:0:0 -b:v 500k -vcodec mjpeg \"%s.%4d%02d%02d%02d%02d-camera1.mov\"",
						theURL,
						fp->vmgi.StartDate.year, fp->vmgi.StartDate.month, fp->vmgi.StartDate.day,
						fp->vmgi.StartTime.hours, fp->vmgi.StartTime.minutes );
#ifdef _MSC_VER
				pfp[0] = popenEx( command, mode, &popProps );
#else
				pfp[0] = popen( command, mode );
#endif
				snprintf( command, sizeof(command),
					    "ffmpeg -y -v 1 -i - -vf crop=360:288:360:0 -b:v 500k -vcodec mjpeg \"%s.%4d%02d%02d%02d%02d-camera2.mov\"",
						theURL,
						fp->vmgi.StartDate.year, fp->vmgi.StartDate.month, fp->vmgi.StartDate.day,
						fp->vmgi.StartTime.hours, fp->vmgi.StartTime.minutes );
#ifdef _MSC_VER
				pfp[1] = popenEx( command, mode, &popProps );
#else
				pfp[1] = popen( command, mode );
#endif
				snprintf( command, sizeof(command),
					    "ffmpeg -y -v 1 -i - -vf crop=360:288:0:288 -b:v 500k -vcodec mjpeg \"%s.%4d%02d%02d%02d%02d-camera3.mov\"",
						theURL,
						fp->vmgi.StartDate.year, fp->vmgi.StartDate.month, fp->vmgi.StartDate.day,
						fp->vmgi.StartTime.hours, fp->vmgi.StartTime.minutes );
#ifdef _MSC_VER
				pfp[2] = popenEx( command, mode, &popProps );
#else
				pfp[2] = popen( command, mode );
#endif
				snprintf( command, sizeof(command),
					    "ffmpeg -y -v 1 -i - -vf crop=360:288:360:288 -b:v 500k -vcodec mjpeg \"%s.%4d%02d%02d%02d%02d-camera4.mov\"",
						theURL,
						fp->vmgi.StartDate.year, fp->vmgi.StartDate.month, fp->vmgi.StartDate.day,
						fp->vmgi.StartTime.hours, fp->vmgi.StartTime.minutes );
#ifdef _MSC_VER
				pfp[3] = popenEx( command, mode, &popProps );
#else
				pfp[3] = popen( command, mode );
#endif
#endif // EXTRACTCHANNELVIEWS
			}
			current_vmgi = fp->current_vmgi;
			if( !StartTimeStamp.isSet ){
				StartTimeStamp.StartDate = fp->vmgi.StartDate;
				StartTimeStamp.StartTime = fp->vmgi.StartTime;
				StartTimeStamp.isSet = 1;
#ifdef _MSC_VER
				{ struct tm tmm = {0};
					// Fill out the modified time structure
					// tm_hour is supposed to be in UTC, so we correct for the fact we're in Western continental Europe
					tmm.tm_hour = fp->vmgi.StartTime.hours - 1;
					// guess whether DST was in effect:
					tmm.tm_isdst = (fp->vmgi.StartDate.month > 3 && fp->vmgi.StartDate.month < 11)? 1 : 0;
					tmm.tm_min = fp->vmgi.StartTime.minutes;
					tmm.tm_sec = fp->vmgi.StartTime.seconds;
					tmm.tm_mday = fp->vmgi.StartDate.day;
					// tm_mon is 0 based
					tmm.tm_mon = fp->vmgi.StartDate.month - 1;
					// tm_year starts at 1900
					tmm.tm_year = fp->vmgi.StartDate.year - 1900;
					
					// Convert tm to time_t
					StartTimeStamp.ut.actime = StartTimeStamp.ut.modtime = mktime(&tmm);
				}
#endif
			}
			fprintf( stderr, "## VMGI #%d @ 0x%lx (%.3g%%), %d video, %u audio frames%s%s, %4d%02d%02d::%02d:%02d:%02d - %4d%02d%02d::%02d:%02d:%02d\n",
				fp->current_vmgi, (unsigned long) fp->current_vmgiPos,
				((double)fp->current_vmgiPos)/((double) fp->fileSize) * 100.0,
				fp->vmgiVideoFrames, fp->vmgiAudioChunks,
				(fp->vmgi.VOB_Ns != fp->vmgiVideoFrames+fp->vmgiAudioChunks)? " plus something else" : "",
				(fp->vmgi.InfoType==2)? ", GPS" : "",
				fp->vmgi.StartDate.year, fp->vmgi.StartDate.month, fp->vmgi.StartDate.day,
				fp->vmgi.StartTime.hours, fp->vmgi.StartTime.minutes, fp->vmgi.StartTime.seconds,
				fp->vmgi.EndDate.year, fp->vmgi.EndDate.month, fp->vmgi.EndDate.day,
				fp->vmgi.EndTime.hours, fp->vmgi.EndTime.minutes, fp->vmgi.EndTime.seconds
			);
			fflush( stderr );
			if( N == 0 ){
				fpos = FFTELL(fp->theFile);
				// we save fp->vmgi because Read_VOB_JPEG_Header_Scan_Next() can update this field to the file's next VMGI
				// when in fact we still want to print out information from the current one...
				vmgi = fp->vmgi;
			}
#ifdef USE_VMGI
			readtableN += 1;
			if( (readtable = (struct readtables*) realloc( readtable, readtableN*sizeof(struct readtables) )) ){
				rt = &readtable[readtableN-1];
				rt->current_vmgi = current_vmgi;
				rt->current_vmgiPos = fp->vmgiPos[current_vmgi];
				rt->VOB_Ns = fp->vmgi.VOB_Ns;
				rt->vmgiAudioChunks = fp->vmgiAudioChunks;
				rt->vmgiVideoFrames = fp->vmgiVideoFrames;
				rt->frames = rt->doubles = 0;
			}
			else{
				rt = NULL;
			}
			// fp and fp->vmgi are always up to date and never ahead of us.
			vmgi = fp->vmgi;
			fpos = FFTELL(fp->theFile);
			fp->readContent = 1;
			for( VOBid = 0; VOBid_Check(fp, VOBid, NULL); VOBid++ ){
				fflush(stdout);
				switch( VODFILE_VOB_TYPE(fp,VOBid) ){
					case VMGI_VOB_TYPE_VIDEO:
						if( (ok = Read_VOB_xPEG_Header( fp, VOBid, &jH, &jpgOffset, &nextFound, &mp4Frag, DUMP_JPGS /*&& (fp->vmgi.InfoType==2)*/ )) ){
							if( ok < 0  ){
								fputs( "((", stderr );
							}
							if( VOB_Header_isJPEG( (VOB_Headers_Common*)&jH ) ){
							  size_t nameLen = (strlen(theURL)+strlen(dirTemp)+34)*sizeof(char);
							  char *tmpName = (char*) malloc(nameLen);
								if( tmpName ){
									snprintf( tmpName, nameLen, "%s%s.%04ld.jpg", dirTemp, bURL, fp->decodedFrames );
									if( dumpFP ){
										fclose(dumpFP);
									}
									if( (dumpFP = fopen(tmpName, "wb")) ){
										fwrite( fp->lastReadContent, sizeof(unsigned char),
											  fp->lastReadContentByteLength, dumpFP );
										fclose(dumpFP); dumpFP = NULL;
										jpgFrameCount += 1;
									}
									free(tmpName);
								}
								if( VOBid == 0 || VOBid == fp->vmgi.VOB_Ns - 1 ){
									fprintf( stderr, "jpeg image #%d at offset %ld; %hux%hu, %4d%02d%02d::%02d:%02d:%02d flags=%s\n",
										VOBid, (long) jpgOffset,
										jH.Width, jH.Height,
										jH.Date.year, jH.Date.month, jH.Date.day,
										jH.Time.hours, jH.Time.minutes, jH.Time.seconds,
										VOB_JPEG_Flags(&jH)
									);
									if( VOBid == 0 ){
										fputs( "...\n", stderr );
									}
								}
							}
							else if( VOB_Header_isMPG4( (VOB_Headers_Common*)&jH ) ){
								if( !dumpFP ){
								  size_t nameLen = (strlen(theURL)+strlen(dirTemp)+34)*sizeof(char);
								  char *tmpName = (char*) malloc(nameLen);
									if( tmpName ){
										snprintf( tmpName, nameLen, "%s%s.fragments.mp4", dirTemp, bURL, fp->decodedFrames );
										dumpFP = fopen(tmpName, "wb");
										free(tmpName);
									}
								}
								if( dumpFP ){
									fwrite( fp->lastReadContent, sizeof(unsigned char),
										  fp->lastReadContentByteLength, dumpFP );
									if( gotPipeSignal ){
#ifdef _MSC_VER
										pcloseEx(dumpFP);
#else
										pclose(dumpFP);
#endif
										dumpFP = NULL;
										goto done;
									}
									mp4FragCount += 1;
								}
#ifdef EXTRACTCHANNELVIEWS
								for( ff = 0 ; ff < 4 ; ff++ ){
									if( pfp[ff] ){
										fwrite( fp->lastReadContent, sizeof(unsigned char),
											  fp->lastReadContentByteLength, pfp[ff] );
										if( gotPipeSignal ){
#ifdef _MSC_VER
											pcloseEx(pfp[ff]);
#else
											pclose(pfp[ff]);
#endif
											pfp[ff] = NULL;
											goto done;
										}
									}
								}
#endif
#ifndef NO_FFMPEGLIBS_DEPEND
								fprintf( stderr, "MPEG4 fragment #%d.%d at offset %ld(%ld), %d(%lu) bytes of %lu; %hux%hu, %4d%02d%02d::%02d:%02d:%02d flags=%s\n",
									VOBid, mp4Frag.current.frameNr,
									(long) jpgOffset, (long) jpgOffset - (long) mp4Frag.fileOffset,
									mp4Frag.current.byteLength, jH.Length - sizeof(jH), mp4Frag.byteLength,
									jH.Width, jH.Height,
									jH.Date.year, jH.Date.month, jH.Date.day,
									jH.Time.hours, jH.Time.minutes, jH.Time.seconds,
									VOB_JPEG_Flags(&jH)
								);
#else
								fprintf( stderr, "MPEG4 fragment #%d.%d @0x%lx, %lu bytes; %hux%hu, %4d%02d%02d::%02d:%02d:%02d flags=%s\n",
									VOBid, mp4Frag.current.frameNr,
									(long) jpgOffset, mp4Frag.byteLength,
									jH.Width, jH.Height,
									jH.Date.year, jH.Date.month, jH.Date.day,
									jH.Time.hours, jH.Time.minutes, jH.Time.seconds,
									VOB_JPEG_Flags(&jH)
								);
#endif
							}

							if( fp->hasGPS ){
								if( fp->gpsData.fieldsRead > 0 ){
									fprintf( stderr, "\tGPS NMEA Sentence %s[%d] ; speed=%g\n", 
										   fp->nmeaSentence, fp->gpsData.fieldsRead, fp->gpsData.speed
									);
								}
							}
							if( ok > 0 ){
								if( rt ){
									rt->frames += 1;
									if( jpgOffset == prev_Offset ){
										rt->doubles += 1;
									}
								}
								tskipped += print_positions( stdout, fp, &vmgi, N, &jH, &prev_jH, jpgOffset, &prev_Offset, &fpos, &prev_fpos );
								N += 1;
							}
						};
						break;
					case VMGI_VOB_TYPE_AUDIO:
						Find_Next_VOB_Audio_Header(fp, NULL, NULL, 1);
						fp->curPos += 1;
						break;
					default:
						break;
				}
				fflush(stderr);
			}
			fp->readContent = 0;
			if( strncmp( fp->vmgi.Id, "VOBX", 4 ) == 0 && fp->vmgi.VOB_Ns == 0 ){
				fprintf( stderr, "Encountered an empty VOBX instead of a VOBS - assuming end of recording!\n" );
				break;
			}
#else	// !USE_VMGI
			fp->readContent = 1;
			while( (ok = Read_VOB_xPEG_Header_Scan_Next( fp, &jH, &jpgOffset, &nextFound, &mp4Frag, DUMP_JPGS )) || nextFound ){
				if( current_vmgi != fp->current_vmgi || readtableN == 0 ){
					current_vmgi = fp->current_vmgi;
					readtableN += 1;
					fprintf( stderr, "## VMGI #%d @ 0x%lx (%.3g%%), %d video, %u audio frames%s%s, %4d%02d%02d::%02d:%02d:%02d - %4d%02d%02d::%02d:%02d:%02d\n",
						fp->current_vmgi, (unsigned long) fp->current_vmgiPos,
						((double)fp->current_vmgiPos)/((double) fp->fileSize) * 100.0,
						fp->vmgiVideoFrames, fp->vmgiAudioChunks,
						(fp->vmgi.VOB_Ns != fp->vmgiVideoFrames+fp->vmgiAudioChunks)? " plus something else" : "",
						(fp->vmgi.InfoType==2)? ", GPS" : "",
						fp->vmgi.StartDate.year, fp->vmgi.StartDate.month, fp->vmgi.StartDate.day,
						fp->vmgi.StartTime.hours, fp->vmgi.StartTime.minutes, fp->vmgi.StartTime.seconds,
						fp->vmgi.EndDate.year, fp->vmgi.EndDate.month, fp->vmgi.EndDate.day,
						fp->vmgi.EndTime.hours, fp->vmgi.EndTime.minutes, fp->vmgi.EndTime.seconds
					);
					fflush( stderr );
					if( (readtable = (struct readtables*) realloc( readtable, readtableN*sizeof(struct readtables) )) ){
						rt = &readtable[readtableN-1];
						rt->current_vmgi = current_vmgi;
						rt->current_vmgiPos = fp->vmgiPos[current_vmgi];
						rt->VOB_Ns = fp->vmgi.VOB_Ns;
						rt->vmgiAudioChunks = fp->vmgiAudioChunks;
						rt->vmgiVideoFrames = fp->vmgiVideoFrames;
						rt->frames = rt->doubles = 0;
					}
					else{
						rt = NULL;
					}
				}
				if( ok ){
					fflush(stdout);
					if( ok < 0 ){
						fputs( "((", stderr );
					}
					if( VOB_Header_isJPEG( (VOB_Headers_Common*)&jH ) ){
					  size_t nameLen = (strlen(theURL)+strlen(dirTemp)+34)*sizeof(char);
					  char *tmpName = (char*) malloc(nameLen);
						if( tmpName ){
							snprintf( tmpName, nameLen, "%s%s.%04ld.jpg", dirTemp, bURL, fp->decodedFrames );
							if( dumpFP ){
								fclose(dumpFP);
							}
							if( (dumpFP = fopen(tmpName, "wb")) ){
								fwrite( fp->lastReadContent, sizeof(unsigned char),
									  fp->lastReadContentByteLength, dumpFP );
								fclose(dumpFP); dumpFP = NULL;
								jpgFrameCount += 1;
							}
							free(tmpName);
						}
						fprintf( stderr, "jpeg image #%d:%d at offset %lu; %hux%hu, %4d%02d%02d::%02d:%02d:%02d\n",
							fp->current_vmgi, fp->current_VOBDes, (unsigned long) jpgOffset,
							jH.Width, jH.Height,
							jH.Date.year, jH.Date.month, jH.Date.day,
							jH.Time.hours, jH.Time.minutes, jH.Time.seconds
						);
					}
					else if( VOB_Header_isMPG4( (VOB_Headers_Common*)&jH ) ){
						if( !dumpFP ){
						  size_t nameLen = (strlen(theURL)+strlen(dirTemp)+34)*sizeof(char);
						  char *tmpName = (char*) malloc(nameLen);
							if( tmpName ){
								snprintf( tmpName, nameLen, "%s%s.fragments.mp4", dirTemp, bURL, fp->decodedFrames );
								dumpFP = fopen(tmpName, "wb");
								free(tmpName);
							}
						}
						if( dumpFP ){
							fwrite( fp->lastReadContent, sizeof(unsigned char),
								  fp->lastReadContentByteLength, dumpFP );
							mp4FragCount += 1;
						}
						fprintf( stderr, "MPEG4 fragment #%d:%d at offset %lu, %lu bytes; %hux%hu, %4d%02d%02d::%02d:%02d:%02d\n",
							fp->current_vmgi, fp->current_VOBDes, (unsigned long) jpgOffset, fp->lastReadContentByteLength,
							jH.Width, jH.Height,
							jH.Date.year, jH.Date.month, jH.Date.day,
							jH.Time.hours, jH.Time.minutes, jH.Time.seconds
						);
					}
					fflush( stderr );
					if( ok > 0 ){
						if( rt ){
							rt->frames += 1;
							if( jpgOffset == prev_Offset ){
								rt->doubles += 1;
							}
						}
						tskipped += print_positions( stdout, fp, &vmgi, N, &jH, &prev_jH, jpgOffset, &prev_Offset, &fpos, &prev_fpos );
						N += 1;
					}
				}
			}
			fp->readContent = 0;
#endif	// USE_VMGI
		}
done:
		// whether or not the scan of the file is complete is up to us to decide, though the routines in brigade.c
		// will set the flag if they encounter EOF.
		fp->scanComplete = 1;
		if( dumpFP ){
		  char fname[1024];
			if( pcloseDumpFP ){
				fflush(stdout);
				fprintf( stderr, "Waiting for ffmpeg to terminate..." ); fflush(stderr);
#ifdef _MSC_VER
				pcloseEx(dumpFP);
				fprintf( stderr, " took %g user and %g kernel seconds\n",
					   popProps.times.user, popProps.times.kernel );
#else
				pclose(dumpFP);
				fputs( "\n", stderr );
#endif
				snprintf( fname, sizeof(fname), "%s.%4d%02d%02d%02d%02d.mov",
					    theURL, StartTimeStamp.StartDate.year, StartTimeStamp.StartDate.month, StartTimeStamp.StartDate.day,
					    StartTimeStamp.StartTime.hours, StartTimeStamp.StartTime.minutes );
#ifdef _MSC_VER
				_utime( fname, &StartTimeStamp.ut );
#endif
			}
			else{
				fclose(dumpFP);
			}
			dumpFP = NULL;
		}
#ifdef EXTRACTCHANNELVIEWS
		for( ff = 0 ; ff < 4 ; ff++ ){
			if( pfp[ff] ){
#ifdef _MSC_VER
				pcloseEx(pfp[ff]);
#else
				pclose(pfp[ff]);
#endif
				pfp[ff] = NULL;
			}
		}
#endif
		if(0){
		  int aN = 0, pN = fp->NaudioPos;
		  extern int Find_All_VOB_Audio_Headers( VODFiles *fp );
			fprintf( stderr, "Doing an exhaustive scan for audio ... " );
			fflush(stderr);
			aN = Find_All_VOB_Audio_Headers(fp);
			if( aN != pN ){
				fprintf( stderr, " encountered %d chunks", aN );
			}
			fputs( "\n", stderr );
			rewind_VODFile(fp, NULL);
		}
		else{
			rewind_VODFile(fp, NULL);
		}
		if( fp ){
			fprintf( stderr, "Read %d VMGIs and encountered %d audio chunks while scanning for video frames\n",
				    fp->NvmgiPos, fp->NaudioPos
			);
		}
		else{
			fprintf( stderr, "Error reopening file after rewind (%s)\n", strerror(errno) );
		}
		if( fp && fp->NaudioPos ){
		  FILE *audioFP = NULL;
		  char *tmpName;
		  int tnl;
			if( (tmpName = (char*) malloc( (tnl = strlen(theURL)+32) * sizeof(char) )) ){
				snprintf( tmpName, tnl, "%s.au1", fp->theURL );
				audioFP = fopen( tmpName, "wb" );
				free(tmpName), tmpName = NULL;
			}
			for( VOBid = 0, audioDuration = 0 ; VOBid < fp->NaudioPos ; VOBid++ ){
			  uint32_t audioLength;
			  double duration, freq[] = { 8, 11025, 22050, 44100 };
				if( VOBid == 0 || fp->audioPos[VOBid] != fp->audioPos[VOBid-1] ){
					Read_VOB_Audio_Header_From_audioPos( fp, VOBid, &aH, &audioOffset, &audioLength, 1, audioFP );
					if( aH.Audio_Channel == 2 ){
						duration = audioLength / 2 / freq[aH.Audio_Rate];
					}
					else{
						duration = audioLength / freq[aH.Audio_Rate];
					}
					fprintf( stderr, "Audio chunk #%d: %lu bytes at 0x%lx; duration=%gs\n",
						    VOBid, audioLength, (unsigned long) audioOffset, duration
					);
					audioDuration += duration;
				}
			}
			if( audioFP ){
				fclose(audioFP);
			}
		}
		if( audioDuration ) {
			fprintf( stderr, "Total audio duration: %gs\n", audioDuration );
		}
		Finalise_MPG4_Fragment(&mp4Frag);
		close_VODFile(fp);
		fprintf( stderr, "Average skipped bytes per frame: %g\n", tskipped / N );
#ifdef DEBUG
		if( readtable ){
		  int i, AudioChunks = 0;
			for( i = 0; i< readtableN; i++){
				fprintf( stderr, "%03d VMGI #%d@0x%lu\tframes=%d\tVOB_Ns=%d,VideoFrames=%d,AudioChunks=%d",
					    i, readtable[i].current_vmgi, (unsigned long)readtable[i].current_vmgiPos,
					    readtable[i].frames, readtable[i].VOB_Ns, readtable[i].vmgiVideoFrames, readtable[i].vmgiAudioChunks
				);
				if( rt->doubles ){
					fprintf( stderr, "\tdoubles=%d", rt->doubles );
				}
				fputc( '\n', stderr );
				AudioChunks += readtable[i].vmgiAudioChunks;
			}
			free(readtable);
			readtable = NULL;
		}
#endif
		{ char command[1024];
			if( jpgFrameCount ){
				snprintf( command, sizeof(command),
					    "ffmpeg -y -r %g -i %s%s.%%04d.jpg -vcodec copy \"%s.%4d%02d%02d%02d%02d.mov\"",
					    frameRate, dirTemp, bURL,
					    theURL, StartTimeStamp.StartDate.year, StartTimeStamp.StartDate.month, StartTimeStamp.StartDate.day,
					    StartTimeStamp.StartTime.hours, StartTimeStamp.StartTime.minutes );
				system( command );
			}
//			if( mp4FragCount ){
//				snprintf( command, sizeof(command), "ffmpeg -y -i %s%s.fragments.mp4 -vcodec copy %s.converted.mov",
//					    dirTemp, bURL, theURL );
//				system( command );
//			}
#if defined(WIN32) || defined(_MSC_VER) || defined(__MINGW32__)
			snprintf( command, sizeof(command), "rmdir /s /q %s", dirTemp );
#else
			snprintf( command, sizeof(command), "rm -rf %s", dirTemp );
#endif
			system(command);
			snprintf( command, sizeof(command), "%s.%4d%02d%02d%02d%02d.mov",
				    theURL, StartTimeStamp.StartDate.year, StartTimeStamp.StartDate.month, StartTimeStamp.StartDate.day,
				    StartTimeStamp.StartTime.hours, StartTimeStamp.StartTime.minutes );
#ifdef _MSC_VER
			_utime( command, &StartTimeStamp.ut );
#endif
			free(bURL);
		}
#ifdef USE_VMGI
		if( mp4Frag.valid ){
			fprintf( stderr, "Read %lu %gHz 1sec fragments from \"%s\" in %g sec (using VMGI info)\n",
				   N, frameRate, theURL, HRTime_toc() );
		}
		else{
			fprintf( stderr, "Read %lu frames from \"%s\" in %g sec (using VMGI info)\n", N, theURL, HRTime_toc() );
		}
#else
		fprintf( stderr, "Read %lu frames from \"%s\" in %g sec\n", N, theURL, HRTime_toc() );
#endif
	}
	else{
		fprintf( stderr, "Cannot open \"%s\" (%s)\n", theURL, strerror(errno) );
	}
	return N;
}


main( int argc, char *argv[] )
{ int i;
  int hasError = 0;
	if( argc <= 1 ){
		fprintf( stderr, "Usage: %s file1.VOD [file2.VOD,...]\n", argv[0] );
		if( system("ffmpeg -version") ){
			fprintf( stderr, "\tffmpeg must be installed and available on the PATH !\n" );
		}
	}
	for( i = 1; i < argc ; i++ ){
		if( !Read_VOD( argv[i] ) ){
			hasError += 1;
		}
	}
#ifdef _MSC_VER
	if( hasError ){
		fprintf( stderr, "Hit <enter>... " ); fflush(stderr);
		getc(stdin);
	}
#endif
	exit(0);
}
