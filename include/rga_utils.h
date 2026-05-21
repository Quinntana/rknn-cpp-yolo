#ifndef RGA_UTILS_H
#define RGA_UTILS_H

#include <opencv2/opencv.hpp>
#include <cstdint>
#include <iostream>
#include <rga/im2d.hpp>
#include <rga/rga.h>

#include <cstring>
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

// �Ż���� memcpy ������ʹ�� NEON ָ�
void memcpy_neon1(void* dst, const void* src, size_t size);

// �ü�ͼ��Ϊ�����β�ȷ���ߴ�Ϊ16�ı���
void crop_image_to_square_and_16_alignment(cv::Mat& image);

// �ü�ͼ��ȷ���ߴ�Ϊ16�ı���
void crop_image_to_16_alignment(cv::Mat& image);

// ʹ�� RGA ���вü����ü�Ϊ�����β�ȷ���ߴ�Ϊ16�ı���
void crop_to_square_align16(const cv::Mat& src, cv::Mat& dst);

// ����Ӧ�ü���亯����ֱ�ӽ����д��Ŀ�������ַ
int adaptive_letterbox(const cv::Mat& src, int target_size, uint8_t* dst_virtual_addr, letterbox_t* letterbox, uint8_t fill_color, int interpolation = INTER_LINEAR);

// ʹ�� OpenCV ʵ�ֵ�����Ӧ�ü���亯����ֱ�ӽ����д��Ŀ�������ַ
int CV_adaptive_letterbox(const cv::Mat& src, int target_size, uint8_t* dst_virtual_addr, uint8_t fill_color, int interpolation = cv::INTER_LINEAR);


#ifdef __cplusplus
}  // extern "C"
#endif

#endif