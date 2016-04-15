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

/*typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
typedef CGAL::Polyhedron_3<K> Polyhedron_3;
typedef K::Segment_3 Segment_3;

typedef K::Point_3 Point_3;
typedef CGAL::Creator_uniform_3<double,Point_3> PointCreator;*/
using namespace std;

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

	considerAllLevels = true;
	renderPoly = false;
	renderRect = false;

	numFile = 0;
	translateDistance = 0.5;
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
	cv_bridge::CvImagePtr cv_ptr = cv_bridge::toCvCopy(img, sensor_msgs::image_encodings::RGB8);

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
	if (mimFrameBW.size().x != (int)img->width || mimFrameBW.size().y != (int)img->height)
		mimFrameBW.resize(CVD::ImageRef(img->width, img->height));

	memcpy(mimFrameBW.data(), cv_ptr->image.data, img->width * img->height * 3);
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

	glPointSize(6);
	glEnable(GL_BLEND);
	glEnable(GL_POINT_SMOOTH);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glLineWidth(5);

	// Rendering clicked points

	/*glColor3f(1.0,0,0);
	glBegin(GL_POINTS);
	for (int i = 0; i < numPointsClicked; ++i)
	{
		glVertex2i(pointsClicked[i][0], pointsClicked[i][1]);
	}
	glEnd();*/
	
	// render detected keyPoints
	if(!renderPoly) {
		glColor3f(0, 0, 1.0);
		glBegin(GL_POINTS);
		for (int i = 0; i < numKeyPointsDetected; ++i)
		{
			vector<int> p;
			bool found = node->get2DPoint(keyPointsNearest[i], p, considerAllLevels);
			if(found) {
				glVertex2i(p[0], p[1]);
			}
			else {

			}
		}
		glEnd();
	}
	else if(renderPoly) {
		// render convex hull polygon

		/*glColor3f(0, 0, 1.0);
		glBegin(GL_POINTS);
		for (int i = 0; i < numKeyPointsDetected; ++i)
		{
			vector<int> p;
			bool found = node->get2DPoint(keyPointsNearest[i], p, considerAllLevels);
			if(found) {
				glVertex2i(p[0], p[1]);
			}
			else {

			}
		}
		glEnd();*/


		glColor3f(0, 1.0, 0.0);
		glBegin(GL_LINE_STRIP);
		for(unsigned int i=0; i<ccPoints.size(); i++) {
			vector<int> p;
			bool found = node->get2DPoint(keyPointsNearest[ccPoints[i]], p, considerAllLevels);
			if(found) {
				glVertex2i(p[0], p[1]);
			}
			else {

			}
		}
		glEnd();
	}
	if(renderRect)
	{		
		int size = continuousBoundingBoxPoints.size();
		
		for(int planeIndex = 0; planeIndex<size; planeIndex++) {
			vector<Point3f> planeBoundingBoxPoints = continuousBoundingBoxPoints[planeIndex];	
			vector< vector<int> > planePts2D;
			//glColor3f(0, 1.0, 0.0);
			glColor3f(1.0-1.0/(planeIndex+1), 0, 1.0/(planeIndex+1)); 
		 	glBegin(GL_LINE_STRIP);
		/*	
			for(int pointIndex = 0; pointIndex<planeBoundingBoxPoints.size(); pointIndex++) {
				vector<float> pt(3);
				pt[0] = planeBoundingBoxPoints[pointIndex].x;
				pt[1] = planeBoundingBoxPoints[pointIndex].y;
				pt[2] = planeBoundingBoxPoints[pointIndex].z;
				vector<int> p;
				bool found = node->get2DPoint(pt, p, true);
				if (found){
					planePts2D.push_back(p);
					glVertex2i(p[0],p[1]);
				}
			}
		*/	
			vector<Point2f> imagePts;
			node->project3DPointsOnImage(planeBoundingBoxPoints, imagePts);
            for(int pointIndex = 0; pointIndex<imagePts.size(); pointIndex++) {
				vector<int> p(2);
				p[0] = imagePts[pointIndex].x;
				p[1] = imagePts[pointIndex].y;
				glVertex2i(p[0],p[1]);
			}
		
			glEnd();
		}
	}
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
		renderPoly = false;
		renderRect= false;
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
	if(key == 115) // s
	{
		// save all the keypoint information
		node->saveKeyPointInformation(numFile);
		numFile++;
	}
	if(key == 116) // t
	{
		// renders the polygon
		//renderRect = false;
		renderPoly = !renderPoly;
		extractBoundingPoly();
	}
	if(key == 98) // b
	{
		// renders the bounding rectangle
		//renderPoly = false;
		//renderRect = !renderRect;
		extractBoundingRect();
	}
	if(key == 101) // e
	{
		// Extract multiple planes

		extractBoundingPoly();
		/*
		 * TODO Currently commented code which fits single plane and moves the drone accordingly
		 * Appropriate code needs to be added which will handle multiple planes
		 * For time being for multiple planes each plane is shown in different color
		 *
		vector<float> plane = node->fitPlane3d (ccPoints, pointsClicked);
		vector<vector<float> > pPoints =  node->projectPoints (ccPoints, keyPointsNearest);
		grid g = node->buildGrid(pPoints);
		vector<vector<double> > tPoints =  node->getTargetPoints(g, plane);
		node->moveDrone(tPoints);
		*/
		renderRect = true;
		int size = continuousBoundingBoxPoints.size();
		assert(size == planeParameters.size());
		int i,j;
		for (i = 0; i < size; ++i) {
			continuousBoundingBoxPoints[i].clear();
			planeParameters[i].clear();
		}
		continuousBoundingBoxPoints.clear();
		planeParameters.clear();
	
		node->fitMultiplePlanes3d(ccPoints, pointsClicked, planeParameters, continuousBoundingBoxPoints);
		cout << "[ DEBUG ] continuousBoundingBoxPoints from ImageView\n";
		size = continuousBoundingBoxPoints.size();
		for (i = 0; i < size; ++i) {
			for (j = 0; j < 5; ++j) {
				cout << continuousBoundingBoxPoints[i][j] << " ";
			}
			cout << "\n";
		}
		//vector<float> translatedPlane = node->translatePlane (translateDistance);
		
	}
	if(key == 103) //g 			
	{
		//Cover multiple planes
		if(renderRect){
			renderRect = false;  //while moving the quadcopter we don't want bounding box to appear
			node->moveQuadcopter(planeParameters, continuousBoundingBoxPoints);	
		}
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
	vector<int> v;
	v.push_back(x);
	v.push_back(y);
	pointsClicked.push_back(v);

	search(v);
}



void ImageView::search(vector<int> pt) {
	vector<float> kp = node->searchNearest(pt, considerAllLevels);

	keyPointsNearest.push_back(kp);
	numKeyPointsDetected++;
	printf("X, Y and Z of nearest keypoint : (%f, %f, %f)\n", kp[0], kp[1], kp[2]);
}

void ImageView::extractBoundingPoly() {
	// Check if the number of clicked points is exactly 4
	//assert(pointsClicked.size()==4);
	
	//vector<vector<int> > ccPoints;
	ccPoints.clear();

	vector<int> minXPoint;
	float minX = -1;
	int minXIndex = -1;
	for(unsigned int i=0; i<pointsClicked.size(); i++) {
		if(minX==-1) {
			minX = pointsClicked[i][0];
			minXPoint = pointsClicked[i];
			minXIndex = i;
		}
		else if(pointsClicked[i][0]<minX) {
			minX = pointsClicked[i][0];
			minXPoint = pointsClicked[i];
			minXIndex = i;
		}
	}

	//ccPoints.push_back(minXPoint);

	vector<int> endPoint;
	int endIndex;
	int i=0;
	do{
		ccPoints.push_back(minXIndex);
		endPoint = pointsClicked[0];
		endIndex = 0;
		for(unsigned int j=1;j<pointsClicked.size();j++) {
			if((endPoint[0]==minXPoint[0] && endPoint[1]==minXPoint[1]) || onLeft(pointsClicked[j], minXPoint, endPoint)) {
				endPoint = pointsClicked[j];
				endIndex = j;
			}
		}
		i = i+1;
		minXPoint = endPoint;
		minXIndex = endIndex;

	} while(endPoint[0]!=pointsClicked[ccPoints[0]][0] || endPoint[1]!=pointsClicked[ccPoints[0]][1]);
	ccPoints.push_back(endIndex);

}

void ImageView::extractBoundingRect() {

	bPoints.clear();

	int minX = pointsClicked[0][0], minY = pointsClicked[0][1], maxX = pointsClicked[0][0], maxY = pointsClicked[0][1];
	int minXIndex = 0, minYIndex =0, maxXIndex = 0, maxYIndex = 0;

	for(unsigned int i=0; i<pointsClicked.size(); i++) {
		if(pointsClicked[i][0] < minX) {
			minX = pointsClicked[i][0];
			minXIndex = i;
		}
		if(pointsClicked[i][0] > maxX) {
			maxX = pointsClicked[i][0];
			maxXIndex = i;
		}
		if(pointsClicked[i][1] < minY) {
			minY = pointsClicked[i][1];
			minYIndex = i;
		}
		if(pointsClicked[i][1] > maxY) {
			maxY = pointsClicked[i][1];
			maxYIndex = i;
		}
	}

	bPoints.push_back(minXIndex);
	bPoints.push_back(minYIndex);
	bPoints.push_back(maxXIndex);
	bPoints.push_back(maxYIndex);
}
