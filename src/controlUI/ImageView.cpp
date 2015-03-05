/**
Created on 21st February 2015
Author: Anirudh Vemula
*/

#include "ros/ros.h"
#include "ImageView.h"
#include <cvd/gl_helpers.h>
//#include <gvars3/instances.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>
#include "GLWindow2.h"
#include <iostream>
#include <fstream>
#include <string>
#include "std_msgs/String.h"
#include "../HelperFunctions.h"
#include "ControlUINode.h"

ImageView::ImageView(ControlUINode *cnode) {
	frameWidth = frameHeight = 0;

	video_channel = nh_.resolveName("ardrone/image_raw"); // Change this for undistorted image
	command_channel = nh_.resolveName("tum_ardrone/com");

	vid_sub = nh_.subscribe(video_channel, 10, &ImageView::vidCb, this);
	tum_ardrone_sub = nh_.subscribe(command_channel, 50, &ImageView::comCb, this);

	tum_ardrone_pub = nh_.advertise<std_msgs::String>(command_channel, 50);

	node = cnode;

	numPointsClicked = 0;
	numKeyPointsDetected = 0;

	considerAllLevels = false;
}

ImageView::~ImageView() {

}

void ImageView::vidCb (const sensor_msgs::ImageConstPtr img) {
	newImage(img);
}

void ImageView::comCb (const std_msgs::StringConstPtr str) {

}

void ImageView::startSystem() {
	keepRunning = true;
	changeSizeNextRender = false;
	start();
}

void ImageView::stopSystem() {
	keepRunning = false;
	new_frame_signal.notify_all();
	join();
}

void ImageView::ResetInternal() {
	mimFrameBW.resize(CVD::ImageRef(frameWidth, frameHeight));
	mimFrameBW_workingCopy.resize(CVD::ImageRef(frameWidth, frameHeight));
}


void ImageView::newImage(sensor_msgs::ImageConstPtr img) {
	cv_bridge::CvImagePtr cv_ptr = cv_bridge::toCvCopy(img, sensor_msgs::image_encodings::MONO8);

	boost::unique_lock<boost::mutex> lock(new_frame_signal_mutex);

	//copy to internal image, convert to bw, set flag
	if (ros::Time::now() - img->header.stamp > ros::Duration(30.0)) {
		mimFrameTimeRos = (ros::Time::now()  - ros::Duration(0.001));
	}
	else {
		mimFrameTimeRos = img->header.stamp;
	}

	mimFrameTime = getMS(mimFrameTimeRos);

	mimFrameSEQ = img->header.seq;

	//copy to mimFrame
	if (mimFrameBW.size().x != img->width || mimFrameBW.size().y != img->height)
		mimFrameBW.resize(CVD::ImageRef(img->width, img->height));

	memcpy(mimFrameBW.data(), cv_ptr->image.data, img->width * img->height);
	newImageAvailable = true;

	lock.unlock();
	new_frame_signal.notify_all();
}


void ImageView::run() {
	while(!newImageAvailable)
		usleep(100000);
	newImageAvailable = false;
	while(!newImageAvailable)
		usleep(100000);

	// read image height and width
	frameHeight = mimFrameBW.size().y;
	frameWidth = mimFrameBW.size().x;

	ResetInternal();

	//create window
	myGLWindow = new GLWindow2(CVD::ImageRef(frameWidth, frameHeight), "DRONE CAMERA FEED", this);
	myGLWindow->set_title("DRONE CAMERA FEED");

	changeSizeNextRender = true;
	if (frameWidth < 640) {
		desiredWindowSize = CVD::ImageRef(frameWidth*2, frameHeight*2);
	}
	else {
		desiredWindowSize = CVD::ImageRef(frameWidth, frameHeight);
	}

	boost::unique_lock<boost::mutex> lock(new_frame_signal_mutex);

	while(keepRunning) {
		if(newImageAvailable) {
			newImageAvailable = false;

			//copy to working copy
			mimFrameBW_workingCopy.copy_from(mimFrameBW);
			mimFrameTime_workingCopy = mimFrameTime;
			mimFrameSEQ_workingCopy = mimFrameSEQ;
			mimFrameTimeRos_workingCopy = mimFrameTimeRos;

			//release lock
			lock.unlock();

			renderFrame();

			if (changeSizeNextRender) {
				myGLWindow->set_size(desiredWindowSize);
				changeSizeNextRender = false;
			}

			lock.lock();
		}
		else
			new_frame_signal.wait(lock);
	}

	lock.unlock();
	delete myGLWindow;

}

void ImageView::renderFrame() {

	//setup
	myGLWindow->SetupViewport();
	myGLWindow->SetupVideoOrtho();
	myGLWindow->SetupVideoRasterPosAndZoom();

	// render image from drone camera
	glDrawPixels(mimFrameBW_workingCopy);

	// render clicked points
	glPointSize(6);
	glEnable(GL_BLEND);
	glEnable(GL_POINT_SMOOTH);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	/*glColor3f(1.0,0,0);
	glBegin(GL_POINTS);
	for (int i = 0; i < numPointsClicked; ++i)
	{
		glVertex2i(pointsClicked[i][0], pointsClicked[i][1]);
	}
	glEnd();*/
	
	// render detected keyPoints
	glColor3f(0, 0, 1.0);
	glBegin(GL_POINTS);
	for (int i = 0; i < numKeyPointsDetected; ++i)
	{
		//glVertex2i(keyPointsNearest[i][0], keyPointsNearest[i][1]);
		std::vector<int> p;
		bool found = node->get2DPoint(keyPointsNearest[i], p, considerAllLevels);
		// ROS_INFO("Number of keypoints : %d\n", node->getNumKP(considerAllLevels));
		if(found) {
			// ROS_INFO("Original 3d Point is %f, %f, %f", keyPointsNearest[i][0], keyPointsNearest[i][1], keyPointsNearest[i][2]);
			// ROS_INFO("Point is %d, %d", p[0], p[1]);
			glVertex2i(p[0], p[1]);
		}
		else {
			//printf("not found");
			// ROS_INFO("Point not found in frame");
		}
	}
	glEnd();
	glDisable(GL_BLEND);

	myGLWindow->swap_buffers();
	myGLWindow->HandlePendingEvents();
}

// keyboard input
void ImageView::on_key_down(int key) {
	if(key == 114) // r
	{
		// node->publishCommand("i reset");
		// Need to reset all the entire points to initial state
		numPointsClicked = 0;
		numKeyPointsDetected = 0;
		pointsClicked.clear();
		keyPointsNearest.clear();
	}
	if(key == 100) // d
	{
		// node->publishCommand("i delete");
		// Need to delete the last point
		if(numPointsClicked!=0) {
			numPointsClicked--;
			pointsClicked.pop_back();
		}
		if(numKeyPointsDetected!=0) {
			numKeyPointsDetected--;
			keyPointsNearest.pop_back();	
		}
	}
	if(key == 32) // space
	{
		// node->publishCommand("i run");
		// Send control commands to drone. TODO need to implement
	}
}

void ImageView::on_mouse_down(CVD::ImageRef where, int state, int button) {
	//double x = 4*(where.x/(double)this->myGLWindow->size().x - 0.5);
	//double y = -4*(where.y/(double)this->myGLWindow->size().y - 0.5);

	int x;
	int y;

	if(this->myGLWindow->size().x==640) {
		x = where.x;
	}
	else if(this->myGLWindow->size().x!=0){
		x = (int)((640.0*where.x)/(this->myGLWindow->size().x));
	}

	if(this->myGLWindow->size().y==360) {
		y = where.y;
	}
	else if(this->myGLWindow->size().y!=0){
		y = (int)((360.0*where.y)/(this->myGLWindow->size().y));
	}

	printf("X and Y of point clicked : (%d, %d)\n", x, y);

	numPointsClicked++;
	std::vector<int> v;
	v.push_back(x);
	v.push_back(y);
	pointsClicked.push_back(v);

	search(v);
}


void ImageView::search(std::vector<int> pt) {
	std::vector<float> kp = node->searchNearest(pt, considerAllLevels);

	keyPointsNearest.push_back(kp);
	numKeyPointsDetected++;
}

/*bool ImageView::handleCommand(std::string s) {

}*/