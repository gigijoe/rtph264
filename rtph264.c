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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/uio.h>

#include "rtpdataheader.h"
#include "rtph264.h"
#include "mp4mux.h"

extern AVCodec aac_encoder;
extern AVCodec aac_decoder;
extern AVCodec h264_decoder;
extern AVCodec mpeg4_encoder;
extern AVCodec mpeg4_decoder;

static AVCodecContext *context = NULL;
extern AVCodec aac_encoder;

void RtpH264_Init()
{
  /* must be called before using avcodec lib */
  avcodec_init();
  
  /* register all the codecs */
//  avcodec_register_all();

  avcodec_register(&aac_encoder);
  avcodec_register(&aac_decoder);
  avcodec_register(&h264_decoder);
  avcodec_register(&mpeg4_encoder);  
  avcodec_register(&mpeg4_decoder);

  AVCodec *codec = avcodec_find_decoder(CODEC_ID_H264);
  
  context = avcodec_alloc_context();
  
  /* open it */
  if(avcodec_open(context, codec) < 0) {
    fprintf(stderr, "could not open codec\n");
    exit(EXIT_FAILURE);
  }
  
  Mp4mux_Init();
  Mp4mux_Open("/tmp/scv.mp4");
}

void RtpH264_Deinit()
{
  Mp4mux_Close();

  avcodec_close(context);
  av_free(context);
}

static int bStop = 0;

void RtpH264_Stop()
{
  bStop = 1;
}

void RtpH264_Run(int sfd, RtpH264_OnPicture onPicture)
{
#define INBUF_SIZE (1024 * 128)
  uint8_t inbuf[INBUF_SIZE + FF_INPUT_BUFFER_PADDING_SIZE];
  /* set end of buffer to 0 (this ensures that no overreading happens for damaged mpeg streams) */
  memset(inbuf + INBUF_SIZE, 0, FF_INPUT_BUFFER_PADDING_SIZE);

  AVFrame *picture = avcodec_alloc_frame();
  AVPacket avpkt;
  av_init_packet(&avpkt);
  int frame_count = 0;

  unsigned short sequence = 0;
  unsigned int timestamp = 0;
  
  while(1)  {
    //int ready = 0;
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sfd, &rfds);

    struct timeval timeout;  /*Timer for operation select*/
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000; /*10 ms*/

    if(select(sfd+1, &rfds, 0, 0, &timeout) <= 0) {
      if(bStop)
        break;
      else
        continue;
    }

    if(FD_ISSET(sfd, &rfds) <= 0)
      continue;
    
    rtp_hdr_t rtp;
    unsigned char buf[64];
    memset(buf, 0, 64);
    int r = recv(sfd, buf, sizeof(rtp_hdr_t) + 8, MSG_PEEK);  /*  Peek data from socket, so that we could determin how to read data from socket*/
    if(r <= sizeof(rtp_hdr_t) || r == -1)  {
      recv(sfd, buf, sizeof(rtp_hdr_t) + 8, 0); /*Read invalid packet*/
      printf("Warning !!! Invalid packet\n");
      continue; /*Invalid packet ???*/
    }

    int ready = 0;

    /*  Handle H.264 RTP Header */
    /* +---------------+
    *  |0|1|2|3|4|5|6|7|
    *  +-+-+-+-+-+-+-+-+
    *  |F|NRI|  Type   |
    *  +---------------+
    *
    * F must be 0.
    */
    unsigned char nal_ref_idc, nal_unit_type;
    unsigned char *header = buf + sizeof(rtp_hdr_t);  /*  NAL Header  */
    nal_ref_idc = (header[0] & 0x60) >> 5;  /*  NRI */
//printf("nal_ref_idc = %d\n", nal_ref_idc);
    nal_unit_type = header[0] & 0x1f;       /*  Type  */
//printf("nal_unit_type = %d\n", nal_unit_type);

    switch (nal_unit_type) {
      case 0:
      case 30:
      case 31:
        /* undefined */
        break;
      case 25:
        /* STAP-B    Single-time aggregation packet     5.7.1 */
        /* 2 byte extra header for DON */
        /* fallthrough */
      case 24:
        /* STAP-A    Single-time aggregation packet     5.7.1 */
        break;
      case 26:
        /* MTAP16    Multi-time aggregation packet      5.7.2 */
        /* fallthrough, not implemented */
      case 27:
        /* MTAP24    Multi-time aggregation packet      5.7.2 */
        break;
      case 28:
        /* FU-A      Fragmentation unit                 5.8 */
      case 29:  {
        /* FU-B      Fragmentation unit                 5.8 */
        /* +---------------+
        * |0|1|2|3|4|5|6|7|
        * +-+-+-+-+-+-+-+-+
        * |S|E|R|  Type   |
        * +---------------+
        *
        * R is reserved and always 0
        */

        /*Really read packet*/
        struct iovec data[3];

        data[0].iov_base = &rtp;
        data[0].iov_len = sizeof(rtp_hdr_t);

        if(nal_unit_type == 28) {
          /* strip off FU indicator and FU header bytes */
          data[1].iov_base = buf;
          data[1].iov_len = 2;
        } else if(nal_unit_type == 29) {
          /* strip off FU indicator and FU header and DON bytes */
          data[1].iov_base = buf;
          data[1].iov_len = 4;
        }
        
        /* NAL unit starts here */
        if((header[1] & 0x80) == 0x80)  {
          data[2].iov_base = &inbuf[5];
          data[2].iov_len = INBUF_SIZE - 5;
          
          r = readv(sfd, data, 3);
          if(r <= (sizeof(rtp_hdr_t) + 2))  {
            printf("Socket read fail !!!\n");
            goto cleanup;
          }

          unsigned char fu_indicator = buf[0];
          unsigned char fu_header = buf[1];
          
          timestamp = ntohl(rtp.ts);
          sequence = ntohs(rtp.seq);            
          
          inbuf[0] = 0x00;
          inbuf[1] = 0x00;
          inbuf[2] = 0x00;
          inbuf[3] = 0x01;
          inbuf[4] = (fu_indicator & 0xe0) | (fu_header & 0x1f);
          if(nal_unit_type == 28)
            avpkt.size = (r - sizeof(rtp_hdr_t) - 2 + 5);
          else if(nal_unit_type == 29)
            avpkt.size = (r - sizeof(rtp_hdr_t) - 4 + 5);
          avpkt.data = inbuf;
//printf("nalu = %d\n", fu_header & 0x1f);
          
          if((fu_header & 0x1f) == 7)
            avpkt.flags |= PKT_FLAG_KEY;
//          avpkt.pts = frame_count++;
//printf("avpkt.pts = %d\n", avpkt.pts);          
          break;
        } else  {
          data[2].iov_base = inbuf + avpkt.size;
          data[2].iov_len = INBUF_SIZE - avpkt.size;

          r = readv(sfd, data, 3);

          //unsigned char fu_indicator = buf[0];
          unsigned char fu_header = buf[1];

          if(r <= (sizeof(rtp_hdr_t) + 2) || r == -1)  {
            printf("Socket read fail !!!\n");
            goto cleanup;
          }

          if(ntohl(rtp.ts) != timestamp)  {
            printf("Miss match timestamp %d ( expect %d )\n", ntohl(rtp.ts), timestamp);
          }

          if(ntohs(rtp.seq) != ++sequence)  {
            printf("Wrong sequence number %u ( expect %u )\n", ntohs(rtp.seq), sequence);
          }

          if(nal_unit_type == 28)
            avpkt.size += (r - sizeof(rtp_hdr_t) - 2);
          else if(nal_unit_type == 29)
            avpkt.size += (r - sizeof(rtp_hdr_t) - 4);
//printf("frame size : %d\n", avpkt.size);
        }
        
        /* NAL unit ends  */
        if((header[1] & 0x40) == 0x40)  {
          ready = 1;  /*We are done, go to decode*/
          break;
        }

        break;
      }
      default:  {
        /* 1-23   NAL unit  Single NAL unit packet per H.264   5.6 */
        /* the entire payload is the output buffer */
        struct iovec data[2];

        data[0].iov_base = &rtp;
        data[0].iov_len = sizeof(rtp_hdr_t);

        inbuf[0] = 0x00;
        inbuf[1] = 0x00;
        inbuf[2] = 0x00;
        inbuf[3] = 0x01;

        data[1].iov_base = &inbuf[4];
        data[1].iov_len = INBUF_SIZE - 4;

        r = readv(sfd, data, 2);
        if(r <= sizeof(rtp_hdr_t) || r == -1)  {
          printf("Socket read fail !!!\n");
          goto cleanup;
        }

        avpkt.size = (r - sizeof(rtp_hdr_t) + 4);
        avpkt.data = inbuf;
        
        ready = 1;  /*We are done, go to decode*/

        break;
      }
    }

    int got_picture;
    while(ready && avpkt.size > 0) {
      avpkt.pts = ++frame_count;
//printf("frame size : %d\n", avpkt.size);
//printf("avpkt.dts = %d\n", avpkt.dts);
//      int len = avcodec_decode_video(context, picture, &got_picture, avpkt.data, avpkt.size);
      int len = avcodec_decode_video2(context, picture, &got_picture, &avpkt);
//printf("context->coded_frame->pts = %d\n", context->coded_frame->pts);  /*  0 */
//printf("picture->pts = %d\n", picture->pts);
//printf("picture->pict_type = %d\n", picture->pict_type);
//printf("picture->key_frame = %d\n", picture->key_frame);
//printf("picture->interlaced_frame = %d\n", picture->interlaced_frame);
//printf("avpkt.duration = %d\n", avpkt.duration);
      
      Mp4Mux_WriteVideo(&avpkt, timestamp);

      if(len < 0) {
        fprintf(stderr, "Error while decoding frame\n");
        av_init_packet(&avpkt);
        break;
      }
      
      if(got_picture) {
//        printf("Decode length : %d\n", len);
//        fflush(stdout);
        /* the picture is allocated by the decoder. no need to
               free it */
        if(onPicture)
          onPicture(picture->data[0], picture->linesize[0], context->width, context->height);

        av_init_packet(&avpkt);
        break;
      }
      avpkt.size -= len;
      avpkt.data += len;
    }
  }

cleanup:
  av_free(picture);
  
  return;
}
