#pragma once

extern "C" {
#include "../main.h"
}

void* open_yolo_rknn(int i_width, int i_height);
int yolo_infer_rknn(AVElement_t* p_video, void* param, unsigned char* src, int stride_y,
                    int b_object_show);
void close_yolo_rknn(void* param);
