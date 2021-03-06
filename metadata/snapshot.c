/*
 * upnpavd - UPNP AV Daemon
 *
 * Copyright (C) 2009 - 2010 Alper Akcan, alper.akcan@gmail.com
 * Copyright (C) 2009 - 2010 CoreCodec, Inc., http://www.CoreCodec.com
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Any non-LGPL usage of this software or parts of this software is strictly
 * forbidden.
 *
 * Commercial non-LGPL licensing of this software is possible.
 * For more info contact CoreCodec through info@corecodec.com
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>


#include "metadata.h"

#if defined(ENABLE_LIBFFMPEG)

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

struct metadata_snapshot_s {
	char *path;
	int videostream;
	AVFormatContext *formatctx;
	AVCodecContext *codecctx;
	AVCodec *codec;
	AVFrame *frame;
	AVFrame *framergb;
	struct SwsContext *convertctx;
	int width;
	int height;
	unsigned char *buffer;
};

metadata_snapshot_t * upnpd_metadata_snapshot_init (const char *path, int width, int height)
{
	int s;
	metadata_snapshot_t *snapshot;

	snapshot = (metadata_snapshot_t *) malloc(sizeof(metadata_snapshot_t));
	if (snapshot == NULL) {
		return NULL;
	}
	memset(snapshot, 0, sizeof(metadata_snapshot_t));

	snapshot->path = strdup(path);
	if (snapshot->path == NULL) {
		free(snapshot);
		return NULL;
	}

	av_register_all();

	if (av_open_input_file(&snapshot->formatctx, snapshot->path, NULL, 0, NULL) != 0) {
		free(snapshot->path);
		free(snapshot);
		return NULL;
	}
	if (av_find_stream_info(snapshot->formatctx) < 0) {
		av_close_input_file(snapshot->formatctx);
		free(snapshot->path);
		free(snapshot);
		return NULL;
	}

	snapshot->videostream = -1;
	for (s = 0; s < snapshot->formatctx->nb_streams; s++) {
		switch (snapshot->formatctx->streams[s]->codec->codec_type) {
			case CODEC_TYPE_VIDEO:
				snapshot->videostream = s;
				break;
			default:
				break;
		}
		if (snapshot->videostream >= 0) {
			break;
		}
	}
	if (snapshot->videostream == -1) {
		av_close_input_file(snapshot->formatctx);
		free(snapshot->path);
		free(snapshot);
		return NULL;
	}

	snapshot->codecctx = snapshot->formatctx->streams[snapshot->videostream]->codec;
	snapshot->codec = avcodec_find_decoder(snapshot->codecctx->codec_id);
	if (snapshot->codec == NULL) {
		av_close_input_file(snapshot->formatctx);
		free(snapshot->path);
		free(snapshot);
		return NULL;
	}
	if (avcodec_open(snapshot->codecctx, snapshot->codec) < 0) {
		av_close_input_file(snapshot->formatctx);
		free(snapshot->path);
		free(snapshot);
		return NULL;
	}

	snapshot->convertctx = sws_getContext(snapshot->codecctx->width, snapshot->codecctx->height, snapshot->codecctx->pix_fmt, width, height, PIX_FMT_BGRA, SWS_BICUBIC, NULL, NULL, NULL);

	snapshot->frame = avcodec_alloc_frame();
	snapshot->framergb = avcodec_alloc_frame();

	s = avpicture_get_size(PIX_FMT_BGRA, width, height);
	snapshot->buffer = (unsigned char *) malloc(sizeof(unsigned char) * s);
	if (snapshot->buffer == NULL) {
		av_free(snapshot->frame);
		av_free(snapshot->framergb);
		avcodec_close(snapshot->codecctx);
		av_close_input_file(snapshot->formatctx);
		free(snapshot->buffer);
		free(snapshot->path);
		free(snapshot);
		return NULL;
	}

	snapshot->width = width;
	snapshot->height = height;
	avpicture_fill((AVPicture *)snapshot->framergb, snapshot->buffer, PIX_FMT_BGRA, width, height);

	printf("file: %s\n", snapshot->path);
	printf("picture size: %d\n", s);
	printf("resolution: %d, %d\n", snapshot->codecctx->width, snapshot->codecctx->height);
	if (snapshot->formatctx->duration > 0) {
		int duration = (snapshot->formatctx->duration / AV_TIME_BASE);
		int hours = duration / 3600;
		int minutes = (duration / 60) % 60;
		int seconds = duration % 60;
		int miliseconds = (snapshot->formatctx->duration / (AV_TIME_BASE / 1000)) % 1000;
		printf("duration: %02d:%02d:%02d.%03d\n", hours, minutes, seconds, miliseconds);
	}

	return snapshot;
}

int upnpd_metadata_snapshot_uninit (metadata_snapshot_t *snapshot)
{
	av_free(snapshot->frame);
	av_free(snapshot->framergb);
	avcodec_close(snapshot->codecctx);
	av_close_input_file(snapshot->formatctx);
	sws_freeContext(snapshot->convertctx);
	free(snapshot->buffer);
	free(snapshot->path);
	free(snapshot);
	return 0;
}

unsigned char * upnpd_metadata_snapshot_obtain (metadata_snapshot_t *snapshot, unsigned int seconds)
{
	double pts;
	int finished;
	AVPacket packet;
	int64_t starttime;
	int64_t timestamp;
	unsigned char *buffer;
	timestamp = (int64_t) (seconds * 1000000);
	if (snapshot->formatctx->start_time != AV_NOPTS_VALUE) {
		starttime = snapshot->formatctx->start_time;
	} else {
		starttime = 0;
	}
	timestamp = (int64_t) (((int64_t) seconds) * 1000000 + starttime);
	printf("seeking to: %0.03f\n", (double) (timestamp / AV_TIME_BASE));
#if 1
	if (av_seek_frame(snapshot->formatctx, -1, timestamp, 0) < 0) {
		printf("could not seek position: %0.03f\n", (double) (timestamp / AV_TIME_BASE));
		return NULL;
	}
#else
	if (avformat_seek_file(snapshot->formatctx, -1, INT64_MIN, timestamp, snapshot->formatctx->duration + starttime, 0) < 0) {
		printf("could not seek position: %0.03f\n", (double) (timestamp / AV_TIME_BASE));
		return NULL;
	}
#endif
	while (av_read_frame(snapshot->formatctx, &packet) >= 0) {
		if (url_ferror(snapshot->formatctx->pb) || url_feof(snapshot->formatctx->pb)) {
			break;
		}
		if (packet.stream_index != snapshot->videostream) {
			continue;
		}
		avcodec_decode_video2(snapshot->codecctx, snapshot->frame, &finished, &packet);
		if (packet.dts == AV_NOPTS_VALUE && snapshot->frame->reordered_opaque != AV_NOPTS_VALUE) {
			pts = snapshot->frame->reordered_opaque;
		} else if (packet.dts != AV_NOPTS_VALUE) {
			pts = packet.dts;
		} else {
			pts = 0;
		}
		pts *= av_q2d(snapshot->formatctx->streams[snapshot->videostream]->time_base);
		if (finished != 0) {
			printf("got frame %0.03f\n", pts);
			sws_scale(snapshot->convertctx, snapshot->frame->data, snapshot->frame->linesize, 0, snapshot->codecctx->height, snapshot->framergb->data, snapshot->framergb->linesize);
			av_free_packet(&packet);
			buffer = snapshot->buffer;
			snapshot->buffer = NULL;
			return buffer;
		}
	}
	av_free_packet(&packet);
	return NULL;
}

#else

metadata_snapshot_t * upnpd_metadata_snapshot_init (const char *path, int width, int height)
{
	return NULL;
}

int upnpd_metadata_snapshot_uninit (metadata_snapshot_t *snapshot)
{
	return 0;
}

unsigned char * upnpd_metadata_snapshot_obtain (metadata_snapshot_t *snapshot, unsigned int seconds)
{
	return NULL;
}

#endif
