#ifndef IMAGEIN_HPP__
#define IMAGEIN_HPP__

#include <opencv2/core/core.hpp>

#include "mediain.hpp"

// Still image (png, jpg) processing
class ImageIn : public MediaIn
{
   public:
      ImageIn(const char *path);
	  ~ImageIn() {}
      bool getNextFrame(cv::Mat &frame, bool pause = false);

	  int frameCount(void) const;
	  int frameNumber(void) const;

      int width() const;
      int height() const;

   private:
      cv::Mat frame_;
};
#endif

