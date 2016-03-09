#ifndef VIDEOIN_HPP__
#define VIDEOIN_HPP__

#include <opencv2/highgui/highgui.hpp>
#include "mediain.hpp"

class VideoIn : public MediaIn
{
   public:
      VideoIn(const char *path);
	  ~VideoIn() {}
      bool update();
      bool getFrame(cv::Mat &frame);
      int width() const;
      int height() const;
      int frameCount(void) const;
      int frameNumber(void) const;
      void frameNumber(int frameNumber);

   private:
    bool increment;
    cv::VideoCapture cap_;
	  int              width_;
	  int              height_;
	  int              frames_;
	  int              frameNumber_;
};
#endif
