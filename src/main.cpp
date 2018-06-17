#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include <iostream>
#include <stdio.h>
#include <ctime>
#include <fstream>
#include <pcl/point_types.h>
#include <pcl/registration/icp.h>
#include <pcl/visualization/cloud_viewer.h>

using namespace cv;
using namespace std;

#include "IPM.h"



const double y_shift = 0;
const double x_shift = 0;

struct odom_t {
	float x, y, yaw, speed, yawrate, accel; 
};

struct particle_t {
	float x, y;
};
const double CAM_DST = 7.45;

int main( int _argc, char** _argv )
{
	// Images
	cv::Mat frame;
	Mat inputImg, inputImgGray, calibImg, showImg0;
	Mat top, outputImg, occgrid;
	
	if( _argc != 4 )
	{
		cout << "Usage: ipm.exe <videofile> <calib.yml> <data.poses>" << endl;
		return 1;
	}	
	string videoFileName  = _argv[1];	
	string yml_filename   = _argv[2];	
	string poses_filename = _argv[3];	

	// Video
	cv::VideoCapture video;
	if( !video.open(videoFileName) )
		return 1;

	// Show video information
	int ratio = 1;
	int width = 0, height = 0, fps = 0, fourcc = 0;	
	width = static_cast<int>(video.get(CV_CAP_PROP_FRAME_WIDTH) + x_shift*2) / ratio;
	height = static_cast<int>(video.get(CV_CAP_PROP_FRAME_HEIGHT) + y_shift*2) / ratio;
	fps = static_cast<int>(video.get(CV_CAP_PROP_FPS));
	fourcc = static_cast<int>(video.get(CV_CAP_PROP_FOURCC));

	cout << "Input video: (" << width << "x" << height << ") at " << fps << ", fourcc = " << fourcc << endl;

	int slider[5];
	slider[0] = 955;
	slider[1] = 472;
	slider[2] = 178;
	slider[3] = 74;
	slider[4] = 1;//166;
    /*
	slider[0] = 1500;
	slider[1] = 580;
	slider[2] = 1040;
	slider[3] = 817;
	slider[4] = 120;
    */
	/// Create Windows
	namedWindow("roadslam", 1);
	createTrackbar( "s0", "roadslam", &slider[0], width*4, NULL );
	createTrackbar( "s1", "roadslam", &slider[1], height*2, NULL );
	createTrackbar( "s2", "roadslam", &slider[2], width*2, NULL );
	createTrackbar( "s3", "roadslam", &slider[3], width*2, NULL );
	createTrackbar( "s4", "roadslam", &slider[4], 255, NULL );

	// calib
	cv::Mat cameraMatrix;
	cv::Mat distCoeffs;
    FileStorage fs;
    fs.open(yml_filename, FileStorage::READ);
    if( !fs.isOpened() ){
        cerr << " Fail to open " << yml_filename << endl;
        exit(EXIT_FAILURE);
    }
    // Get camera parameters
    fs["camera_matrix"] >> cameraMatrix;
    fs["dist_coefs"] >> distCoeffs; 
    // Print out the camera parameters
    cout << "\n -- Camera parameters -- " << endl;
    cout << "\n CameraMatrix = " << endl << " " << cameraMatrix << endl << endl;
    cout << " Distortion coefficients = " << endl << " " << distCoeffs << endl << endl;
	
	IPM *ipm = NULL;

	// poses
	std::vector<odom_t> odoms;

	std::ifstream fposes;
	fposes.open(poses_filename.c_str());
	if(!fposes.is_open()) {
		std::cout<<"cant open file: "<<poses_filename<<"\n";
		return 1;
	}
	std::string trash;	
	while(fposes>>trash) {
		
		int n_frame;
		fposes>>n_frame;
		std::cout<<trash<<n_frame<<"\n";

		odom_t o;
		fposes>>o.x
		      >>o.y
			  >>o.yaw
			  >>o.speed
			  >>o.yawrate
			  >>o.accel;
		odoms.push_back(o);
	}

	pcl::PointCloud<pcl::PointXYZ>::Ptr frameCloud(new pcl::PointCloud<pcl::PointXYZ>); 
	pcl::PointCloud<pcl::PointXYZ>::Ptr gCloud(new pcl::PointCloud<pcl::PointXYZ>);

    pcl::visualization::CloudViewer viewer ("Cloud Viewer");
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr vizCloud(new pcl::PointCloud<pcl::PointXYZRGB>); 

	Point2f pose(0, 0);
	double yaw = 0;

	// Main loop
	int frameNum = 0;
	for( ; ; )
	{
		printf("FRAME #%6d %4.3f", frameNum, odoms[frameNum].speed);
		fflush(stdout);
		frameNum++;


		// The 4-points at the input image	
		vector<Point2f> origPoints;
		origPoints.push_back( Point2f(width/2 - slider[0],  slider[2]) );
		origPoints.push_back( Point2f(width/2 + slider[0],  slider[2]) );
		origPoints.push_back( Point2f(width/2 + slider[1],  slider[3]) );
		origPoints.push_back( Point2f(width/2 - slider[1],  slider[3]) );

		// The 4-points correspondences in the destination image
		vector<Point2f> dstPoints;
		dstPoints.push_back( Point2f(0, height) );
		dstPoints.push_back( Point2f(width, height) );
		dstPoints.push_back( Point2f(width, 0) );
		dstPoints.push_back( Point2f(0, 0) );
			
		// IPM object
		if(ipm == NULL)
			ipm = new IPM( Size(width, height), Size(width, height), origPoints, dstPoints );


		// Get current image		
		video >> frame;
		if( frame.empty() )
			break;
		//frame = imread(img_filename);

		if(frameNum < 100)
			continue;

		undistort(frame, inputImg, cameraMatrix, distCoeffs);
		

		// Color Conversion
		cvtColor(inputImg, inputImgGray, CV_BGR2GRAY);				 		 	
	

		 // Process IPM
		clock_t begin = clock();
		ipm->applyHomography( inputImgGray, top );		 
		clock_t end = clock();
		double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
		printf("%.2f (ms)\r", 1000*elapsed_secs);
		ipm->drawPoints(origPoints, inputImg );
		
		// thresh
		resize(top, outputImg, Size(), 0.05, 0.05);
		blur(outputImg, occgrid, Size(3, 3));
		adaptiveThreshold(occgrid, occgrid, 255, CV_ADAPTIVE_THRESH_MEAN_C,THRESH_BINARY,7,-slider[4]);
		imshow("local map", occgrid);
		
		float dt = 0.033;
		float grid2mt = 10.0/90.0;

		yaw = odoms[frameNum].yaw;
		double yaw_sin = sin(yaw);
		double yaw_cos = cos(yaw);
		pose.x += yaw_cos*odoms[frameNum].speed*dt;
		pose.y += yaw_sin*odoms[frameNum].speed*dt;

		particle_t pts[100*100];
		int n_pts = 0;
		for(int i=0; i<occgrid.rows; i++) {
			for(int j=0; j<occgrid.cols; j++) {
				uint8_t val = occgrid.data[i*occgrid.cols + j];
				double ly = j - occgrid.cols/2;
				double lx = -i + occgrid.rows - CAM_DST*grid2mt;
				double rx = lx*yaw_cos - ly*yaw_sin;
				double ry = lx*yaw_sin + ly*yaw_cos;

				if(val > 0) {
					pts[n_pts].x = pose.x + double(rx)*grid2mt;
					pts[n_pts].y = pose.y + double(ry)*grid2mt;
					n_pts++;
				}
			}
		}

		int size = n_pts;
		frameCloud->points.resize(size);
		for(int i=0; i<size; i++) {
			frameCloud->points[i].x = pts[i].x;
			frameCloud->points[i].z = pts[i].y;
			frameCloud->points[i].y = 0;
		}

    	if(frameNum == 100)
			*gCloud = *frameCloud;

    	// run ICP
		pcl::PointCloud<pcl::PointXYZ> Final;
    	pcl::IterativeClosestPoint<pcl::PointXYZ, pcl::PointXYZ> icp;
    	icp.setInputCloud(frameCloud);
    	icp.setInputTarget(gCloud);
    	icp.align(Final);

		size = gCloud->points.size();
		int new_size = Final.points.size() + size;
		gCloud->points.resize(new_size);
		for(int i=size; i<new_size; i++) {
			gCloud->points[i].x = Final.points[i- size].x;
			gCloud->points[i].z = Final.points[i- size].z;
		}

		pcl::copyPointCloud(*gCloud,*vizCloud);
		for(int i=0; i<vizCloud->points.size(); i++) {
			// pack r/g/b into rgb
			uint8_t r = 255, g = 0, b = 0;    // Example: Red color
			uint32_t rgb = ((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b);
			vizCloud->points[i].rgb = *reinterpret_cast<float*>(&rgb);
		}
		viewer.showCloud (vizCloud);
					

		int key = waitKey(1);
		if(key == 114)
			ipm = NULL;

	}

	return 0;	
}		
