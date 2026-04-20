/*
* Copyright 2017-2018 NVIDIA Corporation.  All rights reserved.
*
* Please refer to the NVIDIA end user license agreement (EULA) associated
* with this source code for terms and conditions that govern your use of
* this software. Any use, reproduction, disclosure, or distribution of
* this software and related documentation outside the terms of the EULA
* is strictly prohibited.
*
*/

#include <stdint.h>
#include <cuda_runtime.h>
#include "NvCodecUtils.h"

#define SLEEP_TIME 0

inline __device__ double sleep(int n) {
    double d = 1.0;
    for (int i = 0; i < n; i++) {
        d += sin(d);
    }
    return d;
}

inline __device__ uint8_t clamp(int i) {
    return (uint8_t)min(max(i, 0), 255);
}


static __global__ void rgbaNorm(float *dpNorm, int pitch, int iWidth, int iHeight, uint8_t *dpsrc, int nPitch, int nWidth, int nHeight, int offx, int offy) {
    int ix = blockIdx.x * blockDim.x + threadIdx.x,
        iy = blockIdx.y * blockDim.y + threadIdx.y;
	int ox = offx/2 + ix;
	int oy = offy/2 + iy;

	if (ix >= nWidth / 2 || iy >= nHeight / 2 || ox >= iWidth/2-1 || oy >= iHeight/2-1) {
        return;
    }

    float val  =  (float)(dpsrc[2*iy*nPitch + 2*ix])/255.0;
    float val2 =  (float)(dpsrc[2*iy*nPitch + 2*ix+1])/255.0;
    float val3 =  (float)(dpsrc[(2*iy+1)*nPitch + 2*ix])/255.0;
    float val4 =  (float)(dpsrc[(2*iy+1)*nPitch + 2*ix+1])/255.0;
    
    dpNorm[pitch * oy*2 + ox*2 + 2*iWidth*iHeight] = val;
    dpNorm[pitch * oy*2 + ox*2 +   iWidth*iHeight] = val;
    dpNorm[pitch * oy*2 + ox*2] 				   = val;

    dpNorm[pitch * oy*2 + ox*2 + 1 + 2*iWidth*iHeight] = val2;
    dpNorm[pitch * oy*2 + ox*2 + 1 +   iWidth*iHeight] = val2;
    dpNorm[pitch * oy*2 + ox*2 + 1] 				   = val2;

    dpNorm[pitch * (oy*2 + 1) + ox*2 + 2*iWidth*iHeight] = val3;
    dpNorm[pitch * (oy*2 + 1) + ox*2 +   iWidth*iHeight] = val3;
    dpNorm[pitch * (oy*2 + 1) + ox*2] 				     = val3;

    dpNorm[pitch * (oy*2 + 1) + ox*2 + 1 + 2*iWidth*iHeight] = val4;
    dpNorm[pitch * (oy*2 + 1) + ox*2 + 1 +   iWidth*iHeight] = val4;
    dpNorm[pitch * (oy*2 + 1) + ox*2 + 1] 				     = val4;
    
    sleep(SLEEP_TIME);
}

void rgbaImageNorm(cudaStream_t stream, float *dpNorm, int pitch, int iWidth, int iHeight, uint8_t *dpsrc, int nPitch, int nWidth, int nHeight, int offx, int offy ) {
    rgbaNorm<<<dim3((nWidth + 15) / 16, (nHeight + 15) / 16), dim3(16, 16), 0, stream>>>(dpNorm, pitch, iWidth, iHeight, dpsrc, nPitch, nWidth, nHeight, offx, offy);
}


static __global__ void DrawRectI420(uint8_t *pImage, int pitch, uint8_t *dpU, uint8_t *dpV, int pitch_uv, int nWidth, int nHeight, int x, int y, int iWidth, int iHeight) {
    int ix = blockIdx.x * blockDim.x + threadIdx.x,
        iy = blockIdx.y * blockDim.y + threadIdx.y;
    if (ix >= nWidth/2 || iy >= nHeight/2) {
        return;
    }

    if(((ix == x/2 || ix == x/2 || ix == x/2 + iWidth/2  || ix == x/2 + iWidth/2 ) && (iy >= y/2 && iy < y/2+iHeight/2)) ||
       ((iy == y/2 || iy == y/2 || iy == y/2 + iHeight/2 || iy == y/2 + iHeight/2) && (ix >= x/2 && ix < x/2+iWidth/2)))
    {
        pImage[2*iy * pitch + 2*ix] = 65;
        pImage[2*iy * pitch + 2*ix+1] = 65;
        pImage[(2*iy +1) * pitch + 2*ix] = 65;
        pImage[(2*iy +1) * pitch + 2*ix+1] = 65;
        dpU[iy * pitch_uv + ix] = 212;
        dpV[iy * pitch_uv + ix] = 100;
    }

    sleep(SLEEP_TIME);
}

void ImageDrawRectI420(cudaStream_t stream, uint8_t *dpImage, int pitch, uint8_t *dpU, uint8_t *dpV, int pitch_uv, int nWidth, int nHeight, int x, int y, int iWidth, int iHeight) {
	DrawRectI420<<<dim3((nWidth + 15) / 16, (nHeight + 15) / 16), dim3(16, 16), 0, stream>>>(dpImage, pitch, dpV, dpU, pitch_uv, nWidth, nHeight, x, y, iWidth, iHeight);
}

static __global__ void LaplacianFilter(uint8_t *pDst, uint8_t *pImage, int pitch, int nWidth, int nHeight, float alpha) {
    int ix = blockIdx.x * blockDim.x + threadIdx.x,
        iy = blockIdx.y * blockDim.y + threadIdx.y;

	if (ix < 1 || ix >= nWidth-1 || iy < 1 || iy >= nHeight-1) {
        return;
    }

	float val = pImage[iy*pitch + ix]*4.0 - pImage[iy*pitch + ix+1] -  pImage[iy*pitch + ix-1] -  pImage[(iy-1)*pitch + ix] - pImage[(iy+1)*pitch + ix];
	int pixel = pImage[iy*pitch + ix] + val*alpha;
	pDst[iy*pitch + ix] = clamp(pixel);

    sleep(SLEEP_TIME);
}

void ImageLaplacian(cudaStream_t stream, uint8_t *pDst, uint8_t *dpImage, int pitch, int nWidth, int nHeight, float alpha) {
	LaplacianFilter<<<dim3((nWidth + 15) / 16, (nHeight + 15) / 16), dim3(16, 16), 0, stream>>>(pDst, dpImage, pitch, nWidth, nHeight, alpha);
}

static __global__ void GrayImageNorming(float *pDst, uint8_t *pImage, int pitch, int nWidth, int nHeight) {
    int ix = blockIdx.x * blockDim.x + threadIdx.x,
        iy = blockIdx.y * blockDim.y + threadIdx.y;

	if (ix >= nWidth || iy >= nHeight) {
        return;
    }

	pDst[iy*nWidth + ix] = (float)pImage[iy*pitch + ix]/255.0;

    sleep(SLEEP_TIME);
}

void GrayImageNorm(cudaStream_t stream, float *pDst, uint8_t *dpImage, int pitch, int nWidth, int nHeight) {
	GrayImageNorming<<<dim3((nWidth + 15) / 16, (nHeight + 15) / 16), dim3(16, 16), 0, stream>>>(pDst, dpImage, pitch, nWidth, nHeight);
}


static __global__ void GrayImageDenorming(uint8_t *pDst, float *pImage, int pitch, int nWidth, int nHeight) {
    int ix = blockIdx.x * blockDim.x + threadIdx.x,
        iy = blockIdx.y * blockDim.y + threadIdx.y;

	if (ix >= nWidth || iy >= nHeight) {
        return;
    }

	pDst[iy*pitch + ix] = clamp(pImage[iy*pitch + ix]*255);

    sleep(SLEEP_TIME);
}

void GrayImageDenorm(cudaStream_t stream, uint8_t *pDst, float *dpImage, int pitch, int nWidth, int nHeight) {
	GrayImageDenorming<<<dim3((nWidth + 15) / 16, (nHeight + 15) / 16), dim3(16, 16), 0, stream>>>(pDst, dpImage, pitch, nWidth, nHeight);
}


static __global__ void GrayImageAGKernal(float *pDst, uint8_t *pImage, int pitch, int nWidth, int nHeight) {
    int ix = blockIdx.x * blockDim.x + threadIdx.x,
        iy = blockIdx.y * blockDim.y + threadIdx.y;

	if (ix >= nWidth ||  iy >= nHeight) {
        return;
    }
    
    int l = ix-1;
    int r = ix+1;
    int t = iy-1;
    int b = iy+1;
    float scale = 2;
    
    if(ix == 0) 
    {
	    l = ix;
	    scale = 1;
    }

    if(ix == nWidth-1)
    {
     	r = ix;
     	scale = 1;
    }
    
    if(iy == 0)
    {
	     t = iy;
	     scale = 1;
    }
    
    if(iy == nHeight-1)
    {
    	b = iy;
    	scale = 1;
    }
    
    float valx = (float)(pImage[iy*pitch + r] - pImage[iy*pitch + l])/scale;
    float valy = (float)(pImage[b*pitch + ix] - pImage[t*pitch + ix])/scale;

	pDst[iy*pitch + ix] = sqrt((valx*valx+valy*valy)/2);

    sleep(SLEEP_TIME);
}

void GrayImageAG(cudaStream_t stream, float *pDst, uint8_t *dpImage, int pitch, int nWidth, int nHeight) {
	GrayImageAGKernal<<<dim3((nWidth + 15) / 16, (nHeight + 15) / 16), dim3(16, 16), 0, stream>>>(pDst, dpImage, pitch, nWidth, nHeight);
}

static __global__ void GrayImageSFKernal(float *pDst, uint8_t *pImage, int pitch, int nWidth, int nHeight) {
    int ix = blockIdx.x * blockDim.x + threadIdx.x,
        iy = blockIdx.y * blockDim.y + threadIdx.y;

	if (ix >= nWidth ||  iy >= nHeight) {
        return;
    }
        
    int l = ix-1;
    int t = iy-1;
    
    if(ix == 0) 
    {
	    l = ix;
    }

    if(iy == 0)
    {
	     t = iy;
    }
    
    float valx = (float)(pImage[iy*pitch + ix] - pImage[iy*pitch + l]);
    float valy = (float)(pImage[iy*pitch + ix] - pImage[t*pitch + ix]);

	pDst[iy*pitch + ix] = (valx*valx+valy*valy);

    sleep(SLEEP_TIME);
}

void GrayImageSF(cudaStream_t stream, float *pDst, uint8_t *dpImage, int pitch, int nWidth, int nHeight) {
	GrayImageSFKernal<<<dim3((nWidth + 15) / 16, (nHeight + 15) / 16), dim3(16, 16), 0, stream>>>(pDst, dpImage, pitch, nWidth, nHeight);
}

__device__ void warpReduce(volatile float* sdata, int tid) {
    sdata[tid] += sdata[tid + 32];   // 1、线程束内的32个线程先做这一步
    sdata[tid] += sdata[tid + 16];   // 2、线程束内的32个线程第二步
    sdata[tid] += sdata[tid + 8];    // 3、线程束内的32个线程第三步
    sdata[tid] += sdata[tid + 4];    // 4、线程束内的32个线程第四步
    sdata[tid] += sdata[tid + 2];    // 5、线程束内的32个线程第五步
    sdata[tid] += sdata[tid + 1];    // 6、线程束内的32个线程第六步
}
__global__ void reduce0(float *g_odata, float *g_idata) {
    extern __shared__ float sdata[];       //  分配共享内存空间，不同的block都有共享内存变量的副本
   // each thread loads one element from global to shared mem
   // perform first level of reduction,
   // reading from global memory, writing to shared memory
   unsigned int tid = threadIdx.x;     //  某个block内的线程标号 index
   // 每个一个线程块block，拿出一个线程标号index
   // 每隔 1 个 block 拿到线程的标号 index，也就是0，2，2x2，......
   unsigned int i = blockIdx.x*(blockDim.x*2) + threadIdx.x;    //  i 是某个线程的标号 index
   // 每隔线程块大小来 累加，这样每个线程块就累加了前后两个线程块大小的数据
   sdata[tid] = g_idata[i] + g_idata[i+blockDim.x];
   __syncthreads();
   // do reduction in shared mem
   for (unsigned int s=blockDim.x/2; s>32; s>>=1) {   // 折半规约直到只剩下32x2个线程，也就是一个线程数warpsize
      if (tid < s)
         sdata[tid] += sdata[tid + s];
      __syncthreads();
   }
   if (tid < 32) warpReduce(sdata, tid);
    // write result for this block to global mem
    if (tid == 0) g_odata[blockIdx.x] = sdata[0];  // 某个block只做一次操作   复制共享内存变量累加的结果到全局内存
}

void configure_reduce_sum(cudaStream_t stream, float *pDst, float *dpImage, int n) {
	reduce0<<<dim3(160, 1), dim3(1024, 1), 1024*sizeof(float), stream>>>( pDst, dpImage);
}

__global__ void histo_kernel(unsigned char *buffer, long size, float *histo){
    __shared__ unsigned int temp[256];
    temp[threadIdx.x] = 0;
    __syncthreads();
    int i = threadIdx.x + blockIdx.x * blockDim.x;
    int offset = blockDim.x *gridDim.x;
    while (i<size){
        atomicAdd(&temp[buffer[i]], 1);
        i += offset;
    }
    __syncthreads();
    atomicAdd(&(histo[threadIdx.x]), temp[threadIdx.x]);
}

void cal_hist(cudaStream_t stream, unsigned char *buffer, long size, float *pDst) {
	histo_kernel<<<dim3(1, 1), dim3(256, 1), 0, stream>>>( buffer, size, pDst);
}


__global__ void max_min_kernel(unsigned char *buffer, long size, unsigned char *pDstmax, unsigned char *pDstmin) 
{ 
    //申请共享内存，存在于每个block中
    __shared__ int partialMax[1024];  
    __shared__ int partialMin[1024];

    //确定索引 
    int i = threadIdx.x + blockIdx.x * blockDim.x; 
    int tid = threadIdx.x;  
    
	if (i >= size) {
        return;
    }

    //传global memory数据到shared memory 
    partialMax[tid]=buffer[i]; 
    partialMin[tid]=buffer[i];

    //传输同步 
    __syncthreads(); 
    
    //在共享存储器中进行规约 
    for(int stride = blockDim.x / 2; stride > 0; stride >>= 1) 
    { 
        __syncthreads();
        if(tid < stride)
        {
            if(partialMax[tid] < partialMax[tid+stride])
            {
                int temp = partialMax[tid];
                partialMax[tid] = partialMax[tid+stride];
                partialMax[tid+stride] = temp;
            }
            if(partialMin[tid] > partialMin[tid+stride])
            {
                int temp = partialMin[tid];
                partialMin[tid] = partialMin[tid+stride];
                partialMin[tid+stride] = temp;
            }
        }
    } 

    //将当前block的计算结果写回输出数组 
    if(tid==0)   
    {
        pDstmax[blockIdx.x] = partialMax[0];
        pDstmin[blockIdx.x] = partialMin[0];        
    } 
} 

void findMaxmin(cudaStream_t stream, unsigned char *buffer, long size, unsigned char *pDstmax, unsigned char *pDstmin) {
	max_min_kernel<<<dim3((size+1023)/1024, 1), dim3(1024, 1), 0, stream>>>( buffer, size, pDstmax, pDstmin);
}


static __global__ void GrayImageconvert_kernel(uint8_t *pDst, uint8_t *pImage, int pitch, int nWidth, int nHeight, int omin, int nmin, float ratio) {
    int ix = blockIdx.x * blockDim.x + threadIdx.x,
        iy = blockIdx.y * blockDim.y + threadIdx.y;

	if (ix >= nWidth || iy >= nHeight) {
        return;
    }

	int  val = nmin + ((int)pImage[iy*pitch + ix] - omin)*ratio;
	pDst[iy*pitch + ix] = clamp(val);

    sleep(SLEEP_TIME);
}

void GrayImageconvert(cudaStream_t stream, uint8_t *pDst, uint8_t *dpImage, int pitch, int nWidth, int nHeight, int omin, int nmin, float ratio) {
	GrayImageconvert_kernel<<<dim3((nWidth + 15) / 16, (nHeight + 15) / 16), dim3(16, 16), 0, stream>>>(pDst, dpImage, pitch, nWidth, nHeight, omin, nmin, ratio);
}
