#ifndef ZEDCAMERAIN_HPP__
#define ZEDCAMERAIN_HPP__

//opencv include
#include <opencv2/core/core.hpp>
#include "mediain.hpp"

#ifdef ZED_SUPPORT
//zed include
#include <zed/Mat.hpp>
#include <zed/Camera.hpp>
#include <zed/utils/GlobalDefine.hpp>
#endif

class ZedIn : public MediaIn
{
	public:
		ZedIn(const char *filename = NULL);
		bool getNextFrame(cv::Mat &frame, bool pause = false);

#ifdef ZED_SUPPORT
		int    width(void) const;
		int    height(void) const;
		bool   getDepthMat(cv::Mat &depthMat);
		double getDepth(int x, int y);
#endif

	private:
#ifdef ZED_SUPPORT
		bool getNextFrame(cv::Mat &frame, bool left, bool pause);

		sl::zed::Camera* zed_;
		cv::Mat frameRGBA_;
		cv::Mat frame_;
		cv::Mat depthMat_;
		int width_;
		int height_;
		int frameCounter_;
#endif
};
#endif

