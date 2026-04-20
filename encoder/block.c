
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include "block.h"
#include <sys/time.h>
#include <errno.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include <cuda_profiler_api.h>

block_t *block_New(int i_size )
{
	/* We do only one malloc
	* TODO bench if doing 2 malloc but keeping a pool of buffer is better
	* 64 -> align on 64
	* 2 * BLOCK_PADDING_SIZE -> pre + post padding
	*/
	uint8_t*p_buffer;
	const int i_alloc = i_size + 2 * 64 + 32;
	block_t *p_block = (block_t *) malloc(sizeof( block_t ) + i_alloc);
	
	if( p_block == NULL ) return NULL;
	p_buffer = (uint8_t*)p_block + sizeof( block_t ) + 64;
	p_block->p_buffer       = p_buffer + 32 - ((uint64_t)p_buffer%32);
	
	/* Fill all fields */
	p_block->p_next         = NULL;
	//p_block->p_prev         = NULL;
	p_block->i_flags        = 0;
	p_block->i_pts          = 0;
	p_block->i_samples 		= 0;
	p_block->i_dts          = 0;
	p_block->i_length       = 0;
	p_block->i_rate         = 0;
	p_block->i_buffer       = i_size;
	p_block->i_alloc        = 64 + i_size + ((uint64_t)p_buffer%32);

	return p_block;
}

void block_Release(block_t * p_block)
{
	free(p_block);
}

void block_ChainAppend( block_t **pp_list, block_t *p_block )
{
    if( *pp_list == NULL )
    {
        *pp_list = p_block;
    }
    else
    {
        block_t *p = *pp_list;

        while( p->p_next ) p = p->p_next;
        p->p_next = p_block;
    }
}

void block_ChainRelease( block_t *p_block )
{
    while( p_block )
    {
        block_t *p_next = p_block->p_next;
        block_Release( p_block );
        p_block = p_next;
    }
}

void FrameBufferInit( recycle_frame_t *c, int i_depth, int i_frame_size)
{
	c->b_init = 1;
	c->i_write_block  = 0;
	c->i_read_block   = 0;
	c->i_frame_size   = i_frame_size;
	c->i_block_size   = i_depth;
	for(int i = 0; i < c->i_block_size; i++)
	{
	    cudaMalloc(&c->block_buffer[i], i_frame_size);
	    cudaMemset(c->block_buffer[i],  128, i_frame_size);
	}

	pthread_mutex_init(&c->mutex, NULL);
}

void FrameBufferClean( recycle_frame_t *c)
{
	if(c->b_init)
	{
		c->b_init = 0;
		pthread_mutex_lock(&c->mutex );
		for(int i = 0; i < c->i_block_size; i++)
		cudaFree(c->block_buffer[i]);
		pthread_mutex_unlock(&c->mutex );
		pthread_mutex_destroy(&c->mutex );
	}
}

uint8_t *FrameBufferGet( recycle_frame_t *c, int64_t *pts )
{
	uint8_t *b = NULL;
	pthread_mutex_lock(&c->mutex );
	if(c->i_write_block > c->i_read_block)
	{
		int index = c->i_read_block%c->i_block_size;
		b = c->block_buffer[index];
		*pts = c->ppts[index];
	}
	pthread_mutex_unlock(&c->mutex );
	return b;
}

int FrameBufferGetFlags( recycle_frame_t *c)
{
	int flags;
	pthread_mutex_lock(&c->mutex );
	flags = c->flags[c->i_read_block%c->i_block_size];
	pthread_mutex_unlock(&c->mutex );
	return flags;
}

void FrameBufferGetOff( recycle_frame_t *c)
{
	pthread_mutex_lock(&c->mutex );
	c->i_read_block++;
	pthread_mutex_unlock(&c->mutex );
}

uint8_t * FrameBufferPut( recycle_frame_t *c)
{
	uint8_t *b = NULL;
	if(c->b_init)
	{
		pthread_mutex_lock(&c->mutex );

		if((c->i_write_block - c->i_read_block) < c->i_block_size)
		{
			b = c->block_buffer[c->i_write_block%c->i_block_size];
		}

		pthread_mutex_unlock(&c->mutex );
	}

	return b;
}

void FrameBufferPutdown( recycle_frame_t *c, int64_t pts, int flags)
{
	pthread_mutex_lock(&c->mutex );
	int index = c->i_write_block%c->i_block_size;
	c->ppts[index] = pts;
	c->flags[index] = flags;
	c->i_write_block++;
	pthread_mutex_unlock(&c->mutex );
}

int FrameBufferGetNum( recycle_frame_t *c )
{
	int num = 0;
	if(c->b_init)
	{
		pthread_mutex_lock(&c->mutex );
		num = c->i_write_block - c->i_read_block;
		pthread_mutex_unlock(&c->mutex );
	}
	return num;
}

void BlockBufferInit  ( recycle_block_buffer_t *c, int i_depth, int i_stream_len, int b_force)
{
	c->b_init = 1;
	c->b_force = b_force;
	c->i_read_block  = 0;
	c->i_write_block = 0;
	c->i_stream_pos  = 0;
	c->i_block_size  = i_depth;
	c->i_bitstream   = i_stream_len;
	c->block_buffer  = (block_t *) malloc(sizeof( block_t )*i_depth);
	c->p_bitstream   = (uint8_t *) malloc(i_stream_len);
	pthread_mutex_init(&c->mutex, NULL);
}

int BlockBufferWrite( recycle_block_buffer_t *c, block_t *in)
{
	int ret = 0;
	if(c->b_init)
	{
		int nCanWrite = c->i_bitstream;
		pthread_mutex_lock(&c->mutex );

		if(!c->b_force && c->i_write_block != c->i_read_block)
		{
			block_t *rb = &c->block_buffer[c->i_read_block];
			int diff = rb->i_rate - c->i_stream_pos;
			if ( diff > 0)
				nCanWrite =  diff;
			else
			{
				if(c->i_stream_pos + in->i_buffer + in->i_extra <= c->i_bitstream)
					nCanWrite = c->i_bitstream + diff;
				else
					nCanWrite = rb->i_rate;
			}

			if(in->i_buffer + in->i_extra > nCanWrite)
			{
				printf("size %d > can:%d\n", in->i_buffer + in->i_extra,  nCanWrite);
			}
		}

		if(in->i_buffer + in->i_extra > 0 && (c->b_force || (((c->i_write_block+1)%c->i_block_size) != c->i_read_block && in->i_buffer + in->i_extra < nCanWrite)))
		{
			block_t*b = &c->block_buffer[c->i_write_block];
			if(c->i_stream_pos + in->i_buffer + in->i_extra > c->i_bitstream)
			{
				c->i_stream_pos = 0;
			}

			b->p_buffer = c->p_bitstream + c->i_stream_pos;
			b->i_buffer = in->i_buffer + in->i_extra;
			b->i_alloc  = c->i_stream_pos;
			b->i_pts    = in->i_pts;
			b->i_dts    = in->i_dts;
			b->i_length = in->i_length;
			b->i_flags  = in->i_flags;
			b->i_rate   = c->i_stream_pos;
			b->i_samples= in->i_samples;
			b->i_extra = 0;
			b->p_next = NULL;

			memcpy(b->p_buffer, in->p_buffer, in->i_buffer);
			memcpy(b->p_buffer + in->i_buffer, in->p_extra, in->i_extra);

			c->i_stream_pos += in->i_buffer+in->i_extra;
			c->i_write_block = (c->i_write_block+1)%c->i_block_size;
			c->i_frame_pts = in->i_pts;

			ret = 1;//((c->i_write_block - c->i_read_block + c->i_block_size)%c->i_block_size);
		}
		pthread_mutex_unlock(&c->mutex );
	}
	else
		ret = 1;
	return ret;
}

int BlockBufferOverwrite( recycle_block_buffer_t *c, block_t *in)
{
	int ret = 0;
	if(c->b_init)
	{
		pthread_mutex_lock(&c->mutex );
		block_t*b = &c->block_buffer[c->i_write_block];
		if(c->i_stream_pos + in->i_buffer + in->i_extra > c->i_bitstream)
		{
			c->i_stream_pos = 0;
		}

		b->p_buffer = c->p_bitstream + c->i_stream_pos;
		b->i_buffer = in->i_buffer + in->i_extra;
		b->i_alloc  = c->i_stream_pos;
		b->i_pts    = in->i_pts;
		b->i_dts    = in->i_dts;
		b->i_length = in->i_length;
		b->i_flags  = in->i_flags;
		b->i_rate   = c->i_stream_pos;
		b->i_samples= in->i_samples;
		b->i_extra = 0;
		b->p_next = NULL;

		memcpy(b->p_buffer, in->p_buffer, in->i_buffer);
		memcpy(b->p_buffer + in->i_buffer, in->p_extra, in->i_extra);

		c->i_stream_pos += in->i_buffer+in->i_extra;
		c->i_write_block = (c->i_write_block+1)%c->i_block_size;
		c->i_frame_pts = in->i_pts;

		ret = 1;

		pthread_mutex_unlock(&c->mutex );
	}
	else
		ret = 1;
	return ret;
}

void BlockBufferReset( recycle_block_buffer_t *c )
{
	if(c->b_init)
	{
		pthread_mutex_lock(&c->mutex );
		if(c->i_write_block != c->i_read_block)
		{
			block_t *b = &c->block_buffer[c->i_read_block];
			c->i_frame_dts = b->i_dts;
		}
		c->i_read_block = c->i_write_block;
		pthread_mutex_unlock(&c->mutex );
	}
}

int BlockBufferGetNum( recycle_block_buffer_t *c )
{
	int num = 0;
	if(c->b_init)
	{
		pthread_mutex_lock(&c->mutex );
		num = (c->i_write_block - c->i_read_block + c->i_block_size)%c->i_block_size;
		pthread_mutex_unlock(&c->mutex );
	}
	return num;
}

block_t *BlockBufferGet( recycle_block_buffer_t *c )
{
	block_t *b = NULL;
	pthread_mutex_lock(&c->mutex );
	if(c->i_write_block != c->i_read_block)
	{
		b= &c->block_buffer[c->i_read_block];
		c->i_frame_dts = b->i_dts;
	}
	pthread_mutex_unlock(&c->mutex );
	return b;
}

void BlockBufferGetOff( recycle_block_buffer_t *c)
{
	pthread_mutex_lock(&c->mutex );
	c->i_read_block = (c->i_read_block+1)%c->i_block_size;
	pthread_mutex_unlock(&c->mutex );
}

int RecycleBufferGetBlockNum( recycle_block_buffer_t *c, int i_read_block )
{
	int num = 0;
	if(c->b_init)
	{
		pthread_mutex_lock(&c->mutex );
		num = (c->i_write_block - i_read_block + c->i_block_size)%c->i_block_size;
		pthread_mutex_unlock(&c->mutex );
	}
	return num;
}

block_t *RecycleBufferGetBlock( recycle_block_buffer_t *c, int i_read_block )
{
	block_t *b = NULL;
	pthread_mutex_lock(&c->mutex );
	if(c->i_write_block != i_read_block)
	{
		b= &c->block_buffer[i_read_block];
		c->i_frame_dts = b->i_dts;
	}
	pthread_mutex_unlock(&c->mutex );
	return b;
}

void RecycleBufferGetBlockOff( recycle_block_buffer_t *c, int *i_read_block)
{
	*i_read_block = (*i_read_block+1)%c->i_block_size;
}

int RecycleBufferBlockReset( recycle_block_buffer_t *c )
{
	int i_read_block = 0;
	if(c->b_init)
	{
		pthread_mutex_lock(&c->mutex );
		i_read_block = c->i_write_block;
		pthread_mutex_unlock(&c->mutex );
	}

	return i_read_block;
}

void BlockBufferClean(recycle_block_buffer_t *c )
{
	if(c->b_init)
	{
		pthread_mutex_lock(&c->mutex );
		free(c->block_buffer);
		free(c->p_bitstream);
		pthread_mutex_unlock(&c->mutex );
		pthread_mutex_destroy(&c->mutex );
	}

	c->b_init = 0;
}

