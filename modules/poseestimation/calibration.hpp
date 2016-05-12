#ifndef POSEESTIMATION_HPP_
#define POSEESTIMATION_HPP_

#include <iostream>
#include <sstream>
#include <time.h>
#include <stdio.h>
#include "calibration_settings.h"

#include <libcaer/events/polarity.h>
#include <libcaer/events/frame.h>

#include <opencv2/core.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>

#include <iostream>
#include <aruco/aruco.h>
#include <aruco/cvdrawingutils.h>
#include <opencv2/highgui/highgui.hpp>

using namespace cv;
using namespace std;
using namespace aruco;

class PoseCalibration {

public:
	PoseCalibration(PoseCalibrationSettings settings);
	bool findMarkers(caerFrameEvent frame);
	
private:

};

#endif /* CALIBRATION_HPP_ */
