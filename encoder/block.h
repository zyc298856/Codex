#ifndef _BLOCK_H
#define _BLOCK_H
#include <stdint.h>
#include <string.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/shm.h>
#include <fcntl.h>

#define         MIN(A, B)       ((A) < (B) ? (A) : (B))
#define         MAX(A, B)       ((A) > (B) ? (A) : (B))

/** The content doesn't follow the last block, or is probably broken */
#define BLOCK_FLAG_DISCONTINUITY 0x0001
/** Intra frame */
#define BLOCK_FLAG_TYPE_I        0x0002
/** Inter frame with backward reference only */
#define BLOCK_FLAG_TYPE_P        0x0004
/** Inter frame with backward and forward reference */
#define BLOCK_FLAG_TYPE_B        0x0008
/** For inter frame when you don't know the real type */
#define BLOCK_FLAG_TYPE_PB       0x0010
/** Warm that this block is a header one */
#define BLOCK_FLAG_HEADER        0x0020
/** This is the last block of the frame */
#define BLOCK_FLAG_END_OF_FRAME  0x0040
/** This is not a key frame for bitrate shaping */
#define BLOCK_FLAG_NO_KEYFRAME   0x0080
/** This block contains a clock reference */
#define BLOCK_FLAG_CLOCK         0x0200
/** This block is scrambled */
#define BLOCK_FLAG_SCRAMBLED     0x0400
/** This block has to be decoded but not be displayed */
#define BLOCK_FLAG_PREROLL       0x0800
/** This block is corrupted and/or there is data loss  */
#define BLOCK_FLAG_CORRUPTED     0x1000

#define BLOCK_FLAG_PRIVATE_MASK  0xffff0000
#define BLOCK_FLAG_PRIVATE_SHIFT 16

typedef struct block_s block_t;

struct block_s
{
	block_t     *p_next;

	int64_t     i_pts;
	int64_t     i_dts;
	int64_t     i_length;

	uint32_t    i_flags;
	uint32_t    i_reserved;

	int         i_samples; /* Used for audio */
	int         i_rate;
	int         i_extra;
	uint8_t     p_extra[128];
	
	int         i_alloc;
	int         i_buffer;
	//uint8_t     *p_alloc;
	uint8_t     *p_buffer;
};

block_t *block_New(int i_size );
//block_t *block_Realloc(block_t *p_block, int i_prebody, int i_body );
void block_Release(block_t * p_block);
void block_ChainAppend( block_t **pp_list, block_t *p_block );
void block_ChainRelease( block_t *p_block );


typedef struct
{
	int      i_block_size;
	int      i_read_block;
	int      i_write_block;
	int      i_stream_pos;
	block_t  *block_buffer;
	uint8_t  *p_bitstream;
	int64_t  i_stream_length;
	int      i_bitstream;
} recycle_block_chain_t;

static inline void ChainBufferInit  ( recycle_block_chain_t *c, int i_depth, int i_stream_len)
{
	c->i_read_block  = 0;
	c->i_write_block = 0;
	c->i_stream_pos  = 0;
	c->i_stream_length=0;
	c->i_block_size  = i_depth;
	c->i_bitstream   = i_stream_len;
	c->block_buffer  = (block_t *) malloc(sizeof( block_t )*i_depth);
	c->p_bitstream   = (uint8_t *) malloc(i_stream_len);
}

static inline block_t * ChainBufferPut( recycle_block_chain_t *c, int i_buffer)
{
	block_t *b = NULL;
	if(((c->i_write_block+1)%c->i_block_size) != c->i_read_block)
	{
		b = &c->block_buffer[c->i_write_block];
		if(c->i_stream_pos + i_buffer > c->i_bitstream)
		c->i_stream_pos = 0;
		b->p_buffer = c->p_bitstream + c->i_stream_pos;
		b->i_buffer = i_buffer;
		b->i_alloc  = i_buffer;
		b->i_pts    = 0;
		b->i_dts    = 0;
		b->i_length = 0;
		b->i_flags  = 0;
		b->i_rate   = 0;
		b->i_samples= 0;
		b->p_next = NULL;
		//b->p_prev = NULL;
		//b->p_alloc = NULL;
	}
	return b;
}

static inline void ChainBufferPutDown( recycle_block_chain_t *c)
{
	block_t *b = &c->block_buffer[c->i_write_block];
	c->i_stream_pos += MAX(b->i_buffer, b->i_alloc);
	c->i_stream_length += b->i_length;
	c->i_write_block = (c->i_write_block+1)%c->i_block_size;
}

static inline int ChainBufferGetNum( recycle_block_chain_t *c )
{
	return (c->i_write_block - c->i_read_block + c->i_block_size)%c->i_block_size;
}

static inline block_t *ChainBufferGet( recycle_block_chain_t *c )
{
	block_t *b = NULL;
	if(c->i_write_block != c->i_read_block)
	{
		b= &c->block_buffer[c->i_read_block];
	}
	return b;
}

static inline void ChainBufferGetOff( recycle_block_chain_t *c)
{
	block_t *b = &c->block_buffer[c->i_read_block];
	c->i_stream_length -= b->i_length;
	c->i_read_block = (c->i_read_block+1)%c->i_block_size;
}

static inline void ChainBufferRetset( recycle_block_chain_t *c)
{
	//if(c->b_init)
	{
		c->i_read_block  = 0;
		c->i_write_block = 0;
		c->i_stream_pos  = 0;
		c->i_stream_length=0;
	}
}

static inline void ChainBufferClean(recycle_block_chain_t *c )
{
	free(c->block_buffer);
	free(c->p_bitstream);
}


typedef struct
{
	int 	 b_init;
	int		 i_frame_size;
	int      i_block_size;
	int64_t  i_read_block;
	int64_t  i_write_block;
	int64_t  ppts[16];
	int64_t  flags[16];
	uint8_t  *block_buffer[16];
	pthread_mutex_t mutex;
} recycle_frame_t;

void FrameBufferInit  ( recycle_frame_t *c, int i_depth, int i_frame_size);
int  FrameBufferGetNum( recycle_frame_t *c );
int  FrameBufferGetFlags( recycle_frame_t *c);
uint8_t *FrameBufferGet( recycle_frame_t *c, int64_t *pts );
void FrameBufferGetOff( recycle_frame_t *c);
uint8_t * FrameBufferPut( recycle_frame_t *c);
void FrameBufferPutdown( recycle_frame_t *c, int64_t pts, int flags);
void FrameBufferClean(recycle_frame_t *c );

typedef struct
{
	int 	 b_init;
	int      i_block_size;
	int64_t  i_read_block;
	int64_t  i_write_block;
	int      buffer_fd[16];
	pthread_mutex_t mutex;
} recycle_buffer_fd_t;

void FrameBufferFDInit  ( recycle_buffer_fd_t *c, int i_depth);
int  FrameBufferFDGetNum( recycle_buffer_fd_t *c );
int  FrameBufferFDGet( recycle_buffer_fd_t *c );
void FrameBufferFDGetOff( recycle_buffer_fd_t *c);
int  FrameBufferFDPut( recycle_buffer_fd_t *c);
void FrameBufferFDPutdown( recycle_buffer_fd_t *c);
void FrameBufferFDClean(recycle_buffer_fd_t *c );


typedef struct
{
	int 	 b_init;
	int 	 b_force;
	int64_t  i_frame_dts;
	int64_t  i_frame_pts;
	int      i_block_size;
	int      i_read_block;
	int      i_write_block;
	block_t  *block_buffer;
	int      i_stream_pos;
	int      i_bitstream;
	uint8_t  *p_bitstream;
	pthread_mutex_t mutex;
} recycle_block_buffer_t;

void BlockBufferInit  ( recycle_block_buffer_t *c, int i_depth, int i_stream_len, int b_force);
int  BlockBufferWrite( recycle_block_buffer_t *c, block_t *in);
int BlockBufferOverwrite( recycle_block_buffer_t *c, block_t *in);
int  BlockBufferGetNum( recycle_block_buffer_t *c );
void BlockBufferReset( recycle_block_buffer_t *c );
block_t *BlockBufferGet( recycle_block_buffer_t *c );
void BlockBufferGetOff( recycle_block_buffer_t *c);
void BlockBufferClean(recycle_block_buffer_t *c );

int RecycleBufferGetBlockNum( recycle_block_buffer_t *c, int i_read_block );
block_t *RecycleBufferGetBlock( recycle_block_buffer_t *c, int i_read_block );
void RecycleBufferGetBlockOff( recycle_block_buffer_t *c, int *i_read_block);
int RecycleBufferBlockReset( recycle_block_buffer_t *c );
int BlockBufferGetFrames( recycle_block_buffer_t *c, int v, int l);

#endif

