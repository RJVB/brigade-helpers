/*!
*	@file ffmpegDemux.c
*
*  libavformat-based demuxer to parse MPEG4 fragments
*  Adapted (C) 2012 RJVB from Perian's ffmpeg-based importing code.
*
*  Copyright(C) 2006 Christoph Naegeli <chn1@mac.com>
*
*  This library is free software; you can redistribute it and/or
*  modify it under the terms of the GNU Lesser General Public
*  License as published by the Free Software Foundation; either
*  version 2.1 of the License, or (at your option) any later version.
*  
*  This library is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
*  Lesser General Public License for more details.
*  
*  You should have received a copy of the GNU Lesser General Public
*  License along with this library; if not, write to the Free Software
*  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*
***************************************************************************
*/

#include <stdio.h>
#ifndef _MSC_VER
#	include <sys/fcntl.h>
#endif
#include "brigade.h"

#include "ffmpegDemux.h"

/*!
	internal representation of an MPEG4 fragment, and the extra info
	required to demux it with libavformat.
 */
typedef struct MemIOContext {
	unsigned char *content;			//!< the MP4 fragment as read from a VOD file
	FOFFSET size, pos;				//!< the fragment's size in bytes and the current position
	void *avio_alloc_context_buffer;	//!< buffer for libavformat IO functions
} MemIOContext;

static int FILEsize(FILE *fp, FOFFSET *size)
{
	if( fp ){
	 FOFFSET pos = FFTELL(fp);
		FFSEEK( fp, 0, SEEK_END );
		*size = FFTELL(fp);
		FFSEEK( fp, pos, SEEK_SET );
		return 0;
	}
	else{
		return (errno)? errno : -50;
	}
}

#ifndef MIN
#	define	MIN(a,b) (((a)<(b))?(a):(b))
#endif /* MIN */

/*!
	function to read from the in-memory content in a MemIOContext
 */
static int memRead( void *opaque, uint8_t *buf, int bufLen )
{ MemIOContext *fp = (MemIOContext*) opaque;
  size_t rem = fp->size - fp->pos, tbRead = MIN( bufLen, rem );
	memmove( buf, &fp->content[fp->pos], tbRead );
	fp->pos += tbRead;
	return tbRead;
}

void ffFreeMemIOContext( MemIOContext **theMemFile )
{
	if( theMemFile && *theMemFile ){
		av_freep(&(*theMemFile)->avio_alloc_context_buffer);
		av_freep(&(*theMemFile)->content);
		av_freep(theMemFile);
	}
}

/* This is the public function to initialise an AVIOContext with an in-memory (MPEG4) fragment */
/*!
	Create a MemIOContext and use it to allocate an AVIOContext for the given FFMemoryDemuxer
 */
static int ffCreateMemIOContext( FFMemoryDemuxer *h, unsigned char *content, FOFFSET size)
{ MemIOContext *theMemFile;
  int buffer_size = 4096;
	if( (theMemFile = (MemIOContext*) av_mallocz(sizeof(MemIOContext)))
	   && (theMemFile->content = (unsigned char*) av_malloc(size))
	   && (theMemFile->avio_alloc_context_buffer = av_malloc(4096))
	){
		memcpy( theMemFile->content, content, size );
		theMemFile->size = size;
		theMemFile->pos = 0;
		h->memIOContext = theMemFile;
		h->inputContext = avio_alloc_context( theMemFile->avio_alloc_context_buffer, 4096,
							    0, theMemFile,
							    memRead, NULL, NULL );
		return 0;
	}
	else{
		ffFreeMemIOContext(&theMemFile);
		h->inputContext = NULL;
		return ENOMEM;
	}
} /* ffCreateMemIOContext() */

void ffFreeMemoryDemuxer(FFMemoryDemuxer **h)
{
	if( h && *h ){
		if( (*h)->opened ){
			if( (*h)->videoCodecContext ){
				avcodec_close((*h)->videoCodecContext);
			}
			avformat_close_input( &(*h)->formatContext );
		}
		av_freep(&(*h)->inputContext);
		if( (*h)->memIOContext ){
			// already deallocated:
			((MemIOContext*)(*h)->memIOContext)->avio_alloc_context_buffer = NULL;
		}
		ffFreeMemIOContext( (MemIOContext**) &(*h)->memIOContext );
		free(*h);
		*h = NULL;
	}
}

static int open_codec_context( int *stream_idx,
						AVFormatContext *fmt_ctx, enum AVMediaType type )
{ int ret;
  AVStream *st;
  AVCodecContext *dec_ctx = NULL;
  AVCodec *dec = NULL;

	ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
	if( ret < 0 ){
#ifdef STANDALONE
		fprintf( stderr, "Could not find %s stream in input\n",
			   av_get_media_type_string(type) );
#endif
		return ret;
	}
	else{
		*stream_idx = ret;
		st = fmt_ctx->streams[*stream_idx];

		/* find decoder for the stream */
		dec_ctx = st->codec;
		dec = avcodec_find_decoder(dec_ctx->codec_id);
		if( !dec ){
#ifdef STANDALONE
			fprintf( stderr, "Failed to find %s codec\n", av_get_media_type_string(type) );
#endif
			return ret;
		}

		if( (ret = avcodec_open2(dec_ctx, dec, NULL)) < 0 ){
#ifdef STANDALONE
			fprintf( stderr, "Failed to open %s codec\n", av_get_media_type_string(type) );
#endif
			return ret;
		}
	}

	return 0;
}

/*!
	Create an FFMemoryDemuxer object. This is the object that allows to demux ('parse')
	the video fragment contained in the content argument
 */
FFMemoryDemuxer *ffCreateMemoryDemuxer( unsigned char *content, FOFFSET size )
{ FFMemoryDemuxer *h = NULL;
  int hOK = 0;
	if( content ){
		ffInit();
		if( (h = (FFMemoryDemuxer*) calloc( 1, sizeof(FFMemoryDemuxer) ))
		   && (h->formatContext = avformat_alloc_context())
		){
			if( ffCreateMemIOContext( h, content, size ) == 0 ){
				// store the new AVIOContext in the pb field so avformat_open_input
				// will not want to open a file
				h->formatContext->pb = h->inputContext;
				// open a channel onto the input data
				hOK = avformat_open_input( &h->formatContext, "memory fragment", NULL, NULL );
				if( hOK == 0 && h->formatContext ){
					h->opened = 1;
					// obtain whatever stream info is available
					hOK = avformat_find_stream_info( h->formatContext, NULL );
				}
				if( hOK >= 0 ){
//#ifdef STANDALONE
					av_dump_format( h->formatContext, 0, "memory fragment", 0);
//#endif
					h->videoStreamID = -1;
					if( open_codec_context( &h->videoStreamID, h->formatContext, AVMEDIA_TYPE_VIDEO ) >= 0 ){
						h->videoCodecContext = h->formatContext->streams[h->videoStreamID]->codec;
					}
					h->current.code = -1;
					av_init_packet( &h->next.pkt );
					h->next.pkt.data = NULL;
					h->next.pkt.size = 0;
					h->next.code = -1;
				}
				else{
					ffFreeMemoryDemuxer(&h);
				}
			}
			else{
			}
		}
	}
	return h;
}

/*!
	read the next waiting packet from the input into the 'next' structure.
 */
int ffMemoryDemuxerReadNextPacket( FFMemoryDemuxer *demux )
{ int ret, i;
	if( (ret = av_read_frame( demux->formatContext, &demux->next.pkt )) >= 0 ){
		if( demux->next.pkt.stream_index >= 0 ){
			demux->next.st = demux->formatContext->streams[demux->next.pkt.stream_index];
			for (i = 0; i < demux->next.st->nb_index_entries; i++) {
				if( demux->next.pkt.dts == demux->next.st->index_entries[i].timestamp ){
					demux->next.header_offset = demux->next.pkt.pos - demux->next.st->index_entries[i].pos;
					break;
				}
			}
		}
		else{
			demux->next.st = NULL;
		}
	}
	else{
		demux->next.st = NULL;
	}
	// store av_read_frame's return code
	demux->next.code = ret;
	return ret;
}

static int ffInited = 0;
void ffInit()
{
	if( !ffInited ){
		av_register_all();
		ffInited = 1;
	}
}
