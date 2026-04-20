#include <NvInfer.h>
#include <cuda_runtime_api.h>
#include <opencv2/opencv.hpp>
#include <vector>
#include <iostream>
#include <fstream>

using namespace nvinfer1;

// ��������
const int INPUT_W = 640;
const int INPUT_H = 640;
const int NUM_CLASSES = 80;

// 日志类，用于TensorRT内部信息输出（只打印非INFO级别日志）
class Logger : public nvinfer1::ILogger {
public:
    // 重写log方法，severity为日志等级，msg为日志内容
    void log(Severity severity, const char* msg) noexcept override;
};

Logger gLogger;                        // 日志对象引用


// Logger实现：只打印非INFO级别日志
void Logger::log(Severity severity, const char* msg) noexcept {
    if (severity != Severity::kINFO)
        std::cout << msg << std::endl;
}

// BGR to RGB + resize + normalization
void preProcess(const cv::Mat& img, float* input) {
    cv::Mat resized, rgb;
    cv::resize(img, resized, cv::Size(INPUT_W, INPUT_H)); //将输入图像img缩放到指定大小（INPUT_W x INPUT_H），并将结果存储在resized中
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB); //BGR->RGB
    rgb.convertTo(rgb, CV_32FC3, 1.0 / 255);  //CV_8UC3（8位无符号整数，范围[0,255]）> CV_32FC3（32位浮点数，范围[0,1]）
    std::vector<cv::Mat> channels(3);
    cv::split(rgb, channels);
    for (int c = 0; c < 3; ++c) {
        memcpy(input + c * INPUT_H * INPUT_W, channels[c].data, INPUT_H * INPUT_W * sizeof(float));
    }  //  输出一维浮点数组0--h*w-1:R通道，h*w--2h*w-1:G通道，2h*w--3h*w-1:B通道，
}

//   output  [x, y, w, h, conf, class_id] 保留置信度大于0.5的检测框
void postProcess(float* output, int output_size) {
    for (int i = 0; i < output_size; i += 6) {
        float conf = output[i + 4];
        if (conf > 0.5) {
            int class_id = static_cast<int>(output[i + 5]);
            float x = output[i + 0];
            float y = output[i + 1];
            float w = output[i + 2];
            float h = output[i + 3];
            std::cout << "Class " << class_id << ": " << conf << " [" << x << "," << y << "," << w << "," << h << "]" << std::endl;
        }
    }
}

int main() {
    // 1. ���� engine 加载模型
    std::ifstream file("yolov10m.engine", std::ios::binary);
    file.seekg(0, std::ifstream::end);
    size_t size = file.tellg();
    file.seekg(0);
    std::vector<char> engine_data(size);
    file.read(engine_data.data(), size);

    IRuntime* runtime = createInferRuntime(gLogger);
    ICudaEngine* engine = runtime->deserializeCudaEngine(engine_data.data(), size);
    IExecutionContext* context = engine->createExecutionContext();

    // 2. ׼������ͼ�� 预处理
    cv::Mat img = cv::imread("5180.png");
    float* host_input = new float[3 * INPUT_H * INPUT_W];
    preProcess(img, host_input);  ////  输出一维浮点数组0--h*w-1:R通道，h*w--2h*w-1:G通道，2h*w--3h*w-1:B通道

    float* host_output = new float[1000];  // ��������С��ģ�Ͷ���

    // 3. CUDA�ڴ�������ʼ��
    void* device_input;
    void* device_output;
    cudaMalloc(&device_input, 3 * INPUT_H * INPUT_W * sizeof(float));
    cudaMalloc(&device_output, 1000 * sizeof(float));

    cudaStream_t stream;
    cudaStreamCreate(&stream);

    // 4. ��������
    cudaMemcpyAsync(device_input, host_input, 3 * INPUT_H * INPUT_W * sizeof(float), cudaMemcpyHostToDevice, stream);
    void* bindings[] = {device_input, device_output};
    context->enqueueV2(bindings, stream, nullptr);
    cudaMemcpyAsync(host_output, device_output, 1000 * sizeof(float), cudaMemcpyDeviceToHost, stream);
    cudaStreamSynchronize(stream);

    // 5. ����
    postProcess(host_output, 1000);

    // 6. ��Դ�ͷ�释放所有分配的资源
    cudaStreamDestroy(stream);
    cudaFree(device_input);
    cudaFree(device_output);
    delete[] host_input;
    delete[] host_output;
    context->destroy();
    engine->destroy();
    runtime->destroy();

    return 0;
}
