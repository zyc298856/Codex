//#include "mimo_unet.h"
//#include <filesystem>
#include <opencv2/opencv.hpp>
#include <iostream>

// 使用C++17标准的文件系统库，简化文件和路径操作
//namespace fs = std::filesystem;

int main(int argc, char *argv[])
{
	if(argc < 4)
	{
		printf(" min max filename\n for example \n 20 130 0.5cut.bmp\n");
		return 0;
	}

	int i_mins = atoi(argv[1]);
	int i_maxs = atoi(argv[2]);
	cv::Mat img = cv::imread(argv[3], cv::IMREAD_GRAYSCALE);

	cv::namedWindow("ori", cv::WINDOW_NORMAL);
	cv::imshow("ori", img);

	int i_max = 0;
	int i_min = 255;
	for (int i = 0; i < img.rows ; ++i)
	{
		uchar* pdata = img.ptr<uchar>(i);
		for (int j = 0; j < img.cols; ++j)
		{
			if( i_max < pdata[j])
				i_max = pdata[j];
			if( i_min > pdata[j])
				i_min = pdata[j];
		}
	}

	for (int i = 0; i < img.rows ; ++i)
	{
		uchar* pdata = img.ptr<uchar>(i);
		for (int j = 0; j < img.cols; ++j)
		{
			int val = i_mins + ((int)pdata[j] - i_min) * (i_maxs - i_mins) / (i_max - i_min);
			pdata[j] = cv::max(0, cv::min(val, 255));
		}
	}

	char psz[256], filename[256];
	strcpy(psz, argv[3]);
	char *p = strrchr(psz, '.');
	*p = 0;
	sprintf(filename, "%s-%d-%d.bmp", psz, i_mins, i_maxs);

	cv::imwrite(filename, img);                                 // 保存去模糊后的结果图像

	cv::namedWindow("MRTD", cv::WINDOW_NORMAL);
	cv::imshow("MRTD", img);

	cv::waitKey(0);

	return 0 ;
}
