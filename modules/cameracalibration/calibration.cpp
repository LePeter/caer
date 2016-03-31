#include "calibration.hpp"

Calibration::Calibration(CameraCalibrationSettings settings) {
	this->settings = settings;

	updateSettings();
}

void Calibration::updateSettings(void) {
	if (settings->useFisheyeModel) {
		// The fisheye model has its own enum, so overwrite the flags.
		flag = fisheye::CALIB_FIX_SKEW | fisheye::CALIB_RECOMPUTE_EXTRINSIC | fisheye::CALIB_FIX_K2
			| fisheye::CALIB_FIX_K3 | fisheye::CALIB_FIX_K4;
	}
	else {
		flag = CALIB_FIX_K4 | CALIB_FIX_K5;

		if (settings->aspectRatio) {
			flag |= CALIB_FIX_ASPECT_RATIO;
		}

		if (settings->assumeZeroTangentialDistortion) {
			flag |= CALIB_ZERO_TANGENT_DIST;
		}

		if (settings->fixPrincipalPointAtCenter) {
			flag |= CALIB_FIX_PRINCIPAL_POINT;
		}
	}

	// Update board size.
	boardSize.width = settings->boardWidth;
	boardSize.height = settings->boardHeigth;

	// Clear current image points.
	imagePoints.clear();
}

bool Calibration::findNewPoints(caerFrameEvent frame) {
	if (frame == NULL || !caerFrameEventIsValid(frame)) {
		return (false);
	}

	// Initialize OpenCV Mat based on caerFrameEvent data directly (no image copy).
	Size frameSize(caerFrameEventGetLengthX(frame), caerFrameEventGetLengthY(frame));
	Mat orig = Mat(frameSize, CV_16UC(caerFrameEventGetChannelNumber(frame)), caerFrameEventGetPixelArrayUnsafe(frame));

	// Create a new Mat that has only 8 bit depth from the original 16 bit one.
	// findCorner functions in OpenCV only support 8 bit depth.
	Mat view;
	orig.convertTo(view, CV_8UC(orig.channels()), 1.0/256.0);

	int chessBoardFlags = CALIB_CB_ADAPTIVE_THRESH | CALIB_CB_NORMALIZE_IMAGE;

	if (!settings->useFisheyeModel) {
		// Fast check erroneously fails with high distortions like fisheye lens.
		chessBoardFlags |= CALIB_CB_FAST_CHECK;
	}

	// Find feature points on the input image.
	vector<Point2f> pointBuf;
	bool found;

	switch (settings->calibrationPattern) {
		case CAMCALIB_CHESSBOARD:
			found = findChessboardCorners(view, boardSize, pointBuf, chessBoardFlags);
			break;

		case CAMCALIB_CIRCLES_GRID:
			found = findCirclesGrid(view, boardSize, pointBuf);
			break;

		case CAMCALIB_ASYMMETRIC_CIRCLES_GRID:
			found = findCirclesGrid(view, boardSize, pointBuf, CALIB_CB_ASYMMETRIC_GRID);
			break;

		default:
			found = false;
			break;
	}

	if (found) {
		// Improve the found corners' coordinate accuracy for chessboard pattern.
		if (settings->calibrationPattern == CAMCALIB_CHESSBOARD) {
			Mat viewGray;

			// Only convert color if not grayscale already.
			if (view.channels() == 1) {
				viewGray = view;
			}
			else {
				if (view.channels() == 3) {
					cvtColor(view, viewGray, COLOR_RGB2GRAY);
				}
				else if (view.channels() == 4) {
					cvtColor(view, viewGray, COLOR_RGBA2GRAY);
				}
			}

			cornerSubPix(viewGray, pointBuf, Size(5, 5), Size(-1, -1),
				TermCriteria(TermCriteria::EPS + TermCriteria::COUNT, 30, 0.1));
		}

		imagePoints.push_back(pointBuf);
	}

	return (found);
}

size_t Calibration::foundPoints(void) {
	return (imagePoints.size());
}

double Calibration::computeReprojectionErrors(const vector<vector<Point3f> >& objectPoints,
	const vector<vector<Point2f> >& imagePoints, const vector<Mat>& rvecs, const vector<Mat>& tvecs,
	const Mat& cameraMatrix, const Mat& distCoeffs, vector<float>& perViewErrors, bool fisheye) {
	vector<Point2f> imagePoints2;
	size_t totalPoints = 0;
	double totalErr = 0;
	double err;

	perViewErrors.resize(objectPoints.size());

	for (size_t i = 0; i < objectPoints.size(); i++) {
		if (fisheye) {
			fisheye::projectPoints(objectPoints[i], imagePoints2, rvecs[i], tvecs[i], cameraMatrix, distCoeffs);
		}
		else {
			projectPoints(objectPoints[i], rvecs[i], tvecs[i], cameraMatrix, distCoeffs, imagePoints2);
		}

		err = norm(imagePoints[i], imagePoints2, NORM_L2);

		size_t n = objectPoints[i].size();
		perViewErrors[i] = (float) std::sqrt(err * err / n);
		totalErr += err * err;
		totalPoints += n;
	}

	return (std::sqrt(totalErr / totalPoints));
}

void Calibration::calcBoardCornerPositions(Size boardSize, float squareSize, vector<Point3f>& corners,
	enum CameraCalibrationPattern patternType) {
	corners.clear();

	switch (patternType) {
		case CAMCALIB_CHESSBOARD:
		case CAMCALIB_CIRCLES_GRID:
			for (int y = 0; y < boardSize.height; y++) {
				for (int x = 0; x < boardSize.width; x++) {
					corners.push_back(Point3f(x * squareSize, y * squareSize, 0));
				}
			}
			break;

		case CAMCALIB_ASYMMETRIC_CIRCLES_GRID:
			for (int y = 0; y < boardSize.height; y++) {
				for (int x = 0; x < boardSize.width; x++) {
					corners.push_back(Point3f((2 * x + y % 2) * squareSize, y * squareSize, 0));
				}
			}
			break;

		default:
			break;
	}
}

bool Calibration::runCalibration(Size& imageSize, Mat& cameraMatrix, Mat& distCoeffs,
	vector<vector<Point2f> > imagePoints, vector<Mat>& rvecs, vector<Mat>& tvecs, vector<float>& reprojErrs,
	double& totalAvgErr) {
	// 3x3 camera matrix to fill in.
	cameraMatrix = Mat::eye(3, 3, CV_64F);

	if (flag & CALIB_FIX_ASPECT_RATIO) {
		cameraMatrix.at<double>(0, 0) = settings->aspectRatio;
	}

	if (settings->useFisheyeModel) {
		distCoeffs = Mat::zeros(4, 1, CV_64F);
	}
	else {
		distCoeffs = Mat::zeros(8, 1, CV_64F);
	}

	vector<vector<Point3f> > objectPoints(1);

	calcBoardCornerPositions(boardSize, settings->boardSquareSize, objectPoints[0], settings->calibrationPattern);

	objectPoints.resize(imagePoints.size(), objectPoints[0]);

	// Find intrinsic and extrinsic camera parameters.
	if (settings->useFisheyeModel) {
		Mat _rvecs, _tvecs;
		fisheye::calibrate(objectPoints, imagePoints, imageSize, cameraMatrix, distCoeffs, _rvecs, _tvecs, flag);

		rvecs.reserve(_rvecs.rows);
		tvecs.reserve(_tvecs.rows);

		for (int i = 0; i < int(objectPoints.size()); i++) {
			rvecs.push_back(_rvecs.row(i));
			tvecs.push_back(_tvecs.row(i));
		}
	}
	else {
		calibrateCamera(objectPoints, imagePoints, imageSize, cameraMatrix, distCoeffs, rvecs, tvecs, flag);
	}

	totalAvgErr = computeReprojectionErrors(objectPoints, imagePoints, rvecs, tvecs, cameraMatrix, distCoeffs,
		reprojErrs, settings->useFisheyeModel);

	bool ok = checkRange(cameraMatrix) && checkRange(distCoeffs) && totalAvgErr < settings->maxTotalError;

	return (ok);
}

// Print camera parameters to the output file
void Calibration::saveCameraParams(Size& imageSize, Mat& cameraMatrix, Mat& distCoeffs, const vector<Mat>& rvecs,
	const vector<Mat>& tvecs, const vector<float>& reprojErrs, double totalAvgErr) {
	FileStorage fs(settings->saveFileName, FileStorage::WRITE);

	time_t tm;
	time(&tm);
	struct tm *t2 = localtime(&tm);
	char buf[1024];
	strftime(buf, sizeof(buf) - 1, "%c", t2);

	fs << "calibration_time" << buf;

	if (!rvecs.empty() || !reprojErrs.empty()) {
		fs << "nr_of_frames" << (int) std::max(rvecs.size(), reprojErrs.size());
	}

	fs << "image_width" << imageSize.width;
	fs << "image_height" << imageSize.height;
	fs << "board_width" << boardSize.width;
	fs << "board_height" << boardSize.height;
	fs << "square_size" << settings->boardSquareSize;

	if (flag & CALIB_FIX_ASPECT_RATIO) {
		fs << "aspect_ratio" << settings->aspectRatio;
	}

	if (flag) {
		if (settings->useFisheyeModel) {
			sprintf(buf, "flags:%s%s%s%s%s%s", flag & fisheye::CALIB_FIX_SKEW ? " +fix_skew" : "",
				flag & fisheye::CALIB_FIX_K1 ? " +fix_k1" : "", flag & fisheye::CALIB_FIX_K2 ? " +fix_k2" : "",
				flag & fisheye::CALIB_FIX_K3 ? " +fix_k3" : "", flag & fisheye::CALIB_FIX_K4 ? " +fix_k4" : "",
				flag & fisheye::CALIB_RECOMPUTE_EXTRINSIC ? " +recompute_extrinsic" : "");
		}
		else {
			sprintf(buf, "flags:%s%s%s%s%s%s%s%s%s%s", flag & CALIB_USE_INTRINSIC_GUESS ? " +use_intrinsic_guess" : "",
				flag & CALIB_FIX_ASPECT_RATIO ? " +fix_aspect_ratio" : "",
				flag & CALIB_FIX_PRINCIPAL_POINT ? " +fix_principal_point" : "",
				flag & CALIB_ZERO_TANGENT_DIST ? " +zero_tangent_dist" : "", flag & CALIB_FIX_K1 ? " +fix_k1" : "",
				flag & CALIB_FIX_K2 ? " +fix_k2" : "", flag & CALIB_FIX_K3 ? " +fix_k3" : "",
				flag & CALIB_FIX_K4 ? " +fix_k4" : "", flag & CALIB_FIX_K5 ? " +fix_k5" : "",
				flag & CALIB_FIX_K6 ? " +fix_k6" : "");
		}

		cvWriteComment(*fs, buf, 0);
	}

	fs << "flags" << flag;

	fs << "use_fisheye_model" << settings->useFisheyeModel;

	fs << "camera_matrix" << cameraMatrix;
	fs << "distortion_coefficients" << distCoeffs;

	fs << "avg_reprojection_error" << totalAvgErr;
	if (!reprojErrs.empty()) {
		fs << "per_view_reprojection_errors" << Mat(reprojErrs);
	}

	if (!rvecs.empty() && !tvecs.empty()) {
		CV_Assert(rvecs[0].type() == tvecs[0].type());
		Mat bigmat((int) rvecs.size(), 6, rvecs[0].type());

		for (size_t i = 0; i < rvecs.size(); i++) {
			Mat r = bigmat(Range(int(i), int(i + 1)), Range(0, 3));
			Mat t = bigmat(Range(int(i), int(i + 1)), Range(3, 6));

			CV_Assert(rvecs[i].rows == 3 && rvecs[i].cols == 1);
			CV_Assert(tvecs[i].rows == 3 && tvecs[i].cols == 1);

			// *.t() is MatExpr (not Mat) so we can use assignment operator.
			r = rvecs[i].t();
			t = tvecs[i].t();
		}

		fs << "extrinsic_parameters" << bigmat;
	}

	// Close file.
	fs.release();
}

bool Calibration::runCalibrationAndSave(void) {
	// Only run if enough valid points have been accumulated.
	if (imagePoints.size() < settings->minNumberOfPoints) {
		return (false);
	}

	Size imageSize(settings->imageWidth, settings->imageHeigth);
	vector<Mat> rvecs, tvecs;
	vector<float> reprojErrs;
	double totalAvgErr = 0;

	bool ok = runCalibration(imageSize, cameraMatrix, distCoeffs, imagePoints, rvecs, tvecs, reprojErrs, totalAvgErr);

	if (ok) {
		saveCameraParams(imageSize, cameraMatrix, distCoeffs, rvecs, tvecs, reprojErrs, totalAvgErr);
	}

	return (ok);
}

bool Calibration::loadUndistortMatrices(void) {
	// Open file with undistort matrices.
	FileStorage fs(settings->loadFileName, FileStorage::READ);

	Mat undistortCameraMatrix;
	Mat undistortDistCoeffs;
	bool useFisheyeModel = false;

	fs["camera_matrix"] >> undistortCameraMatrix;
	fs["distortion_coefficients"] >> undistortDistCoeffs;
	fs["use_fisheye_model"] >> useFisheyeModel;

	// Close file.
	fs.release();

	// Generate maps for frame remap().
	Size imageSize(settings->imageWidth, settings->imageHeigth);

	if (useFisheyeModel) {
		Mat optimalCameramatrix;
		fisheye::estimateNewCameraMatrixForUndistortRectify(undistortCameraMatrix, undistortDistCoeffs, imageSize,
			Matx33d::eye(), optimalCameramatrix, 1);

		fisheye::initUndistortRectifyMap(undistortCameraMatrix, undistortDistCoeffs, Matx33d::eye(),
			optimalCameramatrix, imageSize,
			CV_16SC2, undistortRemap1, undistortRemap2);
	}
	else {
		Mat optimalCameramatrix = getOptimalNewCameraMatrix(undistortCameraMatrix, undistortDistCoeffs, imageSize, 1,
			imageSize, 0);

		initUndistortRectifyMap(undistortCameraMatrix, undistortDistCoeffs, Mat(), optimalCameramatrix, imageSize,
		CV_16SC2, undistortRemap1, undistortRemap2);
	}

	// TODO: generate LUT for event undistortion.

	return (true);
}

void Calibration::undistortEvent(caerPolarityEvent polarity) {
	if (polarity == NULL || !caerPolarityEventIsValid(polarity)) {
		return;
	}

	// TODO: use LUT to undistort.
}

void Calibration::undistortFrame(caerFrameEvent frame) {
	if (frame == NULL || !caerFrameEventIsValid(frame)) {
		return;
	}

	Size frameSize(caerFrameEventGetLengthX(frame), caerFrameEventGetLengthY(frame));
	Mat view = Mat(frameSize, CV_16UC(caerFrameEventGetChannelNumber(frame)), caerFrameEventGetPixelArrayUnsafe(frame));
	Mat inView = view.clone();

	remap(inView, view, undistortRemap1, undistortRemap2, REMAP_INTERPOLATION);
}
