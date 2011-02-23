#ifndef RTPDATAHEADER_H
#define RTPDATAHEADER_H

// For 32bit intel machines
typedef short int16;
typedef int   int32;
typedef unsigned int u_int32;
typedef unsigned short u_int16;

#define RTP_BIG_ENDIAN 0
#define RTP_LITTLE_ENDIAN 1

//  RTP data header
//typedef __attribute__ ((__packed__)) struct {
typedef struct {
#if RTP_BIG_ENDIAN
	unsigned int version:2;         // protocol version
	unsigned int p:1;               // padding flag
	unsigned int x:1;               // header extension flag
	unsigned int cc:4;              // CSRC count
	unsigned int m:1;               // marker bit
	unsigned int pt:7;              // payload type
#elif RTP_LITTLE_ENDIAN
	unsigned int cc:4;              // CSRC count
	unsigned int x:1;               // header extension flag
	unsigned int p:1;               // padding flag
	unsigned int version:2;         // protocol version
	unsigned int pt:7;              // payload type
	unsigned int m:1;               // marker bit
#else
#error Define one of RTP_LITTLE_ENDIAN or RTP_BIG_ENDIAN
#endif
	unsigned int seq:16;            // sequence number
	u_int32 ts;                     // timestamp 32bits
	u_int32 ssrc;                   // synchronization source
} rtp_hdr_t;

#endif
