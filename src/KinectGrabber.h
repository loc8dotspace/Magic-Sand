#pragma once
#include "ofMain.h"

#include "ofxOpenCv.h"
#include "ofxCv.h"
#include "ofxKinect.h"

//#include "FrameFilter.h"
#include "Utils.h"

//#include <Geometry/HVector.h>
//#include <Geometry/Plane.h>
//#include <Geometry/Matrix.h>
//#include <Geometry/ProjectiveTransformation.h>

using namespace states;

class KinectGrabber: public ofThread {
public:
	typedef unsigned short RawDepth; // Data type for raw depth values
	typedef float FilteredDepth; // Data type for filtered depth values

	KinectGrabber();
	~KinectGrabber();
    void setup(General_state, Calibration_state);
    void setupFramefilter(int gradFieldresolution, float newMaxOffset, ofRectangle ROI);
    void initiateBuffers(void); // Reinitialise buffers
    void resetBuffers(void);

    ofMatrix4x4 getWorldMatrix();
   
    void decStoredframes(){
        storedframes -= 1;
    }
    bool isFirstImageReady(){
        return firstImageReady;
    }
    bool isFrameNew(){
        return newFrame;
    }
    ofVec2f getKinectSize(){
        return ofVec2f(width, height);
    }
    void setMaxOffset(float newMaxOffset){
        maxOffset = newMaxOffset;
    }
    
	ofThreadChannel<ofFloatPixels> filtered;
	ofThreadChannel<ofPixels> colored;
	ofThreadChannel<ofVec2f*> gradient;
	ofThreadChannel<General_state> generalStateChannel;
	ofThreadChannel<Calibration_state> calibrationStateChannel;
    ofThreadChannel<ofRectangle> ROIchannel;
    
private:
	void threadedFunction();
    void filter();
    void applySpaceFilter();
    void updateGradientField();
    void setMode(General_state sgeneralState, Calibration_state scalibrationState);
    void setKinectROI(ofRectangle skinectROI);
    bool isInsideROI(int x, int y); // test is x, y is inside ROI
    
    // General state flags and variables
    General_state generalState;
    Calibration_state calibrationState;
	bool newFrame;
    bool bufferInitiated;
    bool firstImageReady;
    int storedframes;
    
    // Kinect parameters
    ofxKinect               kinect;
    unsigned int width, height; // Width and height of frames
    int minX, maxX, ROIwidth; // ROI definition
    int minY, maxY, ROIheight;
    
    // General buffers
    ofxCvColorImage         kinectColorImage;
    ofShortPixels     kinectDepthImage;
    ofFloatPixels filteredframe;
    ofVec2f* gradField;
    
    // Filtering buffers
	float* averagingBuffer; // Buffer to calculate running averages of each pixel's depth value
	float* statBuffer; // Buffer retaining the running means and variances of each pixel's depth value
	float* validBuffer; // Buffer holding the most recent stable depth value for each pixel
    
    // Gradient computation variables
    int gradFieldcols, gradFieldrows;
    int gradFieldresolution;           //Resolution of grid relative to window width and height in pixels
    float maxgradfield, depthrange;
    
    // Frame filter parameters
	int numAveragingSlots; // Number of slots in each pixel's averaging buffer
	int averagingSlotIndex; // Index of averaging slot in which to store the next frame's depth values
	unsigned int minNumSamples; // Minimum number of valid samples needed to consider a pixel stable
	float maxVariance; // Maximum variance to consider a pixel stable
    float unvalidValue;
	float hysteresis; // Amount by which a new filtered value has to differ from the current value to update the display
    float bigChange; // Amount of change over which the averaging slot is reset to new value
	float instableValue; // Value to assign to instable pixels if retainValids is false
	bool spatialFilter; // Flag whether to apply a spatial filter to time-averaged depth values
    float maxOffset;
    
    int minInitFrame; // Minimal number of frame to consider the kinect initialized
    int currentInitFrame;
};
