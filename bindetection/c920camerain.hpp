#ifndef C920CAMERAIN_HPP__
#define C920CAMERAIN_HPP__

#include <opencv2/core/core.hpp>
#include "camerain.hpp"

#ifdef __linux__
#include "../C920VideoCap/C920Camera.h"
#endif

// Code specific for C920 camera. We have lots of
// extra controls avaiable for this, so use it if
// possible
class C920CameraIn : public CameraIn
{
   public:
      C920CameraIn(int _stream = -1, bool gui = false);
      bool getNextFrame(cv::Mat &frame, bool pause = false);

      int width(void) const;
      int height(void) const;

   private:
#ifdef __linux__
      bool initCamera(int _stream, bool gui);

	  // Mark these as friends so they can access private class data
	  friend void brightnessCallback(int value, void *data);
	  friend void contrastCallback(int value, void *data);
	  friend void saturationCallback(int value, void *data);
	  friend void sharpnessCallback(int value, void *data);
	  friend void gainCallback(int value, void *data);
	  friend void autoExposureCallback(int value, void *data);
	  friend void backlightCompensationCallback(int value, void *data);
	  friend void whiteBalanceTemperatureCallback(int value, void *data);
	  friend void focusCallback(int value, void *data);

      v4l2::C920Camera  _camera;
      int               _brightness;
      int               _contrast;
      int               _saturation;
      int               _sharpness;
      int               _gain;
      int               _focus;
      int               _autoExposure;
      int               _backlightCompensation;
      int               _whiteBalanceTemperature;
	  int               _frameCounter;
	  v4l2::CaptureSize _captureSize;
#endif
};
#endif
