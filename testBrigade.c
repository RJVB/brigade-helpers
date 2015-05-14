/*
 *  testBrigade.c
 *  VOBimportPB
 *
 *  Created by René J.V. Bertin on 20100409.
 *
 */

#include "winixdefs.h"
#include "copyright.h"

IDENTIFY("test suite for Brigade Electronics VOD video file parser");

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "brigade.h"
#include "timing.h"

MGRIs mgri;
TIME_TABLEs time_table;
TIME_ELEMs time_elem;
VODFiles FP;
unsigned char *VOD = NULL, *fpBase;
size_t VODlen= 1024*1024;

#define USE_VMGI
#define DUMP_JPGS	0

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
  char strbuf[512];
  static char prevbuf[512];
  long skipped;
	if( fp->next_unread_VOBDes < fp->current_VOBDes ){
	  //  we just read a new VMGI
		current_vmgi = fp->current_vmgi-1;
	}
	else{
		current_vmgi = fp->current_vmgi;
		if( fp->current_VOBDes >= fp->vmgi.VOB_Ns ){
			fputc( '#', out );
		}
	}
	fprintf( out, "%d.%d\t", current_vmgi, fp->current_VOBDes );
	if( N ){
		skipped = (long)(*fpos - (*prev_Offset + prev_jH->Length - sizeof(VOB_JPEG_Headers)));
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
#ifdef DUMP_SKIPPED_INFO
		{ FOFFSET here = FFTELL(fp->theFile), there = *prev_Offset + prev_jH->Length - sizeof(VOB_JPEG_Headers);
		  FILE *ofp;
		  unsigned char mysteryByte, n;
		  char *tmpName;
		  size_t len = (strlen(fp->theURL)+64), count;
			if( (tmpName = (char*) malloc( len*sizeof(char) )) ){
				fflush( out );
				snprintf( tmpName, len, "%s_%d.%d.dat", fp->theURL, current_vmgi, fp->current_VOBDes );
				if( (ofp = fopen(tmpName, "wb")) ){
					fprintf( ofp, "frame\theaderPos\theaderPosDiff\tjpgPos\tjpgSize\tSkipped\tprev_VOBDes_Offset\tVOBDes_Offset\n" );
					fprintf( out, "%d.%d\t", current_vmgi, fp->current_VOBDes );
					fprintf( ofp, strbuf );
					fprintf( ofp, "Dumping from %lu - %lu:\n", (unsigned long) there, (unsigned long)(*fpos-1) );
					FFSEEK(fp->theFile, there, SEEK_SET );
					n = 0;
					count = 0;
					while( there < *fpos ){
						if( fread( &mysteryByte, sizeof(mysteryByte), 1, fp->theFile ) == 1 ){
						  char c = 0;
//							fwrite( &mysteryByte, sizeof(mysteryByte), 1, ofp ) ;
							if( n == 16 ){
								fputc( '\n', ofp );
								n = 0;
							}
							fprintf( ofp, "%s%02x", (n)? " " : "", mysteryByte );
							if( mysteryByte == (skipped & 0xff) ){
								c= fputc( '*', ofp );
							}
							else if( mysteryByte == ((skipped & 0xff00) >> 8) ){
								c = fputc( '#', ofp );
							}
							else if( skipped < 512 && skipped > 255 ){
								if( mysteryByte == 0xff ){
									c = fputc( '<', ofp );
								}
								else if( mysteryByte == skipped - 255 ){
									c = fputc( '>', ofp );
								}
							}
							if( c == 0 ){
								fputc( ' ', ofp );
							}
							n += 1;
							count += 1;
						}
						there += 1;
					}
					fprintf( ofp, "\nDumped %lu bytes\n", count );
					fclose(ofp);
					// should be unnecessary:
					FFSEEK(fp->theFile, here, SEEK_SET );
				}
				free(tmpName);
			}
		}
#endif
	}
	else{
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

#ifdef REPAIR_VOD
int Correct_VOB_Audio_At_audioPos( VODFiles *fp, uint32_t idx, VOB_Audio_Headers *aH, FOFFSET *audioOffset, uint32_t *audioLength, FILE *dumpFP )
{ int ok = 0;
  size_t r;
  extern int get_VOB_Audio_Header_here( VODFiles *fp, VOB_Audio_Headers *aH );
  extern void update_VODFile( VODFiles *fp, VOD_Parse_Actions last, int curVOB, int nextVOB );

	if( fp && fp->theFile && aH && fp->audioPos && idx < fp->NaudioPos ){
		if( fp->vmgiPos ){
			if( fp->current_vmgi < 0 ){
				FFSEEK(fp->theFile, fp->vmgiPos[0], SEEK_SET );
				update_VODFile(fp, _read_audioPos, fp->current_VOBDes, 0);
				Read_VMGI(fp);
			}
			if( fp->audioPos[idx] > fp->vmgiPos[fp->current_vmgi+1] ){
				FFSEEK(fp->theFile, fp->vmgiPos[fp->current_vmgi+1], SEEK_SET );
				update_VODFile(fp, _read_audioPos, fp->current_VOBDes, 0);
				Read_VMGI(fp);
			}
		}
		r = FFSEEK( fp->theFile, fp->audioPos[idx], SEEK_SET );
		if( r==0 && get_VOB_Audio_Header_here( fp, aH ) ){
		  double timeStamp;
#ifdef __APPLE_CC__
		  extern void AH_Endian_BtoN( VOB_Audio_Headers *aH );
			AH_Endian_BtoN( aH );
#endif
			if( audioOffset ){
				*audioOffset = FFTELL(fp->theFile);
			}
			{
			  // sound length does NOT include the 64 bytes of header length!
			  // also, the length is in 512 bytes sectors...
			  size_t bytelen = aH->Length*512, tnl = 0;
			  unsigned char *ImData, *d;
			  int i;
			  double lAv = 0, rAv = 0;
			  size_t lN = 0, rN = 0;
				if( (ImData = (unsigned char*) malloc( bytelen*sizeof(unsigned char) )) ){
					r = fread( ImData, sizeof(unsigned char), bytelen, fp->theFile );
					if( r > 0 ){
					  int startClip = -1;
						// assume leading zeros are an insert; import <bytelen> bytes after the insert
						for( i = 0, d = ImData ; i < bytelen && *d == 0; i++, d++);
						if( i > 3 && i < bytelen/4 ){
							FFSEEK(fp->theFile, *audioOffset+i, SEEK_SET );
							r = fread( ImData, sizeof(unsigned char), bytelen, fp->theFile );
							d = ImData;
							fprintf( stderr, "Correct_VOB_Audio_At_audioPos(): spooled forward over %d leading null bytes!\n", i );
							tnl += i;
						}
						// find left and right channel averages. Officially, we'd have to take aH->Audio_Channel
						// into account, but things ought to work out OK if we just consider even and uneven bytes.
						// The symptom we're trying to correct is a chunk of zeros in both channels, after all.
						for( i = 0, d = ImData ; i < bytelen ; i+= 2 ){
							if( *d ){
								lAv += *d;
								lN += 1;
							}
							d++;
							if( *d ){
								rAv += *d;
								rN += 1;
							}
							d++;
						}
						lAv /= lN;
						rAv /= rN;
						i = 0;
						d = ImData;
						// search the buffer for chunks of 0, and replace those with 127 (= silence in mu-law data).
						// The "Clip-Fix" effect in Audacity also gives good results, at a 65% setting, but requires more work...
						while( i < bytelen ){
							if( *d == 0 ){
								startClip = i;
								while( *d == 0 && i < bytelen ){
									i++, d++;
								}
								if( startClip >= 0 && i > startClip + 1 ){
								  int j;
									// replace 0 by 127
//									memset( &ImData[startClip], 127, i - startClip );
									for( j = startClip ; j < i ; j++ ){
										if( j % 2 ){
											ImData[j] = rAv;
										}
										else{
											ImData[j] = lAv;
										}
									}
									fprintf( stderr, "replacing 0 with average levels in ImData[%d-%d]\n", startClip, i-1 );
									tnl += i - startClip;
								}
								startClip = -1;
							}
							else{
								i++, d++;
							}
						}
					}
					if( audioLength ){
						*audioLength = bytelen;
					}
//						// test: mute all sound
//						memset( ImData, 127, bytelen );
//						tnl += bytelen;
					if( tnl ){
					  FOFFSET here = FFTELL(fp->theFile);
						FFSEEK( fp->theFile, *audioOffset, SEEK_SET );
						fflush(fp->theFile);
						r = fwrite( ImData, sizeof(unsigned char), bytelen, fp->theFile );
						fflush(fp->theFile);
						FFSEEK( fp->theFile, here, SEEK_SET );
						if( r != bytelen ){
							if( r ){
								fprintf( stderr, "Warning, wrote only %lu of %lu bytes back to file!! (%s)\n",
									    r, bytelen, strerror(errno)
								);
							}
							else{
								fprintf( stderr, "Warning, error writing %lu bytes back to file!! (%s)\n",
									    bytelen, strerror(errno)
								);
							}
						}
					}
					if( dumpFP ){
						r = fwrite( ImData, sizeof(unsigned char), bytelen, dumpFP );
					}
					free(ImData);
				}
				else{
					FFSEEK( fp->theFile, bytelen, SEEK_CUR );
				}
			}
			ok = 1;
			update_VODFile( fp, _read_audioPos, idx, -1 );
		}
		if( !ok ){
			FFSEEK( fp->theFile, fp->curPos, SEEK_SET );
		}
	}
	return ok;
}
#endif

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
  } *readtable = (struct readtables*) calloc(1, sizeof(struct readtables)), *rt;
  unsigned int readtableN = 0;
#if REPAIR_VOD == 2
  char *mode = "r+b";
#else
  char *mode = "rb";
#endif
#ifdef REPAIR_VOD
  unsigned char *readbuf;
#endif
  MPG4_Fragments mp4Frag;

	init_HRTime();
	HRTime_tic();
	if( (fp = open_VODFile( &FP, theURL, mode )) ){
		fprintf( stderr, "Opened VOD file \"%s\"\n", theURL );
#ifdef __APPLE_CC__
		fpBase = FP.theFile->_bf._base;
#endif
		if( (VOD = (unsigned char*) calloc( VODlen, sizeof(unsigned char) )) ){
			FFSEEK( fp->theFile, 0, SEEK_SET );
			VODlen = fread( VOD, sizeof(unsigned char), VODlen, fp->theFile );
			FFSEEK( fp->theFile, 0, SEEK_SET );
		}
//		fprintf( stderr, "\"%s\" : max frequency: %gHz\n", theURL, VODFile_Find_maxRate( fp ) );
		fprintf( stderr, "@@\t average frequency: %gHz\n", VODFile_Find_averageRate(fp) );
#ifdef REPAIR_VOD
		readbuf = (unsigned char*) calloc( 0x200000, 1 );
#endif
		CLEAR_MPG4_FRAGMENT(&mp4Frag);
		VMGICHECK( Read_VMGI( fp ) ){
		  FOFFSET fpos;
			current_vmgi = fp->current_vmgi;
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
#ifndef REPAIR_VOD
			if( N == 0 ){
				fprintf( stdout, "frame\theaderPos\theaderPosDiff\tjpgPos\tjpgSize\tSkipped\tprev_VOBDes_Offset\tVOBDes_Offset\n" );
				fflush( stdout );
				fpos = FFTELL(fp->theFile);
				// we save fp->vmgi because Read_VOB_JPEG_Header_Scan_Next() can update this field to the file's next VMGI
				// when in fact we still want to print out information from the current one...
				vmgi = fp->vmgi;
			}
#endif
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
#ifdef REPAIR_VOD
//			if( readbuf ){
//				fread( readbuf, 0x200000, 1, fp->theFile );
//				FFSEEK(fp->theFile, fpos, SEEK_SET);
//			}
#endif
			for( VOBid = 0; VOBid_Check(fp, VOBid, &mp4Frag); ){
				switch( VODFILE_VOB_TYPE(fp,VOBid) ){
					case VMGI_VOB_TYPE_VIDEO:
						if( (ok = Read_VOB_xPEG_Header( fp, VOBid, &jH, &jpgOffset, &nextFound, &mp4Frag, DUMP_JPGS /*&& (fp->vmgi.InfoType==2)*/ )) ){
#ifndef REPAIR_VOD
							if( ok < 0  ){
								fputs( "((", stderr );
							}
							if( mp4Frag.valid ){
								fprintf( stderr, "MPEG4 fragment #%d.%d at offset %ld(%ld), %d(%lu) bytes of %lu; %hux%hu, %4d%02d%02d::%02d:%02d:%02d flags=%s\n",
									fp->current_VOBDes, mp4Frag.current.frameNr,
									(long) jpgOffset, (long) jpgOffset - (long) mp4Frag.fileOffset,
									mp4Frag.current.byteLength, jH.Length - sizeof(jH), mp4Frag.byteLength,
									jH.Width, jH.Height,
									jH.Date.year, jH.Date.month, jH.Date.day,
									jH.Time.hours, jH.Time.minutes, jH.Time.seconds,
									VOB_JPEG_Flags(&jH)
								);
							}
							else{
								fprintf( stderr, "jpeg image #%d at offset %ld; %hux%hu, %4d%02d%02d::%02d:%02d:%02d flags=%s\n",
									fp->current_VOBDes,
									(long) jpgOffset,
									jH.Width, jH.Height,
									jH.Date.year, jH.Date.month, jH.Date.day,
									jH.Time.hours, jH.Time.minutes, jH.Time.seconds,
									VOB_JPEG_Flags(&jH)
								);
							}
							if( fp->hasGPS ){
#if 0
							  char sent[256];
							  NMEA_GPMRC_Data gps;
								if( Read_GPS_in_JPEG_Pos( fp, jpgOffset, sent, sizeof(sent), &gps ) ){
									fprintf( stderr, "GPS NMEA Sentence %s[%d] ; speed=%g\n", 
										   sent, gps.fieldsRead, gps.speed
									);
								}
#else
								if( fp->gpsData.fieldsRead > 0 ){
									fprintf( stderr, "GPS NMEA Sentence %s[%d] ; speed=%g\n", 
										   fp->nmeaSentence, fp->gpsData.fieldsRead, fp->gpsData.speed
									);
								}
#endif
							}
#endif
							if( ok > 0 ){
								if( rt ){
									rt->frames += 1;
									if( jpgOffset == prev_Offset ){
										rt->doubles += 1;
									}
								}
#ifndef REPAIR_VOD
								tskipped += print_positions( stdout, fp, &vmgi, N, &jH, &prev_jH, jpgOffset, &prev_Offset, &fpos, &prev_fpos );
#else if REPAIR_VOD == 2
								((VOB_Descrs2*)fp->vmgi.VOB_Des)[fp->current_VOBDes].Offset = (uint32_t)( jpgOffset - fp->current_vmgiPos );
#endif
								N += 1;
							}
							if( !mp4Frag.valid || mp4Frag.canIncrementVOBid ){
								VOBid += 1;
							}
						};
						break;
					case VMGI_VOB_TYPE_AUDIO:
						Find_Next_VOB_Audio_Header(fp, NULL, NULL, 1);
						fp->curPos += 1;
						VOBid += 1;
						break;
					default:
						break;
				}
#if REPAIR_VOD == 1
				if( fp->NaudioPos >= fp->vmgiAudioChunks ){
#ifdef DEBUG
					fprintf( stderr, "Found all audio for this VMGI - on to the next!\n" );
#endif
					break;
				}
#endif
			}
#if REPAIR_VOD == 2
			// rewrite the current VMGI with the updated VOB_Des table
			{ FOFFSET vhere = FFTELL(fp->theFile);
				FFSEEK(fp->theFile, fp->current_vmgiPos, SEEK_SET);
				fwrite(&fp->vmgi, sizeof(VMGIs), 1, fp->theFile);
				fflush(fp->theFile);
				FFSEEK(fp->theFile, vhere, SEEK_SET);
			}
#endif
#else	// !USE_VMGI
			while( (ok = Read_VOB_JPEG_Header_Scan_Next( fp, &jH, &jpgOffset, &nextFound, DUMP_JPGS )) || nextFound ){
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
#ifndef REPAIR_VOD
					if( ok < 0 ){
						fputs( "((", stderr );
					}
					fprintf( stderr, "jpeg image #%d:%d at offset %lu; %hux%hu, %4d%02d%02d::%02d:%02d:%02d\n",
						fp->current_vmgi, fp->current_VOBDes, (unsigned long) jpgOffset,
						jH.Width, jH.Height,
						jH.Date.year, jH.Date.month, jH.Date.day,
						jH.Time.hours, jH.Time.minutes, jH.Time.seconds
					);
					fflush( stderr );
#endif
					if( ok > 0 ){
						if( rt ){
							rt->frames += 1;
							if( jpgOffset == prev_Offset ){
								rt->doubles += 1;
							}
						}
#ifndef REPAIR_VOD
						tskipped += print_positions( stdout, fp, &vmgi, N, &jH, &prev_jH, jpgOffset, &prev_Offset, &fpos, &prev_fpos );
#endif
						N += 1;
					}
				}
			}
#endif	// USE_VMGI
			if( strncmp( fp->vmgi.Id, "VOBX", 4 ) == 0 && fp->vmgi.VOB_Ns == 0 ){
				fprintf( stderr, "Encountered an empty VOBX instead of a VOBS - assuming end of recording!\n" );
				break;
			}
		}
		if( VOD ){
			free(VOD);
		}
		// whether or not the scan of the file is complete is up to us to decide, though the routines in brigade.c
		// will set the flag if they encounter EOF.
		fp->scanComplete = 1;
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
#if REPAIR_VOD == 1
			fp = rewind_VODFile(fp, "r+b");
#else
			rewind_VODFile(fp, NULL);
#endif
		}
		else{
#ifdef REPAIR_VOD
			fp = rewind_VODFile(fp, "r+b");
#else
			rewind_VODFile(fp, NULL);
#endif
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
		  FILE *dumpFP = NULL;
		  char *tmpName;
		  int tnl;
			if( (tmpName = (char*) malloc( (tnl = strlen(theURL)+32) * sizeof(char) )) ){
				snprintf( tmpName, tnl, "%s.au1", fp->theURL );
				dumpFP = fopen( tmpName, "wb" );
				free(tmpName), tmpName = NULL;
			}
			for( VOBid = 0, audioDuration = 0 ; VOBid < fp->NaudioPos ; VOBid++ ){
			  uint32_t audioLength;
			  double duration, freq[] = { 8, 11025, 22050, 44100 };
				if( VOBid == 0 || fp->audioPos[VOBid] != fp->audioPos[VOBid-1] ){
#ifndef REPAIR_VOD
					Read_VOB_Audio_Header_From_audioPos( fp, VOBid, &aH, &audioOffset, &audioLength, 1, dumpFP );
#else
					Correct_VOB_Audio_At_audioPos( fp, VOBid, &aH, &audioOffset, &audioLength, dumpFP );
#endif
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
			if( dumpFP ){
				fclose(dumpFP);
			}
		}
		if( audioDuration ) {
			fprintf( stderr, "Total audio duration: %gs\n", audioDuration );
		}
		Finalise_MPG4_Fragment(&mp4Frag);
		close_VODFile(fp);
#ifndef REPAIR_VOD
		fprintf( stderr, "Average skipped bytes per frame: %g\n", tskipped / N );
#endif
#ifdef USE_VMGI
		fprintf( stderr, "Read %lu frames from \"%s\" in %g sec (using VMGI info)\n", N, theURL, HRTime_toc() );
#else
		fprintf( stderr, "Read %lu frames from \"%s\" in %g sec\n", N, theURL, HRTime_toc() );
#endif
#ifndef REPAIR_VOD
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
#else
		if( readbuf ){
			free(readbuf);
		}
#endif
	}
	else{
		fprintf( stderr, "Cannot open \"%s\" (%s)\n", theURL, strerror(errno) );
	}
	return N;
}

#ifndef REPAIR_VOD

#	define TIME_COMPARES	0 /*102400*/
#	define COMPARE_LEN		10240

#if TIME_COMPARES > 0

/* performance comparisons for TIME_COMPARES=10240 and COMPARE_LEN=10240, JPEGTAGTYPE:uint64_t :

	2005 Powermac bi-G5, 1.8Ghz, Mac OS X 10.4.11 compiled with Apple gcc 4.0.1 (VOB_JPEG_HeaderTag_Check() using memcmp)
		sizeof(VMGIs)=2048 ; sizeof(VOB_Descrs)=6 ; sizeof(VOB_JPEG_Headers)=64 ; sizeof(VOB_Audio_Headers)=64 ; sizeof(FOFFSET)=8
		identical: strncmp(a,b,10240) = 0 ; memcmp(a,b,10240) = 0 ; isequal(a,b,10240) = 1
		diff @5120: strncmp(a,b,10240) = -49 ; memcmp(a,b,10240) = -49 ; isequal(a,b,10240) = 0
		identical/strncmp: found 102400 equals in 0.000287346s (3.56365e+08Hz)
		diff@5120/strncmp: found 0 equals in 0.000222814s (4.59576e+08Hz)
		identical/memcmp: found 102400 equals in 0.000223414s (4.58341e+08Hz)
		diff@5120/memcmp: found 0 equals in 0.000220654s (4.64075e+08Hz)
		identical/isequal: found 102400 equals in 2.41314s (42434.4Hz)
		diff@5120/isequal: found 0 equals in 0.915666s (111831Hz)
		Finding <mask> in 10240 random values:
		VOB_JPEG_HeaderTag_Check: found 0 equals in 1.47671s (8.87593e+07Hz)
		VOB_JPEG_HeaderTag_Check8: found 0 equals in 0.230137s (5.69539e+08Hz)
		VOB_JPEG_HeaderTag_Check_BMH: found 0 equals in 2.6247s (4.99379e+07Hz)
		VOB_Audio_HeaderTag_Check: found 0 equals in 2.98484s (8.78252e+07Hz)
		VOB_Audio_HeaderTag_Check4: found 0 equals in 0.588067s (4.45772e+08Hz)
		Finding 1 <mask> in the middle of 10240 random values:
		VOB_JPEG_HeaderTag_Check: found 102400 equals in 1.48394s (8.8327e+07Hz)
		VOB_JPEG_HeaderTag_Check8: found 102400 equals in 0.297419s (4.40697e+08Hz)
		VOB_JPEG_HeaderTag_Check_BMH: found 102400 equals in 2.65638s (4.93424e+07Hz)
		VOB_Audio_HeaderTag_Check: found 102400 equals in 2.96296s (8.84737e+07Hz)
		VOB_Audio_HeaderTag_Check4: found 102400 equals in 0.612588s (4.27929e+08Hz)
		1280 repeats of the mask:
		VOB_JPEG_HeaderTag_Check: found 131072000 equals in 2.72172s (4.81578e+07Hz)
		VOB_JPEG_HeaderTag_Check8: found 131072000 equals in 1.66592s (7.86785e+07Hz)
		VOB_JPEG_HeaderTag_Check_BMH: found 131072000 equals in 32.8918s (3.98494e+06Hz)
		VOB_Audio_HeaderTag_Check: found 262144000 equals in 4.52168s (5.79749e+07Hz)
		VOB_Audio_HeaderTag_Check4: found 262144000 equals in 0.591049s (4.43523e+08Hz)
		61.326 user_cpu 0.111 kernel_cpu 1:01.63 total_time 99.6%CPU {0W 0X 0D 0K 0M 0F 0R 0I 1O 0r 0s 0k 0w 0c}

	mid-2010 Macbook Pro, Core2 Duo @ 2.4Ghz; Mac OS X 10.6.4, compiled with llvm-gcc-4.2, 32bit mode (VOB_JPEG_HeaderTag_Check() using memcmp):
	(notice that VOB_JPEG_HeaderTag_Check8() is 3x slower than on the 5yo G5!!)
		 sizeof(VMGIs)=2048 ; sizeof(VOB_Descrs)=6 ; sizeof(VOB_JPEG_Headers)=64 ; sizeof(VOB_Audio_Headers)=64 ; sizeof(FOFFSET)=8
		 identical: strncmp(a,b,10240) = 0 ; memcmp(a,b,10240) = 0 ; isequal(a,b,10240) = 1
		 diff @5120: strncmp(a,b,10240) = -49 ; memcmp(a,b,10240) = -49 ; isequal(a,b,10240) = 0
		 identical/strncmp: found 102400 equals in 1.974e-06s (5.18745e+10Hz)
		 diff@5120/strncmp: found 0 equals in 1.055e-06s (9.70617e+10Hz)
		 identical/memcmp: found 102400 equals in 1.541e-06s (6.64502e+10Hz)
		 diff@5120/memcmp: found 0 equals in 8.67003e-07s (1.18108e+11Hz)
		 identical/isequal: found 102400 equals in 0.933958s (109641Hz)
		 diff@5120/isequal: found 0 equals in 0.443585s (230846Hz)
		 Finding <mask> in 10240 random values:
		 VOB_JPEG_HeaderTag_Check: found 0 equals in 0.901251s (1.45433e+08Hz)
		 VOB_JPEG_HeaderTag_Check8: found 0 equals in 0.941538s (1.39211e+08Hz)
		 VOB_JPEG_HeaderTag_Check_BMH: found 0 equals in 1.32358s (9.90281e+07Hz)
		 VOB_Audio_HeaderTag_Check: found 0 equals in 0.331117s (7.91697e+08Hz)
		 VOB_Audio_HeaderTag_Check4: found 0 equals in 0.33041s (7.93391e+08Hz)
		 Finding 1 <mask> in the middle of 10240 random values:
		 VOB_JPEG_HeaderTag_Check: found 102400 equals in 0.906063s (1.44661e+08Hz)
		 VOB_JPEG_HeaderTag_Check8: found 102400 equals in 0.941878s (1.3916e+08Hz)
		 VOB_JPEG_HeaderTag_Check_BMH: found 102400 equals in 1.33868s (9.79114e+07Hz)
		 VOB_Audio_HeaderTag_Check: found 102400 equals in 0.332438s (7.88551e+08Hz)
		 VOB_Audio_HeaderTag_Check4: found 102400 equals in 0.330536s (7.93088e+08Hz)
		 1280 repeats of the mask:
		 VOB_JPEG_HeaderTag_Check: found 131072000 equals in 1.65057s (7.94101e+07Hz)
		 VOB_JPEG_HeaderTag_Check8: found 131072000 equals in 0.940276s (1.39397e+08Hz)
		 VOB_JPEG_HeaderTag_Check_BMH: found 131072000 equals in 19.6993s (6.65364e+06Hz)
		 VOB_Audio_HeaderTag_Check: found 262144000 equals in 0.330913s (7.92183e+08Hz)
		 VOB_Audio_HeaderTag_Check4: found 262144000 equals in 0.330536s (7.93087e+08Hz)

	Toshiba Satellite Pentium-M @1.60Ghz, MSWindows XPsp3, MS Visual Studio Express 2008 (VOB_JPEG_HeaderTag_Check() using isequal()):
		sizeof(VMGIs)=2048 ; sizeof(VOB_Descrs)=6 ; sizeof(VOB_JPEG_Headers)=64 ; sizeof(VOB_Audio_Headers)=64 ; sizeof(FOFFSET)=8
		identical: strncmp(a,b,10240) = 0 ; memcmp(a,b,10240) = 0 ; isequal(a,b,10240) = 1
		diff @5120: strncmp(a,b,10240) = -38 ; memcmp(a,b,10240) = -1 ; isequal(a,b,10240) = 0
		identical/strncmp: found 102400 equals in 2.38735s (42892.8Hz)
		diff@5120/strncmp: found 0 equals in 1.10226s (92900.4Hz)
		identical/memcmp: found 102400 equals in 0.509506s (200979Hz)
		diff@5120/memcmp: found 0 equals in 0.264801s (386705Hz)
		identical/isequal: found 102400 equals in 0.000204495s (5.00745e+008Hz)
		diff@5120/isequal: found 0 equals in 0.000141359s (7.24398e+008Hz)
		Finding <mask> in 10240 random values:
		VOB_JPEG_HeaderTag_Check: found 0 equals in 0.776538s (1.6879e+008Hz)
		VOB_JPEG_HeaderTag_Check8: found 0 equals in 0.762046s (1.72e+008Hz)
		VOB_JPEG_HeaderTag_Check_BMH: found 0 equals in 2.40312s (5.45424e+007Hz)
		VOB_Audio_HeaderTag_Check: found 0 equals in 1.55593s (1.6848e+008Hz)
		VOB_Audio_HeaderTag_Check4: found 0 equals in 0.477971s (5.48452e+008Hz)
		Finding 1 <mask> in the middle of 10240 random values:
		VOB_JPEG_HeaderTag_Check: found 102400 equals in 0.793168s (1.65251e+008Hz)
		VOB_JPEG_HeaderTag_Check8: found 102400 equals in 0.836642s (1.56664e+008Hz)
		VOB_JPEG_HeaderTag_Check_BMH: found 102400 equals in 2.40228s (5.45615e+007Hz)
		VOB_Audio_HeaderTag_Check: found 102400 equals in 1.53343s (1.70952e+008Hz)
		VOB_Audio_HeaderTag_Check4: found 102400 equals in 0.469667s (5.58149e+008Hz)
		1280 repeats of the mask:
		VOB_JPEG_HeaderTag_Check: found 131072000 equals in 2.07794s (6.3078e+007Hz)
		VOB_JPEG_HeaderTag_Check8: found 131072000 equals in 1.2456s (1.05228e+008Hz)
		VOB_JPEG_HeaderTag_Check_BMH: found 131072000 equals in 37.5311s (3.49236e+006Hz)
		VOB_Audio_HeaderTag_Check: found 262144000 equals in 3.15682s (8.30405e+007Hz)
		VOB_Audio_HeaderTag_Check4: found 262144000 equals in 0.470981s (5.56591e+008Hz)

*/

#ifdef _MSC_VER
__inline
#else
inline
#endif
int isequal( const unsigned char *a, const unsigned char *b, size_t len )
{ size_t i;
	for( i = 0 ; i < len ; i++, a++, b++ ){
		if( *a != *b ){
			return 0;
		}
	}
	return 1;
}
#endif

#ifdef _MSC_VER
#	define	ISEQUAL(a,b,len)	isequal((a),(b),(len))
#else
#	define	ISEQUAL(a,b,len)	!memcmp((a),(b),(len))
#endif

static
#ifdef _MSC_VER
__inline
#else
inline
#endif
int VOB_JPEG_HeaderTag_Check( unsigned char *tag )
{ static unsigned char mask[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x3E };
	return ISEQUAL( tag, mask, 6 );
}

static
#ifdef _MSC_VER
__inline
#else
inline
#endif
int VOB_Audio_HeaderTag_Check( char *tag )
{ static unsigned char mask[] = { 'A', 'U', '1', '0' };
	return ISEQUAL( tag, mask, 4 );
}

static unsigned char JPEGtag[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x3E, 0, 0 };
#define JPEGTAGTYPE	uint64_t
static JPEGTAGTYPE JPEGmask;

static
#ifdef _MSC_VER
__inline
#else
inline
#endif
int VOB_JPEG_HeaderTag_Check8( JPEGTAGTYPE *tag )
{
	return *((JPEGTAGTYPE*) tag) == JPEGmask;
}

static unsigned char AUDIOtag[] = { 'A', 'U', '1', '0' };
#define AUDIOTAGTYPE	uint32_t
static AUDIOTAGTYPE AUDIOmask;

static
#ifdef _MSC_VER
__inline
#else
inline
#endif
int VOB_Audio_HeaderTag_Check4( AUDIOTAGTYPE *tag )
{
	return *tag == AUDIOmask;
}

#include <limits.h>

// http://en.wikipedia.org/wiki/Boyer-Moore-Horspool_algorithm
/* The constant UCHAR_MAX is assumed to contain the maximum
* value of the input character type. Typically it's 255.
* size_t is an unsigned type for representing sizes.
* If your system doesn't have it, replace with
* unsigned int.
*/

/* Returns a pointer to the first occurrence of "needle"
* within "haystack", or NULL if not found. Works like 
* memmem().
*/
static
#ifdef _MSC_VER
__inline
#else
inline
#endif
const unsigned char *boyermoore_horspool_memmem(const unsigned char* haystack, size_t hlen,
                           const unsigned char* needle, size_t nlen)
{ size_t scan = 0, last;
  size_t bad_char_skip[UCHAR_MAX + 1]; /* Officially called: bad character shift */
	
	/* Sanity checks on the parameters */
	if (nlen <= 0 || !haystack || !needle){
		return NULL;
	}
	
	/* ---- Preprocess ---- */
	/* Initialise the table to default value */
	/* When a character is encountered that does not occur
		* in the needle, we can safely skip ahead for the whole
		* length of the needle.
		*/
	for( scan = 0 ; scan <= UCHAR_MAX; scan = scan + 1 ){
		bad_char_skip[scan] = nlen;
	}
	
	/* C arrays have the first byte at [0], therefore:
		* [nlen - 1] is the last byte of the array. */
	last = nlen - 1;
	
	/* Then populate it with the analysis of the needle */
	for( scan = 0; scan < last; scan = scan + 1 ){
		bad_char_skip[needle[scan]] = last - scan;
	}
	
	/* ---- Do the matching ---- */
	
	/* Search the haystack, while the needle can still be within it. */
	while( hlen >= nlen ){
		/* scan from the end of the needle */
		for( scan = last; haystack[scan] == needle[scan]; scan = scan - 1 ){
			if( scan == 0 ){
			/* If the first byte matches, we've found it. */
				return haystack;
			}
		}
		
		/* otherwise, we need to skip some bytes and start again. 
			Note that here we are getting the skip value based on the last byte
			of needle, no matter where we didn't match. So if needle is: "abcd"
			then we are skipping based on 'd' and that value will be 4, and
			for "abcdd" we again skip on 'd' but the value will be only 1.
			The alternative of pretending that the mismatched character was 
			the last character is slower in the normal case (Eg. finding 
			"abcd" in "...azcd..." gives 4 by using 'd' but only 
			4-2==2 using 'z'.
		*/
		hlen -= bad_char_skip[haystack[last]];
		haystack += bad_char_skip[haystack[last]];
    }
 
    return NULL;
}

static
#ifdef _MSC_VER
__inline
#else
inline
#endif
int VOB_JPEG_HeaderTag_Check_BMH( unsigned char *buffer, size_t buflen )
{ static unsigned char mask[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x3E };
  const unsigned char *found = boyermoore_horspool_memmem( buffer, buflen, mask, 6 );
	if( found ){
		return( (int)( found - buffer ) );
	}
	else{
		return -1;
	}
}

#endif // TIME_COMPARES

main( int argc, char *argv[] )
{ int i;
#ifndef REPAIR_VOD
	fprintf( stderr, "sizeof(VMGIs)=%d ; sizeof(VOB_Descrs)=%d ; sizeof(VOB_JPEG_Headers)=%d ; sizeof(VOB_Audio_Headers)=%d ; sizeof(FOFFSET)=%d\n",
		    sizeof(VMGIs), sizeof(VOB_Descrs), sizeof(VOB_JPEG_Headers), sizeof(VOB_Audio_Headers), sizeof(FOFFSET)
	);
	if( argc == 1 ){
	  char inBuf[256] = "\0";
		fgets( inBuf, sizeof(inBuf), stdin );
		if( strlen(inBuf) ){
			fprintf( stdout, "Just read \"%s\" from stdin\n", inBuf );
		}
		i = isatty(fileno(stdin)) + isatty(fileno(stdout));
	}
#if TIME_COMPARES > 0
	{ unsigned char a[COMPARE_LEN], b[COMPARE_LEN], *tag;
	  size_t eqs;
	  double elapsed;
	  int j;

		if( sizeof(JPEGtag) == sizeof(JPEGmask) ){
			memcpy( &JPEGmask, JPEGtag, sizeof(JPEGtag) );
		}
		else{
			fprintf( stderr, "JPEG tag/mask size mismatch (%d != %d)!\n", sizeof(JPEGtag), sizeof(JPEGmask) );
		}
		if( sizeof(AUDIOtag) == sizeof(AUDIOmask) ){
			memcpy( &AUDIOmask, AUDIOtag, sizeof(AUDIOtag) );
		}
		else{
			fprintf( stderr, "AUDIO tag/mask size mismatch (%d != %d)!\n", sizeof(AUDIOtag), sizeof(AUDIOmask) );
		}
		
		for( i = 0 ; i< COMPARE_LEN ; i++ ){
			a[i] = (rand() & 0xfe) + 1;
		}
		memmove(b, a, COMPARE_LEN);
		fprintf( stderr, "identical: strncmp(a,b,%lu) = %d ; memcmp(a,b,%lu) = %d ; isequal(a,b,%lu) = %d\n",
			    COMPARE_LEN, strncmp(a,b,COMPARE_LEN),
			    COMPARE_LEN, memcmp(a,b,COMPARE_LEN),
			    COMPARE_LEN, isequal(a,b,COMPARE_LEN)
		);
		a[COMPARE_LEN/2] = a[COMPARE_LEN/2]/2 + 1;
		fprintf( stderr, "diff @%lu: strncmp(a,b,%lu) = %d ; memcmp(a,b,%lu) = %d ; isequal(a,b,%lu) = %d\n",
			    COMPARE_LEN/2,
			    COMPARE_LEN, strncmp(a,b,COMPARE_LEN),
			    COMPARE_LEN, memcmp(a,b,COMPARE_LEN),
			    COMPARE_LEN, isequal(a,b,COMPARE_LEN)
		);
		init_HRTime();
		for( i = 0 ; i< COMPARE_LEN ; i++ ){
			a[i] = (rand() & 0xfe) + 1;
		}
		memmove(b, a, COMPARE_LEN);
		HRTime_tic();
		for( i = 0, eqs = 0 ; i < TIME_COMPARES ; i++ ){
			if( !strncmp(a,b,COMPARE_LEN) ){
				eqs += 1;
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "identical/strncmp: found %lu equals in %gs (%gHz)\n", eqs, elapsed, TIME_COMPARES/elapsed );
		a[COMPARE_LEN/2] = a[COMPARE_LEN/2]/2 + 1;
		HRTime_tic();
		for( i = 0, eqs = 0 ; i < TIME_COMPARES ; i++ ){
			if( !strncmp(a,b,COMPARE_LEN) ){
				eqs += 1;
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "diff@%lu/strncmp: found %lu equals in %gs (%gHz)\n", COMPARE_LEN/2, eqs, elapsed, TIME_COMPARES/elapsed );

		for( i = 0 ; i< COMPARE_LEN ; i++ ){
			a[i] = (rand() & 0xfe) + 1;
		}
		memmove(b, a, COMPARE_LEN);
		HRTime_tic();
		for( i = 0, eqs = 0 ; i < TIME_COMPARES ; i++ ){
			if( !memcmp(a,b,COMPARE_LEN) ){
				eqs += 1;
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "identical/memcmp: found %lu equals in %gs (%gHz)\n", eqs, elapsed, TIME_COMPARES/elapsed );
		a[COMPARE_LEN/2] = a[COMPARE_LEN/2]/2 + 1;
		HRTime_tic();
		for( i = 0, eqs = 0 ; i < TIME_COMPARES ; i++ ){
			if( !memcmp(a,b,COMPARE_LEN) ){
				eqs += 1;
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "diff@%lu/memcmp: found %lu equals in %gs (%gHz)\n", COMPARE_LEN/2, eqs, elapsed, TIME_COMPARES/elapsed );

		for( i = 0 ; i< COMPARE_LEN ; i++ ){
			a[i] = (rand() & 0xfe) + 1;
		}
		memmove(b, a, COMPARE_LEN);
		HRTime_tic();
		for( i = 0, eqs = 0 ; i < TIME_COMPARES ; i++ ){
			if( isequal(a,b,COMPARE_LEN) ){
				eqs += 1;
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "identical/isequal: found %lu equals in %gs (%gHz)\n", eqs, elapsed, TIME_COMPARES/elapsed );
		a[COMPARE_LEN/2] = a[COMPARE_LEN/2]/2 + 1;
		HRTime_tic();
		for( i = 0, eqs = 0 ; i < TIME_COMPARES ; i++ ){
			if( isequal(a,b,COMPARE_LEN) ){
				eqs += 1;
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "diff@%lu/isequal: found %lu equals in %gs (%gHz)\n", COMPARE_LEN/2, eqs, elapsed, TIME_COMPARES/elapsed );

//
		fprintf( stderr, "Finding <mask> in %d random values:\n", COMPARE_LEN );
		for( i = 0 ; i< COMPARE_LEN ; i++ ){
			a[i] = (rand() & 0xfe) + 1;
		}
		memmove(b, a, COMPARE_LEN);

		HRTime_tic();
		for( i = 0, eqs = 0 ; i < TIME_COMPARES ; i++ ){
			for( j = 0, tag = a ; j < COMPARE_LEN ; j += 8, tag += 8 ){
				eqs += VOB_JPEG_HeaderTag_Check(tag);
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "VOB_JPEG_HeaderTag_Check: found %lu equals in %gs (%gHz)\n", eqs, elapsed, (TIME_COMPARES*COMPARE_LEN/8)/elapsed );
		
		HRTime_tic();
		for( i = 0, eqs = 0 ; i < TIME_COMPARES ; i++ ){
			for( j = 0, tag = a ; j < COMPARE_LEN ; j += 8, tag += 8 ){
				*((unsigned short*)&tag[6]) = 0;
				eqs += VOB_JPEG_HeaderTag_Check8((JPEGTAGTYPE*)tag);
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "VOB_JPEG_HeaderTag_Check8: found %lu equals in %gs (%gHz)\n", eqs, elapsed, (TIME_COMPARES*COMPARE_LEN/8)/elapsed );
		
		HRTime_tic();
		for( i = 0, eqs = 0; i < TIME_COMPARES; i++ ){
		  int clen = COMPARE_LEN;
			tag = a;
			while( tag ){
			  int idx = VOB_JPEG_HeaderTag_Check_BMH(tag, clen);
				if( idx >= 0 ){
					eqs += 1;
					if( clen > 7 ){
						tag += idx + 1;
						clen -= idx;
					}
				}
				else{
					tag = NULL;
				}
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "VOB_JPEG_HeaderTag_Check_BMH: found %lu equals in %gs (%gHz)\n", eqs, elapsed, (TIME_COMPARES*COMPARE_LEN/8)/elapsed );
		
		HRTime_tic();
		for( i = 0, eqs = 0 ; i < TIME_COMPARES ; i++ ){
			for( j = 0, tag = a ; j < COMPARE_LEN ; j += 4, tag += 4 ){
				eqs += VOB_Audio_HeaderTag_Check(tag);
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "VOB_Audio_HeaderTag_Check: found %lu equals in %gs (%gHz)\n", eqs, elapsed, (TIME_COMPARES*COMPARE_LEN/4)/elapsed );
		
		HRTime_tic();
		for( i = 0, eqs = 0 ; i < TIME_COMPARES ; i++ ){
			for( j = 0, tag = a ; j < COMPARE_LEN ; j += 4, tag += 4 ){
				eqs += VOB_Audio_HeaderTag_Check4((AUDIOTAGTYPE*)tag);
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "VOB_Audio_HeaderTag_Check4: found %lu equals in %gs (%gHz)\n", eqs, elapsed, (TIME_COMPARES*COMPARE_LEN/4)/elapsed );

//
		fprintf( stderr, "Finding 1 <mask> in the middle of %d random values:\n", COMPARE_LEN );
		memcpy( &a[COMPARE_LEN/2], JPEGtag, 8 );

		HRTime_tic();
		for( i = 0, eqs = 0 ; i < TIME_COMPARES ; i++ ){
			for( j = 0, tag = a ; j < COMPARE_LEN ; j += 8, tag += 8 ){
				eqs += VOB_JPEG_HeaderTag_Check(tag);
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "VOB_JPEG_HeaderTag_Check: found %lu equals in %gs (%gHz)\n", eqs, elapsed, (TIME_COMPARES*COMPARE_LEN/8)/elapsed );
		
		HRTime_tic();
		for( i = 0, eqs = 0 ; i < TIME_COMPARES ; i++ ){
			for( j = 0, tag = a ; j < COMPARE_LEN ; j += 8, tag += 8 ){
				*((unsigned short*)&tag[6]) = 0;
				eqs += VOB_JPEG_HeaderTag_Check8((JPEGTAGTYPE*)tag);
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "VOB_JPEG_HeaderTag_Check8: found %lu equals in %gs (%gHz)\n", eqs, elapsed, (TIME_COMPARES*COMPARE_LEN/8)/elapsed );
		
		HRTime_tic();
		for( i = 0, eqs = 0; i < TIME_COMPARES; i++ ){
		  int clen = COMPARE_LEN;
			tag = a;
			while( tag ){
			  int idx = VOB_JPEG_HeaderTag_Check_BMH(tag, clen);
				if( idx >= 0 ){
					eqs += 1;
					if( clen > 7 ){
						tag += idx + 1;
						clen -= idx;
					}
				}
				else{
					tag = NULL;
				}
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "VOB_JPEG_HeaderTag_Check_BMH: found %lu equals in %gs (%gHz)\n", eqs, elapsed, (TIME_COMPARES*COMPARE_LEN/8)/elapsed );
		
		memcpy( &a[COMPARE_LEN/2], AUDIOtag, 4 );
		HRTime_tic();
		for( i = 0, eqs = 0 ; i < TIME_COMPARES ; i++ ){
			for( j = 0, tag = a ; j < COMPARE_LEN ; j += 4, tag += 4 ){
				eqs += VOB_Audio_HeaderTag_Check(tag);
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "VOB_Audio_HeaderTag_Check: found %lu equals in %gs (%gHz)\n", eqs, elapsed, (TIME_COMPARES*COMPARE_LEN/4)/elapsed );
		
		HRTime_tic();
		for( i = 0, eqs = 0 ; i < TIME_COMPARES ; i++ ){
			for( j = 0, tag = a ; j < COMPARE_LEN ; j += 4, tag += 4 ){
				eqs += VOB_Audio_HeaderTag_Check4((AUDIOTAGTYPE*)tag);
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "VOB_Audio_HeaderTag_Check4: found %lu equals in %gs (%gHz)\n", eqs, elapsed, (TIME_COMPARES*COMPARE_LEN/4)/elapsed );

//
		fprintf( stderr, "%d repeats of the mask:\n", COMPARE_LEN/8 );
		for( j = 0, tag = a ; j < COMPARE_LEN ; j += 8, tag += 8 ){
			memcpy( tag, JPEGtag, 8 );
		}

		HRTime_tic();
		for( i = 0, eqs = 0 ; i < TIME_COMPARES ; i++ ){
			for( j = 0, tag = a ; j < COMPARE_LEN ; j += 8, tag += 8 ){
				eqs += VOB_JPEG_HeaderTag_Check(tag);
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "VOB_JPEG_HeaderTag_Check: found %lu equals in %gs (%gHz)\n", eqs, elapsed, (TIME_COMPARES*COMPARE_LEN/8)/elapsed );
		
		HRTime_tic();
		for( i = 0, eqs = 0 ; i < TIME_COMPARES ; i++ ){
			for( j = 0, tag = a ; j < COMPARE_LEN ; j += 8, tag += 8 ){
				*((unsigned short*)&tag[6]) = 0;
				eqs += VOB_JPEG_HeaderTag_Check8((JPEGTAGTYPE*)tag);
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "VOB_JPEG_HeaderTag_Check8: found %lu equals in %gs (%gHz)\n", eqs, elapsed, (TIME_COMPARES*COMPARE_LEN/8)/elapsed );
		
		HRTime_tic();
		for( i = 0, eqs = 0; i < TIME_COMPARES; i++ ){
		  int clen = COMPARE_LEN;
			tag = a;
			while( tag ){
			  int idx = VOB_JPEG_HeaderTag_Check_BMH(tag, clen);
				if( idx >= 0 ){
					eqs += 1;
					if( clen > 7 ){
						tag += idx + 1;
						clen -= idx;
					}
				}
				else{
					tag = NULL;
				}
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "VOB_JPEG_HeaderTag_Check_BMH: found %lu equals in %gs (%gHz)\n", eqs, elapsed, (TIME_COMPARES*COMPARE_LEN/8)/elapsed );
		
		for( j = 0, tag = a ; j < COMPARE_LEN ; j += 4, tag += 4 ){
			memcpy( tag, AUDIOtag, 4 );
		}
		HRTime_tic();
		for( i = 0, eqs = 0 ; i < TIME_COMPARES ; i++ ){
			for( j = 0, tag = a ; j < COMPARE_LEN ; j += 4, tag += 4 ){
				eqs += VOB_Audio_HeaderTag_Check(tag);
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "VOB_Audio_HeaderTag_Check: found %lu equals in %gs (%gHz)\n", eqs, elapsed, (TIME_COMPARES*COMPARE_LEN/4)/elapsed );
		
		HRTime_tic();
		for( i = 0, eqs = 0 ; i < TIME_COMPARES ; i++ ){
			for( j = 0, tag = a ; j < COMPARE_LEN ; j += 4, tag += 4 ){
				eqs += VOB_Audio_HeaderTag_Check4((AUDIOTAGTYPE*)tag);
			}
		}
		elapsed = HRTime_toc();
		fprintf( stderr, "VOB_Audio_HeaderTag_Check4: found %lu equals in %gs (%gHz)\n", eqs, elapsed, (TIME_COMPARES*COMPARE_LEN/4)/elapsed );
	}
#endif
#endif
	for( i = 1; i < argc ; i++ ){
		Read_VOD( argv[i] );
	}
#ifdef _MSC_VER
	fprintf( stderr, "Hit <enter>... " ); fflush(stderr);
	getc(stdin);
#endif
	exit(0);
}