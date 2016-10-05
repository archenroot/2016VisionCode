#include <iostream>
#include "zedsvoin.hpp"
using namespace std;

#ifdef ZED_SUPPORT
using namespace cv;
using namespace sl::zed;

ZedSVOIn::ZedSVOIn(const char *inFileName, ZvSettings *settings) :
	ZedIn(settings),
	frameReady_(false) // trigger an immediate read of 1st frame in update()
{
	zed_ = new Camera(inFileName);

	if (zed_)
	{
		InitParams parameters;
		parameters.mode = PERFORMANCE;
		parameters.unit = MILLIMETER;
		parameters.verbose = 1;
		// init computation mode of the zed
		ERRCODE err = zed_->init(parameters);

		// Quit if an error occurred
		if (err != SUCCESS)
		{
			cout << errcode2str(err) << endl;
			delete zed_;
			zed_ = NULL;
		}
		else
		{
			//only for Jetson K1/X1 - see if it helps
			Camera::sticktoCPUCore(2);

			width_  = zed_->getImageSize().width;
			height_ = zed_->getImageSize().height;

			initCameraParams(true);

			thread_ = boost::thread(&ZedSVOIn::update, this);
		}
	}

	while (height_ > 700)
	{
		width_  /= 2;
		height_ /= 2;
	}
}

ZedSVOIn::~ZedSVOIn()
{
	thread_.interrupt();
	thread_.join();
}

bool ZedSVOIn::isOpened(void) const
{
	return zed_ ? true : false;
}


void ZedSVOIn::update(void)
{
	const bool left = true;
	if (!zed_)
		return;

	sl::zed::Mat slFrame;
	sl::zed::Mat slDepth;

	do
	{
		// If the frame read from the last update()
		// call hasn't been used yet, loop here
		// until it has been. This will prevent
		// the code from reading multiple frames
		// in the time it takes to process one and
		// skipping some video in the process
		boost::mutex::scoped_lock guard(mtx_);
		while (frameReady_)
			condVar_.wait(guard);

		// This can be done outside of the mutex
		// since it doesn't update any shared buffers
		bool res = zed_->grab(SENSING_MODE::STANDARD, true, true, true);

		if (!res)
		{
			slFrame = zed_->retrieveImage(left ? SIDE::LEFT : SIDE::RIGHT);
			slDepth = zed_->retrieveMeasure(MEASURE::DEPTH);
		}

		if (res)
			frame_ = cv::Mat();
		else
		{
			setTimeStamp();
			incFrameNumber();
			cvtColor(slMat2cvMat(slFrame), frame_, CV_RGBA2RGB);
			slMat2cvMat(slDepth).copyTo(depth_);
		}

		while (frame_.rows > 700)
		{
			pyrDown(frame_, frame_);
			pyrDown(depth_, depth_);
		}

		// Let getFrame know that a frame is ready
		// to be read / processed
		frameReady_ = true;
		condVar_.notify_all();
	}
	while (!frame_.empty());
}


bool ZedSVOIn::getFrame(cv::Mat &frame, cv::Mat &depth, bool pause)
{
	if (!zed_)
		return false;

	if (!pause)
	{
		// Wait until a valid frame is in frame_
		boost::mutex::scoped_lock guard(mtx_);
		while (!frameReady_)
			condVar_.wait(guard);

		if (frame_.empty())
			return false;

		frame_.copyTo(prevGetFrame_);
		depth_.copyTo(prevGetDepth_);
		lockTimeStamp();
		lockFrameNumber();

		// Let update() know that getFrame has copied
		// the current frame out of frame_
		frameReady_ = false;
		condVar_.notify_all();

		// Release the mutex so that update() can
		// start getting the next frame while the 
		// current one is returned and processed
		// in the main thread.
	}

	if (prevGetFrame_.empty())
		return false;

	prevGetFrame_.copyTo(frame);
	prevGetDepth_.copyTo(depth);
	
	return true;
}


int ZedSVOIn::frameCount(void) const
{
	// Luckily getSVONumberOfFrames() returns -1 if we're
	// capturing from a camera, which is also what the rest
	// of our code expects in that case
	if (zed_)
		return zed_->getSVONumberOfFrames();

	// If using zms or a live camera, there's no way to tell
	return -1;
}


// Seek to a given frame number. 
// Since the update code is running in a 
// different thread, need to mutex lock this since
// it could change the state of the zed object
// After setting the frame, set frameReady to false
// to force a new frame needs to be read
void ZedSVOIn::frameNumber(int frameNumber)
{
	if (!zed_)
		return;

	boost::mutex::scoped_lock guard(mtx_);
	if (zed_->setSVOPosition(frameNumber))
		setFrameNumber(frameNumber - 1);
	frameReady_ = false;
	condVar_.notify_all();
}


#else
ZedSVOIn::ZedSVOIn(const char *inFileName, ZvSettings *settings) :
	ZedIn(settings)
{
	(void)inFileName;
}


ZedSVOIn::~ZedSVOIn()
{
}
#endif
