#pragma once
class CCyUSBDevice;
class CCyBulkEndPoint;
#include <chrono>
#include <stdlib.h>

#include <iostream>
#include <mutex>
#include <thread>
#include <string.h>

// #include <Windows.h>
#include <vector>

#include <libusb-1.0/libusb.h>

namespace usbcam
{
	class usbdev
	{
	public:
        usbdev(libusb_device *dev, int idx);
		~usbdev();
		void init(int _rows, int _cols, int _bitsPerPix); // 初始化 ， 参数依次为 行数，列数，每像素位数
		void start();									  // 开启线程
		void stop();									  // 停止线程
		bool getimage(unsigned char *addr, int timeout);  // 获取图像，参数依次为 图像地址，超时时间 
		//  bool getimage16(unsigned short* addr, int timeout);//获取图像，参数依次为 图像地址，超时时间，单位为毫秒 获取每像素占16位的图像
	private:
		// CCyUSBDevice *device;
		libusb_device *device;
		libusb_device_handle *hdl;

		void processdata();
		void run();
		// void initEndPoint();
		int usbcontrol(unsigned char *msg, int rlen); //
		// void SetImageInfo(int _type, int _rows, int _cols);

		// CCyBulkEndPoint* epin;
		// CCyBulkEndPoint* epout;

		// OVERLAPPED cmdinOvLap;
		// unsigned char* cmdcontexts;

		unsigned char *data;
		unsigned char *pureData;
		unsigned char *dst;
		unsigned char *recvImage;
        // unsigned char *buffer;
		unsigned char *cmdbuffer;

		int rows;
		int cols;
		int bitsPerPix;
		int datalen;
		bool b = false;
		bool fillImage = false;
		std::mutex dataMutex;
		std::mutex usbMutex;

        int idx;

		const int shead = 512;
		std::thread tusb;	  // usb接受线程
		std::thread tprocess; // 数据处理线程
		static void LIBUSB_CALL streamcb(libusb_transfer *);
        static bool asyReady[128*16];
	};

	std::vector<usbcam::usbdev *> scanUSBDevices();

	// int quit();

}

// 使用示例：
//  usbcam::init(480,640,2); //初始化
//  usbcam::start(); //开启线程
//  unsigned char img[480*640*2]; //定义图像数组
// while(1){ //循环获取图像
//  usbcam::getimage(img,1000); //获取图像，超时时间为1000毫秒
//  处理图像
// }
//  usbcam::stop(); //停止线程
