/*
 * (C) Copyright 2010
 * Steve Chang
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
*/
#include <stdio.h>
#include <string.h>

#include "mp4mux.h"

/*
 * add an audio output stream
 */

float t, tincr, tincr2;
int16_t *samples;
uint8_t *audio_outbuf;
int audio_outbuf_size;
int audio_input_frame_size;

static AVStream *add_audio_stream(AVFormatContext *oc, enum CodecID codec_id)
{
    AVCodecContext *c;
    AVStream *st;

    st = av_new_stream(oc, 1);
    if (!st) {
        fprintf(stderr, "Could not alloc stream\n");
        exit(1);
    }

    c = st->codec;
    c->codec_id = codec_id;
    c->codec_type = CODEC_TYPE_AUDIO;

    /* put sample parameters */
    c->bit_rate = 64000;
    c->sample_rate = 44100;
    c->channels = 2;

    // some formats want stream headers to be separate
    if(oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;

    return st;
}

static void open_audio(AVFormatContext *oc, AVStream *st)
{
    AVCodecContext *c;
    AVCodec *codec;

    c = st->codec;

    /* find the audio encoder */
    codec = avcodec_find_encoder(c->codec_id);
    if (!codec) {
        fprintf(stderr, "codec not found\n");
        exit(1);
    }

    /* open it */
    if (avcodec_open(c, codec) < 0) {
        fprintf(stderr, "could not open codec\n");
        exit(1);
    }

    /* init signal generator */
    t = 0;
    tincr = 2 * M_PI * 110.0 / c->sample_rate;
    /* increment frequency by 110 Hz per second */
    tincr2 = 2 * M_PI * 110.0 / c->sample_rate / c->sample_rate;

    audio_outbuf_size = 10000;
    audio_outbuf = av_malloc(audio_outbuf_size);

    /* ugly hack for PCM codecs (will be removed ASAP with new PCM
       support to compute the input frame size in samples */
    if (c->frame_size <= 1) {
        audio_input_frame_size = audio_outbuf_size / c->channels;
        switch(st->codec->codec_id) {
        case CODEC_ID_PCM_S16LE:
        case CODEC_ID_PCM_S16BE:
        case CODEC_ID_PCM_U16LE:
        case CODEC_ID_PCM_U16BE:
            audio_input_frame_size >>= 1;
            break;
        default:
            break;
        }
    } else {
        audio_input_frame_size = c->frame_size;
    }
    samples = av_malloc(audio_input_frame_size * 2 * c->channels);
}

static void close_audio(AVFormatContext *oc, AVStream *st)
{
    avcodec_close(st->codec);

    av_free(samples);
    av_free(audio_outbuf);
}

#define STREAM_FRAME_RATE 25 /* 25 images/s */
#define STREAM_PIX_FMT PIX_FMT_YUV420P /* default pix_fmt */

//AVFrame *picture, *tmp_picture;
//uint8_t *video_outbuf;
//int video_outbuf_size;

/* add a video output stream */
static AVStream *add_video_stream(AVFormatContext *oc, enum CodecID codec_id)
{
    AVCodecContext *c;
    AVStream *st;

    st = av_new_stream(oc, 0);
    if (!st) {
        fprintf(stderr, "Could not alloc stream\n");
        exit(1);
    }

    c = st->codec;
    c->codec_id = codec_id;
    c->codec_type = CODEC_TYPE_VIDEO;

    /* put sample parameters */
    c->bit_rate = 4000000;
    /* resolution must be a multiple of two */
    c->width = 640;
    c->height = 480;
    /* time base: this is the fundamental unit of time (in seconds) in terms
       of which frame timestamps are represented. for fixed-fps content,
       timebase should be 1/framerate and timestamp increments should be
       identically 1. */
//    c->time_base.den = STREAM_FRAME_RATE;
    c->time_base.den = 3; /*  90000 / timestamp interval / 2  */
    c->time_base.num = 1;
    c->gop_size = 12; /* emit one intra frame every twelve frames at most */
    c->pix_fmt = STREAM_PIX_FMT;
#if 0    
    if (c->codec_id == CODEC_ID_MPEG2VIDEO) {
        /* just for testing, we also add B frames */
        c->max_b_frames = 2;
    }
    if (c->codec_id == CODEC_ID_MPEG1VIDEO){
        /* Needed to avoid using macroblocks in which some coeffs overflow.
           This does not happen with normal video, it just happens here as
           the motion of the chroma plane does not match the luma plane. */
        c->mb_decision=2;
    }
#endif    
    // some formats want stream headers to be separate
    if(oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;

    return st;
}

static void open_video(AVFormatContext *oc, AVStream *st)
{
    AVCodec *codec;
    AVCodecContext *c;

    c = st->codec;

    /* find the video encoder */
    codec = avcodec_find_decoder(c->codec_id);
    if (!codec) {
        fprintf(stderr, "codec not found\n");
        exit(1);
    }

    /* open the codec */
    if (avcodec_open(c, codec) < 0) {
        fprintf(stderr, "could not open codec\n");
        exit(1);
    }
#if 0
    video_outbuf = NULL;
    if (!(oc->oformat->flags & AVFMT_RAWPICTURE)) {
        /* allocate output buffer */
        /* XXX: API change will be done */
        /* buffers passed into lav* can be allocated any way you prefer,
           as long as they're aligned enough for the architecture, and
           they're freed appropriately (such as using av_free for buffers
           allocated with av_malloc) */
        video_outbuf_size = 200000;
        video_outbuf = av_malloc(video_outbuf_size);
    }

    /* allocate the encoded raw picture */
    picture = alloc_picture(c->pix_fmt, c->width, c->height);
    if (!picture) {
        fprintf(stderr, "Could not allocate picture\n");
        exit(1);
    }

    /* if the output format is not YUV420P, then a temporary YUV420P
       picture is needed too. It is then converted to the required
       output format */
    tmp_picture = NULL;
    if (c->pix_fmt != PIX_FMT_YUV420P) {
        tmp_picture = alloc_picture(PIX_FMT_YUV420P, c->width, c->height);
        if (!tmp_picture) {
            fprintf(stderr, "Could not allocate temporary picture\n");
            exit(1);
        }
    }
#endif
}

static void close_video(AVFormatContext *oc, AVStream *st)
{
    avcodec_close(st->codec);
/*
    av_free(picture->data[0]);
    av_free(picture);
    if (tmp_picture) {
        av_free(tmp_picture->data[0]);
        av_free(tmp_picture);
    }
    */
//    av_free(video_outbuf);
}

extern AVOutputFormat mp4_muxer;
extern URLProtocol file_protocol;

void Mp4mux_Init()
{
  av_register_output_format(&mp4_muxer);
  av_register_protocol(&file_protocol);
}

void print_error(const char *filename, int err)
{
    char errbuf[128];
    const char *errbuf_ptr = errbuf;

    if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
        errbuf_ptr = strerror(AVUNERROR(err));
    fprintf(stderr, "%s: %s\n", filename, errbuf_ptr);
}

static AVFormatContext *context = 0;
//static AVStream *audio_stream = NULL, *video_stream = NULL;
static AVStream *video_stream = NULL;

void Mp4mux_Open(const char *filename)
{
  context = avformat_alloc_context();
  AVOutputFormat *format = av_guess_format("mp4", NULL, NULL);
  if(!format) {
    fprintf(stderr, "Could not find suitable output format\n");
    exit(EXIT_FAILURE);
  }

  format->video_codec = CODEC_ID_H264;
  context->oformat = format;
  snprintf(context->filename, sizeof(context->filename), "%s", filename);

  video_stream = add_video_stream(context, format->video_codec);
  //audio_stream = add_audio_stream(context, format->audio_codec);

  if(av_set_parameters(context, NULL) < 0) {
    fprintf(stderr, "Invalid output format parameters\n");
    exit(EXIT_FAILURE);
  }

  dump_format(context, 0, filename, 1);

  open_video(context, video_stream);
  //open_audio(context, audio_stream);
  
  int err;
  if((err = url_fopen(&context->pb, filename, URL_WRONLY)) < 0) {
    print_error(filename, err);
    fprintf(stderr, "Could not open '%s'\n", filename);
    exit(EXIT_FAILURE);
  }

  /* write the stream header, if any */
  av_write_header(context);
}

void Mp4Mux_WriteVideo(AVPacket *pkt, unsigned int timestamp)
{
  static unsigned int prev_timestamp = 0;
  AVCodecContext *c = video_stream->codec;
  
  if(timestamp && prev_timestamp) {
    //printf("den = %d\n", (90000 / (timestamp - prev_timestamp)) / 2);
    c->time_base.den = (90000 / (timestamp - prev_timestamp)) / 2;
  }
  
  prev_timestamp = timestamp;
  
//  if (c->pix_fmt != PIX_FMT_YUV420P)
//    printf("c->pix_fmt != PIX_FMT_YUV420P\n");
  
//  if(c->coded_frame->pts != AV_NOPTS_VALUE)
//    pkt->pts = av_rescale_q(c->coded_frame->pts, c->time_base, video_stream->time_base);

//  if(c->coded_frame->key_frame)
//    pkt->flags |= PKT_FLAG_KEY;
  pkt->stream_index= video_stream->index;

  /* write the compressed frame in the media file */
  int ret = av_interleaved_write_frame(context, pkt);
//  int ret = av_write_frame(context, pkt);

  if(ret != 0)
    fprintf(stderr, "Error while writing video frame\n");
}

void Mp4mux_Close()
{
  /* write the trailer, if any.  the trailer must be written
   * before you close the CodecContexts open when you wrote the
   * header; otherwise write_trailer may try to use memory that
   * was freed on av_codec_close() */
  av_write_trailer(context);

  /* close each codec */
  if (video_stream)
      close_video(context, video_stream);
//  if (audio_stream)
//      close_audio(context, audio_stream);

  /* free the streams */
  int i;
  for(i = 0; i < context->nb_streams; i++) {
      av_freep(&context->streams[i]->codec);
      av_freep(&context->streams[i]);
  }

  /* close the output file */
  url_fclose(context->pb);

  /* free the stream */
  av_free(context);
}



//AVCodecContext *codec_context = video_stream->codec;    