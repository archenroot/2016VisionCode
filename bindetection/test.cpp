#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/objdetect/objdetect.hpp"
#include <opencv2/opencv.hpp>

#include <iostream>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "networktables/NetworkTable.h"
#include "networktables2/type/NumberArray.h"

#include "imagedetect.hpp"
#include "videoin_c920.hpp"
#include "track.hpp"

using namespace std;
using namespace cv;

void writeImage(const Mat &frame, const vector<Rect> &rects, size_t index, const char *path, int frameCounter);

// given a directory number and stage within that directory
// generate a filename to load the cascade from.  Check that
// the file exists - if it doesnt, return an empty string
string getClassifierName(int directory, int stage)
{
   stringstream ss;
   ss << "../cascade_training/classifier_bin_";
   ss << directory;
   ss << "/cascade_oldformat_";
   ss << stage;
   ss << ".xml";

   struct stat fileStat;
   if (stat(ss.str().c_str(), &fileStat) == 0)
      return string(ss.str());
   return string();
}

int main( int argc, const char** argv )
{
   string windowName = "Bin detection";
   string capPath; // Output directory for captured images
   VideoIn *cap; // video input - image, video or camera
   const size_t detectMax = 10;
   const string frameOpt = "--frame=";
   double frameStart = 0.0;
   const string captureAllOpt = "--all";
   const string batchModeOpt = "--batch";
   const string dsOpt = "--ds";

   // Flags for various UI features
   bool pause       = false;  // pause playback?
   bool captureAll  = false;  // capture all found targets to image files?
   bool tracking    = true;   // display tracking info?
   bool printFrames = false;  // print frame number?
   bool batchMode   = false;  // non-interactive mode - no display, run through
                              // as quickly as possible. Combine with --all
   bool ds          = false;  // driver-station?
   
   // Allow switching between CPU and GPU for testing 
   enum CLASSIFIER_MODE
   {
      CLASSIFIER_MODE_UNINITIALIZED,
      CLASSIFIER_MODE_RELOAD,
      CLASSIFIER_MODE_CPU,
      CLASSIFIER_MODE_GPU
   };

   CLASSIFIER_MODE classifierModeCurrent = CLASSIFIER_MODE_UNINITIALIZED;
   CLASSIFIER_MODE classifierModeNext    = CLASSIFIER_MODE_CPU;
   if (gpu::getCudaEnabledDeviceCount() > 0)
      classifierModeNext = CLASSIFIER_MODE_GPU;

   // Classifier directory and stage to start with
   int classifierDirNum   = 7;
   int classifierStageNum = 18;

   // Read through command line args, extract
   // cmd line parameters and input filename
   int fileArgc;
   for (fileArgc = 1; fileArgc < argc; fileArgc++)
   {
      if (frameOpt.compare(0, frameOpt.length(), argv[fileArgc], frameOpt.length()) == 0)
	 frameStart = (double)atoi(argv[fileArgc] + frameOpt.length());
      else if (captureAllOpt.compare(0, captureAllOpt.length(), argv[fileArgc], captureAllOpt.length()) == 0)
	 captureAll = true;
      else if (batchModeOpt.compare(0, batchModeOpt.length(), argv[fileArgc], batchModeOpt.length()) == 0)
	 batchMode = true;
      else if (dsOpt.compare(0, dsOpt.length(), argv[fileArgc], dsOpt.length()) == 0)
	 ds = true;
      else
	 break;
   }
   if (fileArgc >= argc)
   {
      // No arguments? Open default camera
      // and hope for the best
      cap = new VideoIn(0);
      capPath = "negative/2-11";
      windowName = "Camera 0";
   }
   // Digit, but no dot (meaning no file extension)? Open camera
   // Also handle explicit -1 to pick the default camera
   else if (!strstr(argv[fileArgc],".") &&
            (isdigit(*argv[fileArgc]) || !strcmp(argv[fileArgc], "-1")))
   {
      cap = new VideoIn(*argv[fileArgc] - '0');
      capPath = "negative/2-11_" + (*argv[fileArgc] - '0');
      stringstream ss;
      ss << "Camera ";
      ss << argv[fileArgc];
      windowName = ss.str();
   }
   else
   {
      // Open file name - will handle images or videos
      cap = new VideoIn(argv[fileArgc]);
      if (cap->VideoCap())
      {
	 cap->VideoCap()->set(CV_CAP_PROP_POS_FRAMES, frameStart);
	 cap->frameCounter(frameStart);
      }
      capPath = "negative/" + string(argv[fileArgc]).substr(string(argv[fileArgc]).rfind('/')+1);
      windowName = argv[fileArgc];
   }

   Mat frame;
   // If UI is up, pop up the parameters window
   if (!batchMode)
   {
      string detectWindowName = "Detection Parameters";
      namedWindow(detectWindowName);
      createTrackbar ("Scale", detectWindowName, &scale, 50, NULL);
      createTrackbar ("Neighbors", detectWindowName, &neighbors, 50, NULL);
      createTrackbar ("Max Detect", detectWindowName, &maxDetectSize, 1000, NULL);
      createTrackbar ("GPU Scale", detectWindowName, &gpuScale, 100, NULL);
   }
   // Use GPU code if hardware is detected, otherwise
   // fall back to CPU code
   BaseCascadeDetect *detectClassifier = NULL;;
   
   // Grab initial frame to figure out image size and so on
   if (!cap->getNextFrame(false, frame))
   {
      cerr << "Can not read frame from input" << endl;
      return 0;
   }
     
   // Minimum size of a bin at ~30 feet distance
   // TODO : Verify this once camera is calibrated
   minDetectSize = frame.cols * 0.05;

   // Create list of tracked objects
   // recycling bins are 24" wide
   TrackedObjectList binTrackingList(24.0, frame.cols);

   NetworkTable::SetClientMode();
   NetworkTable::SetIPAddress("10.9.0.2");
   NetworkTable *netTable = NetworkTable::GetTable("VisionTable");
   const size_t netTableArraySize = 7; // 7 bins?
   NumberArray netTableArray;

   // 7 bins max, 3 entries each (confidence, distance, angle)
   netTableArray.setSize(netTableArraySize * 3);

   // Frame timing information
#define frameTicksLength (sizeof(frameTicks) / sizeof(frameTicks[0]))
   double frameTicks[3];
   int64 startTick;
   int64 endTick;
   size_t frameTicksIndex = 0;


   // Start of the main loop
   //  -- grab a frame
   //  -- update the angle of tracked objects 
   //  -- do a cascade detect on the current frame
   //  -- add those newly detected objects to the list of tracked objects
   while(cap->getNextFrame(pause, frame))
   {
      startTick = getTickCount(); // start time for this frame

      //TODO : grab angle delta from robot
      // Adjust the position of all of the detected objects
      // to account for movement of the robot between frames
      double deltaAngle = 0.0;
      binTrackingList.adjustAngle(deltaAngle);

      // Code to allow switching between CPU and GPU 
      // for testing
      if ((classifierModeCurrent == CLASSIFIER_MODE_UNINITIALIZED) || 
	  (classifierModeCurrent != classifierModeNext))
      {
	 string classifierName = getClassifierName(classifierDirNum, classifierStageNum);

	 // If reloading with new classifier name, keep the current
	 // CPU/GPU mode setting 
	 if (classifierModeNext == CLASSIFIER_MODE_RELOAD)
	    classifierModeNext = classifierModeCurrent;

	 // Delete the old classifier if it has been initialized
	 if (detectClassifier)
	    delete detectClassifier;

	 // Create a new CPU or GPU classifier based on the
	 // user's selection
	 if (classifierModeNext == CLASSIFIER_MODE_GPU)
	    detectClassifier = new GPU_CascadeDetect(classifierName.c_str());
	 else
	    detectClassifier = new CPU_CascadeDetect(classifierName.c_str());
	 classifierModeCurrent = classifierModeNext;

	 // Verfiy the classifier loaded
	 if( !detectClassifier->loaded() )
	 {
	    cerr << "--(!)Error loading " << classifierName << endl; 
	    return -1; 
	 }
      }
      // Apply the classifier to the frame
      // detectRects is a vector of rectangles, one for each detected object
      // detectDirections is the direction of each detected object - we might not use this
      vector<Rect> detectRects;
      vector<unsigned> detectDirections;
      detectClassifier->cascadeDetect(frame, detectRects, detectDirections); 

      for( size_t i = 0; i < min(detectRects.size(), detectMax); i++ ) 
      {
	 for (size_t j = 0; j < detectRects.size(); j++) {
	    if (i != j) {
	      Rect intersection = detectRects[i] & detectRects[j];
	      if (intersection.width * intersection.height > 0)
	      if (abs((detectRects[i].width * detectRects[i].height) - (detectRects[j].width * detectRects[j].height)) < 2000)
		  if (intersection.width / intersection.height < 5 &&  intersection.width / intersection.height > 0) {
		     Rect lowestYVal;
		     int indexHighest;
		     if(detectRects[i].y < detectRects[j].y) {
			lowestYVal = detectRects[i]; //higher rectangle
			indexHighest = j;
		     }
		     else {	
			lowestYVal = detectRects[j]; //higher rectangle
			indexHighest = i;
		     }
		     if(intersection.y > lowestYVal.y) {
			//cout << "found intersection" << endl;
			if (!batchMode)
			   rectangle(frame, detectRects[indexHighest], Scalar(0,255,255), 3);
			detectRects.erase(detectRects.begin()+indexHighest);
			detectDirections.erase(detectDirections.begin()+indexHighest);
		     }				
		  }
	    }
	 }
	 if (!batchMode)
	 {
	    // Mark detected rectangle on image
	    // Change color based on direction we think the bin is pointing
	    Scalar rectColor;
	    switch (detectDirections[i])
	    {
	       case 1:
		  rectColor = Scalar(0,0,255);
		  break;
	       case 2:
		  rectColor = Scalar(0,255,0);
		  break;
	       case 4:
		  rectColor = Scalar(255,0,0);
		  break;
	       case 8:
		  rectColor = Scalar(255,255,0);
		  break;
	       default:
		  rectColor = Scalar(255,0,255);
		  break;
	    }
	    rectangle( frame, detectRects[i], rectColor, 3);

	    // Label each outlined image with a digit.  Top-level code allows
	    // users to save these small images by hitting the key they're labeled with
	    // This should be a quick way to grab lots of falsly detected images
	    // which need to be added to the negative list for the next
	    // pass of classifier training.
	    stringstream label;
	    label << i;
	    putText(frame, label.str(), Point(detectRects[i].x+10, detectRects[i].y+30), 
		  FONT_HERSHEY_PLAIN, 2.0, Scalar(0, 0, 255));
	 }

	 // Process this detected rectangle - either update the nearest
	 // object or add it as a new one
	 if (!batchMode)
	    binTrackingList.processDetect(detectRects[i]);
      }

      // Print detect status of live objects
      if (tracking)
	 binTrackingList.print();
      // Grab info from trackedobjects, print it out
      vector<TrackedObjectDisplay> displayList;
      binTrackingList.getDisplay(displayList);

      // Clear out network table array
      for (size_t i = 0; i < (netTableArraySize * 3); i++)
	 netTableArray.set(i, -1);

      for (size_t i = 0; i < displayList.size(); i++)
      {
	 if (displayList[i].ratio < 0.15)
	    continue;

	 if (tracking && !batchMode)
	 {
	    // Color moves from red to green (via brown, yuck) 
	    // as the detected ratio goes up
	    Scalar rectColor(0, 255 * displayList[i].ratio, 255 * (1.0 - displayList[i].ratio));

	    // Highlight detected target
	    rectangle(frame, displayList[i].rect, rectColor, 3);

	    // Write detect ID, distance and angle data
	    putText(frame, displayList[i].id, Point(displayList[i].rect.x+25, displayList[i].rect.y+30), FONT_HERSHEY_PLAIN, 2.0, rectColor);
	    stringstream distLabel;
	    distLabel << "D=";
	    distLabel << displayList[i].distance;
	    putText(frame, distLabel.str(), Point(displayList[i].rect.x+10, displayList[i].rect.y+50), FONT_HERSHEY_PLAIN, 1.5, rectColor);
	    stringstream angleLabel;
	    angleLabel << "A=";
	    angleLabel << displayList[i].angle;
	    putText(frame, angleLabel.str(), Point(displayList[i].rect.x+10, displayList[i].rect.y+70), FONT_HERSHEY_PLAIN, 1.5, rectColor);
	 }

	 if (i < netTableArraySize)
	 {
	    netTableArray.set(i*3,   displayList[i].ratio);
	    netTableArray.set(i*3+1, displayList[i].distance);
	    netTableArray.set(i*3+2, displayList[i].angle);
	 }
      }

      if(!ds)
	 netTable->PutValue("VisionArray", netTableArray);

      // Don't update to next frame if paused to prevent
      // objects missing from this frame to be aged out
      // as the current frame is redisplayed over and over
      if (!pause)
	 binTrackingList.nextFrame();

      // Print frame number of video if the option is enabled
      if (!batchMode && printFrames && cap->VideoCap())
      {
	 stringstream ss;
	 ss << cap->frameCounter();
	 ss << '/';
	 ss << cap->VideoCap()->get(CV_CAP_PROP_FRAME_COUNT);
	 putText(frame, ss.str(), Point(frame.cols - 15 * ss.str().length(), 20), FONT_HERSHEY_PLAIN, 1.5, Scalar(0,0,255));
      }

      // Display current classifier under test
      {
	 stringstream ss;
	 ss << classifierDirNum;
	 ss << ',';
	 ss << classifierStageNum;
	 putText(frame, ss.str(), Point(0, frame.rows- 30), FONT_HERSHEY_PLAIN, 1.5, Scalar(0,0,255));
      }

      // For interactive mode, update the FPS as soon as we have
      // a complete array of frame time entries
      // For batch mode, only update every frameTicksLength frames to
      // avoid printing too much stuff
      if ((!batchMode && (frameTicksIndex >= frameTicksLength)) ||
	    (batchMode && ((frameTicksIndex % (frameTicksLength*10)) == 0)))
      {
	 // Get the average frame time over the last
	 // frameTicksLength frames
	 double sum = 0.0;
	 for (size_t i = 0; i < frameTicksLength; i++)
	    sum += frameTicks[i];
	 sum /= frameTicksLength;
	 stringstream ss;
	 // If in batch mode and reading a video, display
	 // the frame count
	 if (batchMode && cap->VideoCap())
	 {
	    ss << cap->frameCounter();
	    ss << '/';
	    ss << cap->VideoCap()->get(CV_CAP_PROP_FRAME_COUNT);
	    ss << " : ";
	 }
	 // Print the FPS
	 ss.precision(3);
	 ss << 1.0 / sum;
	 ss << " FPS";
	 if (!batchMode)
	    putText(frame, ss.str(), Point(frame.cols - 15 * ss.str().length(), 50), FONT_HERSHEY_PLAIN, 1.5, Scalar(0,0,255));
	 else
	    cout << ss.str() << endl;
      }
	 // Driverstation Code
	 if (ds)
	 {
	    bool hits[4];
	    for (int i = 0; i < 4; i++)
	    {
	       Rect dsRect(i * frame.cols / 4, 0, frame.cols/4, frame.rows);
	       rectangle(frame, dsRect, Scalar(0,255,255,3));
	       if (!batchMode)
		  hits[i] = false;
	       for( size_t j = 0; j < displayList.size(); j++ ) 
	       {
		  if (((displayList[j].rect & dsRect) == displayList[j].rect) && (displayList[j].ratio > 0.15))
		  {
		     if (!batchMode)
			rectangle(frame, displayList[j].rect, Scalar(255,128,128), 3);
		     hits[i] = true;
		  }
	       }
	       stringstream ss;
	       ss << "Bin";
	       ss << (i+1);
	       netTable->PutBoolean(ss.str().c_str(), hits[i]);
	    }
	 }
      if (!batchMode)
      {
	 // Put an A on the screen if capture-all is enabled so
	 // users can keep track of that toggle's mode
	 if (captureAll)
	    putText(frame, "A", Point(25,25), FONT_HERSHEY_PLAIN, 2.5, Scalar(0, 255, 255));

	 //-- Show what you got
	 imshow( windowName, frame );

	 // Process user IO
	 char c = waitKey(5);
	 if( c == 'c' ) { break; } // exit
	 else if( c == 'q' ) { break; } // exit
	 else if( c == 27 ) { break; } // exit
	 else if( c == ' ') { pause = !pause; }
	 else if( c == 'f')  // advance to next frame
	 {
	    cap->getNextFrame(false, frame);
	 }
	 else if (c == 'A') // toggle capture-all
	 {
	    captureAll = !captureAll;
	 }
	 else if (c == 't') // toggle tracking info display
	 {
	    tracking = !tracking;
	 }
	 else if (c == 'a') // save all detected images
	 {
	    // Save from a copy rather than the original
	    // so all the markup isn't saved, only the raw image
	    Mat frameCopy;
	    cap->getNextFrame(true, frameCopy);
	    for (size_t index = 0; index < detectRects.size(); index++)
	       writeImage(frameCopy, detectRects, index, capPath.c_str(), cap->frameCounter());
	 }
	 else if (c == 'p') // print frame number to console
	 {
	    cout << cap->frameCounter() << endl;
	 }
	 else if (c == 'P') // Toggle frame # printing to 
	 {
	    printFrames = !printFrames;
	 }
	 else if (c == 'G') // toggle CPU/GPU mode
	 {
	    if (classifierModeNext == CLASSIFIER_MODE_GPU)
	       classifierModeNext = CLASSIFIER_MODE_CPU;
	    else
	       classifierModeNext = CLASSIFIER_MODE_GPU;
	 }
	 else if (c == '.') // higher classifier stage
	 {
	    int num = classifierStageNum + 1;
	    while ((getClassifierName(classifierDirNum, num).length() == 0) &&
		  (num < 100))
	       num += 1;
	    if (num < 100)
	    {
	       classifierStageNum = num;
	       classifierModeNext = CLASSIFIER_MODE_RELOAD;
	    }
	 }
	 else if (c == ',') // lower classifier stage
	 {
	    int num = classifierStageNum - 1;
	    while ((getClassifierName(classifierDirNum, num).length() == 0) &&
		  (num > 0))
	       num -= 1;
	    if (num > 0)
	    {
	       classifierStageNum = num;
	       classifierModeNext = CLASSIFIER_MODE_RELOAD;
	    }
	 }
	 else if (c == '>') // higher classifier dir num
	 {
	    if (getClassifierName(classifierDirNum+1, classifierStageNum).length())
	    {
	       classifierDirNum += 1;
	       classifierModeNext   = CLASSIFIER_MODE_RELOAD;
	    }
	 }
	 else if (c == '<') // higher classifier dir num
	 {
	    if (getClassifierName(classifierDirNum-1, classifierStageNum).length())
	    {
	       classifierDirNum -= 1;
	       classifierModeNext = CLASSIFIER_MODE_RELOAD;
	    }
	 }
	 else if (isdigit(c)) // save a single detected image
	 {
	    Mat frameCopy;
	    cap->getNextFrame(true, frameCopy);
	    writeImage(frameCopy, detectRects, c - '0', capPath.c_str(), cap->frameCounter());
	 }
      }
      // If captureAll is enabled, write each detected rectangle
      // to their own output image file
      if (captureAll && detectRects.size())
      {
	 // Save from a copy rather than the original
	 // so all the markup isn't saved, only the raw image
	 Mat frameCopy;
	 cap->getNextFrame(true, frameCopy);
	 for (size_t index = 0; index < detectRects.size(); index++)
	    writeImage(frameCopy, detectRects, index, capPath.c_str(), cap->frameCounter());
      }
      // Save frame time for the current frame
      endTick = getTickCount();
      frameTicks[frameTicksIndex++ % frameTicksLength] = (double)(endTick - startTick) / getTickFrequency();
   }
   return 0;
}

// Write out the selected rectangle from the input frame
// Save multiple copies - the full size image, that full size image converted to grayscale and histogram equalized, and a small version of each.
// The small version is saved because while the input images to the training process are 20x20
// the detection code can find larger versions of them. Scale them down to 20x20 so the complete detected
// image is used as a negative to the training code. Without this, the training code will pull a 20x20
// sub-image out of the larger full image
void writeImage(const Mat &frame, const vector<Rect> &rects, size_t index, const char *path, int frameCounter)
{
   if (index < rects.size())
   {
      Mat image = frame(rects[index]);
      // Create filename, save image
      stringstream fn;
      fn << path;
      fn << "_";
      fn << frameCounter;
      fn << "_";
      fn << index;
      imwrite(fn.str().substr(fn.str().rfind('\\')+1) + ".png", image);

      // Save grayscale equalized version
      Mat frameGray;
      cvtColor( image, frameGray, CV_BGR2GRAY );
      equalizeHist( frameGray, frameGray );
      imwrite(fn.str().substr(fn.str().rfind('\\')+1) + "_g.png", frameGray);

      // Save 20x20 version of the same image
      Mat smallImg;
      resize(image, smallImg, Size(20,20));
      imwrite(fn.str().substr(fn.str().rfind('\\')+1) + "_s.png", smallImg);

      // Save grayscale equalized version of small image
      cvtColor( smallImg, frameGray, CV_BGR2GRAY );
      equalizeHist( frameGray, frameGray );
      imwrite(fn.str().substr(fn.str().rfind('\\')+1) + "_g_s.png", frameGray);
   }
}
