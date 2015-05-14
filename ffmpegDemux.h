/*
 *  ffmpegDemux.h
 *  brigade-106
 *
 *  Created by René J.V. Bertin on 20121025.
 *  Copyright 2012 RJVB. All rights reserved.
 *
 */

#ifndef _FFMPEGDEMUX_H

#ifdef _MSC_VER
#	define	inline	__inline
#endif

#include "avformat.h"

typedef struct FFMemoryDemuxer {
	AVFormatContext *formatContext;
	AVIOContext *inputContext;
	void *memIOContext;
	int opened, videoStreamID;
	AVCodecContext *videoCodecContext;
//	void *interface;
	struct {
		AVStream *st;
		AVPacket pkt;
		size_t header_offset;
		int code;
	} current, next;
} FFMemoryDemuxer;

extern FFMemoryDemuxer *ffCreateMemoryDemuxer( unsigned char *content, FOFFSET size );
extern void ffFreeMemoryDemuxer(FFMemoryDemuxer **h);

extern int ffMemoryDemuxerReadNextPacket( FFMemoryDemuxer *demux );

extern void ffInit();

#define _FFMPEGDEMUX_H
#endif