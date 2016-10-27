// Class to handle video input from ZED camera
// Similar to other camerain classes, except that depth
// data is returned from getFrame.
// Note that this class is derived from ZedIn rather than 
// MediaIn like many of the other *in classes. This lets the
// code use a common call to grab the camera parameters.
// TODO : test to see that this is correct for SVO files.
// If not, break the code up since calls from ZMS in use fakes
// of those values
#pragma once

#include <opencv2/core/core.hpp>
#include "asyncin.hpp"
#include "zedparams.hpp"

#ifdef ZED_SUPPORT
#include <zed/Camera.hpp>
#endif

class ZvSettings;

class ZedCameraIn : public AsyncIn
{
	public:
		ZedCameraIn(bool gui = false, ZvSettings *settings = NULL);
		~ZedCameraIn();

#ifdef ZED_SUPPORT
		bool         isOpened(void) const;
		CameraParams getCameraParams(void) const;
#endif
	protected:
		// Defined in derived classes to handle the nuts
		// and bolts of grabbing a frame from a given
		// source.  preLock happens before the mutex
		// while postLock happens inside it
		bool preLockUpdate(void);
		bool postLockUpdate(cv::Mat &frame, cv::Mat &depth);
#ifdef ZED_SUPPORT
	private:
		sl::zed::Camera *zed_;
		cv::Mat          localFrame_;
		cv::Mat          localDepth_;
                         
		int              brightness_;
		int              contrast_;
		int              hue_;
		int              saturation_;
		int              gain_;
		int              exposure_;
		ZedParams        params_;

		// Helpers to save and load settings in XML file
		bool loadSettings(void);
		bool saveSettings(void) const;
		std::string getClassName() const { return "ZedCameraIn"; }

		// Mark these as friends so they can access private class data
		friend void zedBrightnessCallback(int value, void *data);
		friend void zedContrastCallback(int value, void *data);
		friend void zedHueCallback(int value, void *data);
		friend void zedSaturationCallback(int value, void *data);
		friend void zedGainCallback(int value, void *data);
		friend void zedExposureCallback(int value, void *data);
#endif
};
