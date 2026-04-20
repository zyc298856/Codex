#include "usbcam.h"
#include <vector>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <libusb-1.0/libusb.h>
#include <fstream>

#define cols 640
#define rows 512
#define bytes 1
#define type CV_8UC1

#include <sys/time.h>
#include <errno.h>

#define HAVE_CLOCK_NANOSLEEP

/*****************************************************************************
 * mdate返回以微秒为单位的当前时间
 *****************************************************************************/
int64_t mdate( void )
{
#if defined (HAVE_CLOCK_NANOSLEEP)
    struct timespec ts;

    /* Try to use POSIX monotonic clock if available */
    if( clock_gettime( CLOCK_MONOTONIC, &ts ) == EINVAL )
        /* Run-time fallback to real-time clock (always available) */
        (void)clock_gettime( CLOCK_REALTIME, &ts );

    return ((int64_t)ts.tv_sec * (int64_t)1000000) // 将秒数转换为微秒
            + (int64_t)(ts.tv_nsec / 1000);        // 将纳秒转化为微秒
#else
    struct timeval tv_date;

    /* gettimeofday() could return an error, and should be tested. However, the
     * only possible error, according to 'man', is EFAULT, which can not happen
     * here, since tv is a local variable. */
    gettimeofday( &tv_date, NULL );    // 获取当前时间，NULL：不需要时区信息
    return( (int64_t) tv_date.tv_sec * 1000000 + (int64_t) tv_date.tv_usec );  // 将秒数转换为微秒+微秒偏移量
#endif
}



/*扫描并初始化USB摄像头设备，从摄像头捕获图像数据并实时显示，计算并输出帧率*/

int main(){
    std::vector<usbcam::usbdev*> devlist = usbcam::scanUSBDevices(); // 获取所有可用的 USB 摄像头设备列表
    std::cout<<"devices nums :"<<devlist.size()<<std::endl; // 在控制台打印出可用设备数量：
    devlist[0]->init(rows,cols,bytes); // 初始化第一个设备，参数依次为 分辨率（行数X列数），每像素字节数
    devlist[0]->start();               // 启动设备，开始捕获图像

    int frame = 0;                     // 计数器：记录当前秒内帧数
    int totals = 0;                    // 记录总帧数
    //FILE *f = fopen("data.yuv", "wb");
    int64_t last = mdate( );           // 记录上一次计算帧率的时间戳
    cv::Mat image(rows,cols,type);     // 储存捕获的图像数据
    //unsigned char *addr = (unsigned char *)malloc(rows*cols);
    while(true){
        if(devlist[0]->getimage(image.data,3000)) // 成功捕获一帧图像
        //if(devlist[0]->getimage(addr,3000))
        {
        	frame++;                              // 更新计数器
            cv::imshow("default",image);          // 显示图像

            //fwrite(addr,1, rows*cols*bytes, f);
        }

        if(mdate( ) - last > 1000000)            // 每隔一秒
        {
        	printf("fps:%ld\n", (int64_t)frame*1000000/(mdate( ) - last)); // 计算当前帧率，并输出
        	frame = 0;
        	last = mdate( );
        }

        if(cv::waitKey(1) ==  'q'){   // 按下q键退出循环
            break;
        }

        if(totals++ > 10000)        // 总帧数totals达到一定值时，退出循环
        {
            //break;
        }
    }

    //fclose(f);

    devlist[0]->stop();        // 停止设备
    printf("over:%d\n",frame); // 输出最终帧数
    return 0;
}
