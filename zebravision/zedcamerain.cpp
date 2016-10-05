#include <iostream>
#include "zedcamerain.hpp"
using namespace std;

#ifdef ZED_SUPPORT
#include <opencv2/imgproc/imgproc.hpp>

#include "cvMatSerialize.hpp"
#include "ZvSettings.hpp"

using namespace cv;
using namespace sl::zed;

void zedBrightnessCallback(int value, void *data);
void zedContrastCallback(int value, void *data);
void zedHueCallback(int value, void *data);
void zedSaturationCallback(int value, void *data);
void zedGainCallback(int value, void *data);

ZedCameraIn::ZedCameraIn(bool gui, ZvSettings *settings) :
	ZedIn(settings),
	updateStarted_(false),
	brightness_(2),
	contrast_(6),
	hue_(7),
	saturation_(4),
	gain_(1)
{
	if (Camera::isZEDconnected()) // Open an actual camera for input
		zed_ = new Camera(HD720, 30);

	if (zed_)
	{
		InitParams parameters;
		parameters.mode = PERFORMANCE;
		parameters.unit = MILLIMETER;
		parameters.verbose = 1;
		// init computation mode of the zed
		ERRCODE err = zed_->init(parameters);

		//only for Jetson K1/X1 - see if it helps
		Camera::sticktoCPUCore(2);

		// Quit if an error occurred
		if (err != SUCCESS)
		{
			cout << errcode2str(err) << endl;
			delete zed_;
			zed_ = NULL;
		}
		else
		{
			width_  = zed_->getImageSize().width;
			height_ = zed_->getImageSize().height;

			if (!loadSettings())
				cerr << "Failed to load ZedCameraIn settings from XML" << endl;

			zedBrightnessCallback(brightness_, this);
			zedContrastCallback(contrast_, this);
			zedHueCallback(hue_, this);
			zedSaturationCallback(saturation_, this);
			zedGainCallback(gain_, this);

			cout << "brightness_ = " << zed_->getCameraSettingsValue(ZED_BRIGHTNESS) << endl;
			cout << "contrast_ = " << zed_->getCameraSettingsValue(ZED_CONTRAST) << endl;
			cout << "hue_ = " << zed_->getCameraSettingsValue(ZED_HUE) << endl;
			cout << "saturation_ = " << zed_->getCameraSettingsValue(ZED_SATURATION) << endl;
			cout << "gain_ = " << zed_->getCameraSettingsValue(ZED_GAIN) << endl;
			if (gui)
			{
				cv::namedWindow("Adjustments", CV_WINDOW_NORMAL);
				cv::createTrackbar("Brightness", "Adjustments", &brightness_, 9, zedBrightnessCallback, this);
				cv::createTrackbar("Contrast", "Adjustments", &contrast_, 9, zedContrastCallback, this);
				cv::createTrackbar("Hue", "Adjustments", &hue_, 12, zedHueCallback, this);
				cv::createTrackbar("Saturation", "Adjustments", &saturation_, 9, zedSaturationCallback, this);
				cv::createTrackbar("Gain", "Adjustments", &gain_, 9, zedGainCallback, this);
			}
			thread_ = boost::thread(&ZedCameraIn::update, this);
		}
	}

	while (height_ > 700)
	{
		width_  /= 2;
		height_ /= 2;
	}
	initCameraParams(true);
}

bool ZedCameraIn::loadSettings(void)
{
	if (settings_) {
		settings_->getInt(getClassName(), "brightness",   brightness_);
		settings_->getInt(getClassName(), "contrast",     contrast_);
		settings_->getInt(getClassName(), "hue",          hue_);
		settings_->getInt(getClassName(), "saturation",   saturation_);
		settings_->getInt(getClassName(), "gain",         gain_);
		return true;
	}
	return false;
}

bool ZedCameraIn::saveSettings(void) const
{
	if (settings_) {
		settings_->setInt(getClassName(), "brightness",   brightness_);
		settings_->setInt(getClassName(), "contrast",     contrast_);
		settings_->setInt(getClassName(), "hue",          hue_);
		settings_->setInt(getClassName(), "saturation",   saturation_);
		settings_->setInt(getClassName(), "gain",         gain_);
		settings_->save();
		return true;
	}
	return false;
}

ZedCameraIn::~ZedCameraIn()
{
	if (!saveSettings())
		cerr << "Failed to save ZedCameraIn settings to XML" << endl;
	thread_.interrupt();
	thread_.join();
}


bool ZedCameraIn::isOpened(void) const
{
	return zed_ ? true : false;
}


void ZedCameraIn::update(void)
{
	// TODO : make this settable somehow?
	const bool left = true;
	if (!zed_)
		return;

	while (1)
	{
		boost::this_thread::interruption_point();
		FPSmark();
		int badReadCounter = 0;
		while (zed_->grab(SENSING_MODE::STANDARD))
		{
			boost::this_thread::interruption_point();
			// Wait a bit to see if the next
			// frame shows up
			usleep(5000);
			// Try to grab a bunch of times before
			// bailing out and failing
			if (++badReadCounter == 100)
			{
				frame_ = cv::Mat();
				return;
			}
		}

		sl::zed::Mat slDepth = zed_->retrieveMeasure(MEASURE::DEPTH);
		sl::zed::Mat slFrame = zed_->retrieveImage(left ? SIDE::LEFT : SIDE::RIGHT);
		boost::mutex::scoped_lock guard(mtx_);
		setTimeStamp();
		incFrameNumber();
		cvtColor(slMat2cvMat(slFrame), frame_, CV_RGBA2RGB);
		slMat2cvMat(slDepth).copyTo(depth_);

		while (frame_.rows > 700)
		{
			pyrDown(frame_, frame_);
			pyrDown(depth_, depth_);
		}

		updateStarted_= true;
		condVar_.notify_all();
	}
}


bool ZedCameraIn::getFrame(cv::Mat &frame, cv::Mat &depth, bool pause)
{
	if (!zed_)
		return false;

	// If input is not paused, copy data from the
	// frame_/depth_ mats- these are the most recent
	// data read from the cameras
	if (!pause)
	{
		boost::mutex::scoped_lock guard(mtx_);
		while (!updateStarted_)
			condVar_.wait(guard);

		lockTimeStamp();
		lockFrameNumber();

		frame_.copyTo(pausedFrame_);
		depth_.copyTo(pausedDepth_);
	}
	pausedFrame_.copyTo(frame);
	pausedDepth_.copyTo(depth);

	return true;
}


void zedBrightnessCallback(int value, void *data)
{
    ZedCameraIn *zedPtr = static_cast<ZedCameraIn *>(data);
	zedPtr->brightness_ = value;
	if (zedPtr->zed_)
	{
		zedPtr->zed_->setCameraSettingsValue(ZED_BRIGHTNESS, value - 1, value == 0);
	}
}


void zedContrastCallback(int value, void *data)
{
    ZedCameraIn *zedPtr = static_cast<ZedCameraIn *>(data);
	zedPtr->contrast_ = value;
	if (zedPtr->zed_)
	{
		zedPtr->zed_->setCameraSettingsValue(ZED_CONTRAST, value - 1, value == 0);
	}
}


void zedHueCallback(int value, void *data)
{
    ZedCameraIn *zedPtr = static_cast<ZedCameraIn *>(data);
	zedPtr->hue_ = value;
	if (zedPtr->zed_)
	{
		zedPtr->zed_->setCameraSettingsValue(ZED_HUE, value - 1, value == 0);
	}
}


void zedSaturationCallback(int value, void *data)
{
    ZedCameraIn *zedPtr = static_cast<ZedCameraIn *>(data);
	zedPtr->saturation_ = value;
	if (zedPtr->zed_)
	{
		zedPtr->zed_->setCameraSettingsValue(ZED_SATURATION, value - 1, value == 0);
	}
}


void zedGainCallback(int value, void *data)
{
    ZedCameraIn *zedPtr = static_cast<ZedCameraIn *>(data);
	zedPtr->gain_ = value;
	if (zedPtr->zed_)
	{
		zedPtr->zed_->setCameraSettingsValue(ZED_GAIN, value - 1, value == 0);
	}
}


#else

ZedCameraIn::ZedCameraIn(bool gui, ZvSettings *settings) :
	ZedIn(settings)
{
	(void)gui;
}

ZedCameraIn::~ZedCameraIn()
{
}


#endif

