#include <NvInfer.h>
#include <cuda_runtime_api.h>
#include <opencv2/opencv.hpp>
#include <vector>
#include <iostream>
#include <fstream>

#include "NvCodecUtils.h"
#include "nvbuf_utils.h"
#include "NvUtils.h"
#include "../cuda_utils.h"
#include "cudaEGL.h"
#include "NvCudaProc.h"
#include "NvBufSurface.h"

extern "C"
{
#include "../main.h"
}

using namespace nvinfer1;

#ifdef YOLOV10

const int INPUT_W = 640;
const int INPUT_H = 640;
const int NUM_CLASSES = 80;
const int OUTPUT_SIZE = 1000 * 6 + 1;

// 日志类，用于TensorRT内部信息输出（只打印非INFO级别日志）
class YLogger : public nvinfer1::ILogger {
public:
    // 重写log方法，severity为日志等级，msg为日志内容
    void log(Severity severity, const char* msg) noexcept override;
};

typedef struct yolov_
{
	IRuntime* runtime;
	ICudaEngine* engine;
	IExecutionContext* context;
	YLogger *pLogger;
	cudaStream_t stream;
	void* buffers[2];
    float prob[OUTPUT_SIZE];
    int i_width;
    int i_height;
} yolov10_t;

void rgbaImageNorm(cudaStream_t stream, float *dpNorm, int pitch, int iWidth, int iHeight, uint8_t *dpsrc, int nPitch, int nWidth, int nHeight, int offx, int offy);
void ImageDrawRectI420(cudaStream_t stream, uint8_t *dpImage, int pitch, uint8_t *dpU, uint8_t *dpV, int pitch_uv, int nWidth, int nHeight, int x, int y, int iWidth, int iHeight);

// Logger实现：只打印非INFO级别日志
void YLogger::log(Severity severity, const char* msg) noexcept {
    if (severity != Severity::kINFO)
        std::cout << msg << std::endl;
}

void *open_yolo(int i_width, int i_height)
{
    yolov10_t *p_yolo = NULL;
    // create a model using the API directly and serialize it to a stream
    char *trtModelStream{nullptr};
    size_t size{0};

    std::ifstream file("yolov10.engine", std::ios::binary);
    if (file.good()) {
        file.seekg(0, file.end);
        size = file.tellg();
        file.seekg(0, file.beg);
        trtModelStream = new char[size];
        assert(trtModelStream);
        file.read(trtModelStream, size);
        file.close();
    }

    if(trtModelStream)
    {
    	YLogger * pLogger= new YLogger;
		IRuntime* runtime = createInferRuntime(*pLogger);
		assert(runtime != nullptr);
		ICudaEngine* engine = runtime->deserializeCudaEngine(trtModelStream, size);
		assert(engine != nullptr);
		IExecutionContext* context = engine->createExecutionContext();
		assert(context != nullptr);
		delete[] trtModelStream;

		p_yolo = (yolov10_t *)malloc(sizeof(yolov10_t));
		memset(p_yolo, 0, sizeof(yolov10_t));

	    // Create GPU buffers on device
	    CUDA_CHECK(cudaMalloc(&p_yolo->buffers[0], 3 * INPUT_H * INPUT_W * sizeof(float)));
	    CUDA_CHECK(cudaMalloc(&p_yolo->buffers[1], OUTPUT_SIZE * sizeof(float)));

	    // Create stream
	    cudaStream_t stream;
	    CUDA_CHECK(cudaStreamCreate(&stream));

		p_yolo->i_width = i_width;
		p_yolo->i_height = i_height;
		p_yolo->runtime = runtime;
		p_yolo->engine  = engine;
		p_yolo->context = context;
		p_yolo->pLogger = pLogger;
		p_yolo->stream = stream;
    }

    return p_yolo;
}

void close_yolo(void *param)
{
	yolov10_t *p_yolo = (yolov10_t*)param;
    // Destroy the engine
	p_yolo->context->destroy();
	p_yolo->engine->destroy();
	p_yolo->runtime->destroy();
	delete p_yolo->pLogger;

    // Release stream and buffers
	CUDA_CHECK(cudaFree(p_yolo->buffers[0]));
	CUDA_CHECK(cudaFree(p_yolo->buffers[1]));
	cudaStreamDestroy(p_yolo->stream);
	free(p_yolo);
}


void doInference(yolov10_t *p_yolo, float* output)
{
    // DMA input batch data to device, infer on the batch asynchronously, and DMA output back to host
    //CHECK(cudaMemcpyAsync(p_yolo->buffers[0], input, batchSize * 3 * INPUT_H * INPUT_W * sizeof(float), cudaMemcpyHostToDevice, p_yolo->stream));
    p_yolo->context->enqueueV2(p_yolo->buffers, p_yolo->stream, nullptr);
    CUDA_CHECK(cudaMemcpyAsync(output, p_yolo->buffers[1], OUTPUT_SIZE * sizeof(float), cudaMemcpyDeviceToHost, p_yolo->stream));
    cudaStreamSynchronize(p_yolo->stream);
}

int yolo_infer(AVElement_t*p_video, void *param, unsigned char*src, int i_src, int b_object_show)
{
	yolov10_t *p_yolo = (yolov10_t*)param;

    int w = p_yolo->i_width;
    int h = p_yolo->i_height;
    int x = 0;
    int y = (INPUT_H - h) / 2;

	rgbaImageNorm(p_yolo->stream, (float*)p_yolo->buffers[0], INPUT_W, INPUT_W, INPUT_H,
			(unsigned char*)src, i_src, w, h, x, y);

	// Run inference
	doInference(p_yolo, p_yolo->prob);

	output_roi(p_video, p_yolo->prob);

	int count = 0;
    for (int i = 0; i < 1000; i += 6) {
        float conf = p_yolo->prob[i + 4];
        if (conf > p_video->f_prob) {
            //int class_id = static_cast<int>(p_yolo->prob[i + 5]);
            count++;
            if(b_object_show)
			ImageDrawRectI420(p_yolo->stream, (uint8_t*)src, i_src,
					(uint8_t*)src+i_src*p_yolo->i_height, (uint8_t*)src+i_src*p_yolo->i_height*5/4, i_src/2,
					p_yolo->i_width, p_yolo->i_height, p_yolo->prob[i + 0], p_yolo->prob[i + 1]-y,
					p_yolo->prob[i + 2]-p_yolo->prob[i], p_yolo->prob[i + 3]-p_yolo->prob[i+1]);
        }
    }

    return count;
}

#endif
