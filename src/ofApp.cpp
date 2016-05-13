#include "ofApp.h"

using namespace ofxCv;
using namespace cv;
using namespace ofxCSG;

//--------------------------------------------------------------
void ofApp::setup(){
    
    // OF basics
    ofSetFrameRate(60);
    ofBackground(0);
	ofSetVerticalSync(true);
    ofSetLogLevel(OF_LOG_VERBOSE);
    ofSetLogLevel("ofThread", OF_LOG_WARNING);
    
	// settings and defaults
	generalState = GENERAL_STATE_CALIBRATION;
	calibrationState  = CALIBRATION_STATE_PROJ_KINECT_CALIBRATION;
    ROICalibrationState = ROI_CALIBRATION_STATE_INIT;
    saved = false;
    loaded = false;
    calibrated = false;
    
    // kinectgrabber: start
	kinectgrabber.setup(generalState, calibrationState);

    // Get projector and kinect width & height
    ofVec2f kinSize = kinectgrabber.getKinectSize();
    kinectResX = kinSize.x;
    kinectResY = kinSize.y;
	projResX =projWindow->getWidth();
	projResY =projWindow->getHeight();
	kinectROI = ofRectangle(0, 0, kinectResX, kinectResY);
	
	// Setup framefilter variables
    depthNorm = 2000; // Kinect raw depth values normalization coef (to bring it in 0..1 range
    elevationMin=950/depthNorm;
	elevationMax=750/depthNorm;
//	int numAveragingSlots=30;
//	unsigned int minNumSamples=10;
//	unsigned int maxVariance=2/(4000*4000);
//	float hysteresis=0.1f/4000;
//	bool spatialFilter=false;
	gradFieldresolution = 20;
    
    // calibration config
	chessboardSize = 300;
	chessboardX = 5;
    chessboardY = 4;
	
    // Setup sandbox boundaries, base plane and kinect clip planes
	basePlaneNormal = ofVec3f(0,0,1);
	basePlaneOffset= ofVec3f(0,0,870);
	nearclip = 500;
	farclip = 1500;
		
	// Load colormap and set heightmap
    heightMap.load("HeightColorMap.yml");
    
    // Setup elevation ranges and base plane equation
    setRangesAndBasePlaneEquation();
    
	// Load shaders
    bool loaded = true;
#ifdef TARGET_OPENGLES
    cout << "Loading shadersES2"<< endl;
	loaded = loaded && elevationShader.load("shadersES2/elevationShader");
	loaded = loaded && heightMapShader.load("shadersES2/heightMapShader");
#else
	if(ofIsGLProgrammableRenderer()){
        cout << "Loading shadersGL3/elevationShader"<< endl;
		loaded = loaded && elevationShader.load("shadersGL3/elevationShader");
        cout << "Loading shadersGL3/heightMapShader"<< endl;
		loaded = loaded && heightMapShader.load("shadersGL3/heightMapShader");
	}else{
        cout << "Loading shadersGL2/elevationShader"<< endl;
		loaded = loaded && elevationShader.load("shadersGL2/elevationShader");
        cout << "Loading shadersGL2/heightMapShader"<< endl;
		loaded = loaded && heightMapShader.load("shadersGL2/heightMapShader");
	}
#endif
    if (!loaded)
    {
        cout << "shader not loaded" << endl;
        exit();
    }
    
	// Initialize the fbos
    fboProjWindow.allocate(projResX, projResY, GL_RGBA);
    contourLineFramebufferObject.allocate(projResX+1, projResY+1, GL_RGBA);
    FilteredDepthImage.allocate(kinectResX, kinectResY);
    FilteredDepthImage.setNativeScale(0, 1);//depthNorm);
    kinectColorImage.allocate(kinectResX, kinectResY);

    Dptimg.allocate(20, 20);

    // Sandbox drawing variables
    drawContourLines = true; // Flag if topographic contour lines are enabled
	contourLineFactor = 0.1f; // Inverse elevation distance between adjacent topographic contour lines
    
// Initialise mesh
    float planeScale = 1;
	meshwidth = kinectResX;
	meshheight = kinectResY;
    mesh.clear();
 	for(unsigned int y=0;y<meshheight;y++)
		for(unsigned int x=0;x<meshwidth;x++)
        {
            ofPoint pt = ofPoint(x*kinectResX*planeScale/(meshwidth-1),y*kinectResY*planeScale/(meshheight-1),0.0f)-kinectROI.getCenter()*planeScale+kinectROI.getCenter()-ofPoint(0.5,0.5,0); // We move of a half pixel to center the color pixel (more beautiful)
            mesh.addVertex(pt); // make a new vertex
            mesh.addTexCoord(pt);
        }
    for(unsigned int y=0;y<meshheight-1;y++)
		for(unsigned int x=0;x<meshwidth-1;x++)
        {
            mesh.addIndex(x+y*meshwidth);         // 0
            mesh.addIndex((x+1)+y*meshwidth);     // 1
            mesh.addIndex(x+(y+1)*meshwidth);     // 10
            
            mesh.addIndex((x+1)+y*meshwidth);     // 1
            mesh.addIndex((x+1)+(y+1)*meshwidth); // 11
            mesh.addIndex(x+(y+1)*meshwidth);     // 10
        }
//    float planeScale = 0.1;
//    int planeWidth = kinectROI.getWidth() * planeScale;
//    int planeHeight = kinectROI.getHeight() * planeScale;
//    int planeColumns = 2;//planeWidth / 1;
//    int planeRows = 2;//planeHeight / 1;
//    plane.set(planeWidth, planeHeight, planeColumns, planeRows, OF_PRIMITIVE_TRIANGLES);
//    plane.setPosition(kinectResX/2, kinectResY/2, 0); /// position in x y z
//    plane.mapTexCoordsFromTexture(FilteredDepthImage.getTexture());

	// finish kinectgrabber setup and start the grabber
    kinectgrabber.setupFramefilter(depthNorm, gradFieldresolution, nearclip, farclip, basePlaneNormal, elevationMin, elevationMax, kinectROI);
    kinectWorldMatrix = kinectgrabber.getWorldMatrix();
    cout << "kinectWorldMatrix: " << kinectWorldMatrix << endl;
    
    //Try to load calibration file if possible
    if (kpt.loadCalibration("calibration.xml"))
    {
        cout << "Calibration loaded " << endl;
        kinectProjMatrix = kpt.getProjectionMatrix();
        cout << "kinectProjMatrix: " << kinectProjMatrix << endl;
        loaded = true;
        calibrated = true;
        generalState = GENERAL_STATE_SANDBOX;
        updateMode();
    } else {
        cout << "Calibration could not be loaded " << endl;
    }
    firstImageReady = false;
	kinectgrabber.startThread(true);
}

//--------------------------------------------------------------
void ofApp::setRangesAndBasePlaneEquation(){
    //if(elevationMin<heightMap.getScalarRangeMin())
    basePlaneEq=getPlaneEquation(basePlaneOffset,basePlaneNormal); //homogeneous base plane equation
    basePlaneEq /= ofVec4f(depthNorm, depthNorm, 1, depthNorm); // Normalized coordinates for the shader (except z since it is already normalized in the Depthframe)
    
    elevationMin=-heightMap.getScalarRangeMin()/depthNorm;
    //if(elevationMax>heightMap.getScalarRangeMax())
    elevationMax=-heightMap.getScalarRangeMax()/depthNorm;
    setHeightMapRange(heightMap.getNumEntries(),elevationMin,elevationMax);
    
    cout << "basePlaneOffset" << basePlaneOffset << endl;
    cout << "basePlaneNormal" << basePlaneNormal << endl;
    cout << "basePlaneEq" << basePlaneEq << endl;
    cout << "elevationMin" << elevationMin << endl;
    cout << "elevationMax" << elevationMax << endl;
    cout << "heightMap.getNumEntries()" << heightMap.getNumEntries() << endl;
}

//--------------------------------------------------------------
void ofApp::update(){
	// Get depth image from kinect grabber
    ofFloatPixels filteredframe;
	if (kinectgrabber.filtered.tryReceive(filteredframe)) {
		FilteredDepthImage.setFromPixels(filteredframe.getData(), kinectResX, kinectResY);
		FilteredDepthImage.updateTexture();
        if (kinectgrabber.framefilter.firstImageReady)
            firstImageReady = true;

        // Get color image from kinect grabber
        ofPixels coloredframe;
        if (kinectgrabber.colored.tryReceive(coloredframe)) {
            kinectColorImage.setFromPixels(coloredframe);
//            kinectColorImage.updateTexture();
        }
        
        // Update grabber stored frame number
		kinectgrabber.lock();
		kinectgrabber.storedframes -= 1;
		kinectgrabber.unlock();

        if (generalState == GENERAL_STATE_CALIBRATION) {
                if (calibrationState == CALIBRATION_STATE_CALIBRATION_TEST){
                    
                    // Get kinect depth image coord
                    ofVec2f t = ofVec2f(min((float)kinectResX-1,testPoint.x), min((float)kinectResY-1,testPoint.y));
                    ofVec3f worldPoint = ofVec3f(t);
                    worldPoint.z = kinectgrabber.kinect.getDistanceAt(t.x, t.y) / depthNorm;
                    ofVec4f wc = ofVec4f(worldPoint);
                    wc.w = 1;
                    
                    ofVec2f projectedPoint = computeTransform(wc);//kpt.getProjectedPoint(worldPoint);
                    drawTestingPoint(projectedPoint);
                }
                else if (calibrationState == CALIBRATION_STATE_PROJ_KINECT_CALIBRATION) {
                    drawChessboard(ofGetMouseX(), ofGetMouseY(), chessboardSize);
                    cvRgbImage = ofxCv::toCv(kinectColorImage.getPixels());
                    cv::Size patternSize = cv::Size(chessboardX-1, chessboardY-1);
                    int chessFlags = cv::CALIB_CB_ADAPTIVE_THRESH + cv::CALIB_CB_FAST_CHECK;
                    bool foundChessboard = findChessboardCorners(cvRgbImage, patternSize, cvPoints, chessFlags);
                    if(foundChessboard) {
                        cv::Mat gray;
                        cvtColor(cvRgbImage, gray, CV_RGB2GRAY);
                        cornerSubPix(gray, cvPoints, cv::Size(11, 11), cv::Size(-1, -1),
                                     cv::TermCriteria(CV_TERMCRIT_EPS + CV_TERMCRIT_ITER, 30, 0.1));
                        drawChessboardCorners(cvRgbImage, patternSize, cv::Mat(cvPoints), foundChessboard);
 //                       cout << "draw chess" << endl;
                    }
                }
                else if (calibrationState == CALIBRATION_STATE_ROI_DETERMINATION){
                    updateROI();
                }
        }
        else if (generalState == GENERAL_STATE_SANDBOX){
//Check values for debug
//            float maxval = -1000.0;
//            float minval = 1000.0;
//            float xf;
//            for (int i = 0; i<640*480; i ++){
//                xf = FilteredDepthImage.getFloatPixelsRef().getData()[i] - basePlaneOffset.z;
//
//                if (xf > maxval)
//                    maxval = xf;
//                if (xf < minval)
//                    minval = xf;
//            }
//            cout << "FilteredDepthImage - baseplane offset maxval : " << maxval << " FilteredDepthImage - baseplane offset minval : " << minval << endl;
            
            // Get kinect depth image coord
            ofVec2f t = kinectROI.getCenter();
            ofVec3f worldPoint = ofVec3f(t);
            worldPoint.z = FilteredDepthImage.getFloatPixelsRef().getData()[(int)t.x+kinectResX*(int)t.y];
            ofVec4f wc = ofVec4f(worldPoint);
            wc.w = 1;
            
            ofVec2f projectedPoint = computeTransform(wc);//kpt.getProjectedPoint(worldPoint);
            drawTestingPoint(projectedPoint);

            drawSandbox();
        }
    }
}

//--------------------------------------------------------------
void ofApp::draw(){
    if (generalState == GENERAL_STATE_CALIBRATION) {
        kinectColorImage.draw(0, 0, 640, 480);
        FilteredDepthImage.draw(650, 0, 320, 240);
        
        ofSetColor(0);
        if (calibrationState == CALIBRATION_STATE_CALIBRATION_TEST){
            ofDrawBitmapStringHighlight("Click on the image to test a point in the RGB image.", 340, 510);
            ofDrawBitmapStringHighlight("The projector should place a green dot on the corresponding point.", 340, 530);
            ofDrawBitmapStringHighlight("Press the 's' key to save the calibration.", 340, 550);
            if (saved) {
                ofDrawBitmapStringHighlight("Calibration saved.", 340, 590);
            }
            ofSetColor(255, 0, 0);
            
            //Draw testing point indicator
            float ptSize = ofMap(cos(ofGetFrameNum()*0.1), -1, 1, 3, 40);
            ofDrawCircle(testPoint.x, testPoint.y, ptSize);
            
        } else if (calibrationState == CALIBRATION_STATE_PROJ_KINECT_CALIBRATION || calibrationState == CALIBRATION_STATE_ROI_DETERMINATION) {
            ofNoFill();
            ofSetColor(255);
            ofDrawRectangle(kinectROI);
            ofFill();
            ofDrawBitmapStringHighlight("Position the chessboard using the mouse.", 340, 510);
            ofDrawBitmapStringHighlight("Adjust the size of the chessboard using the 'q' and 'w' keys.", 340, 530);
            ofDrawBitmapStringHighlight("Press the spacebar to save a set of point pairs.", 340, 550);
            ofDrawBitmapStringHighlight("Press the 'c' key to perform calibration.", 340, 570);
            ofDrawBitmapStringHighlight("Press the 'r' key to find ROI.", 340, 590);
            ofDrawBitmapStringHighlight("Press the 't' key to switch between performing calibrating and testing calibration.", 340, 610);
            ofDrawBitmapStringHighlight("Press the 'b' key to switch between calibrating and using sandbox.", 340, 630);
            ofDrawBitmapStringHighlight(resultMessage, 340, 650);
            ofDrawBitmapStringHighlight(ofToString(pairsKinect.size())+" point pairs collected.", 340, 670);
        }
        ofSetColor(255);
    } else if (generalState == GENERAL_STATE_SANDBOX){
        kinectColorImage.draw(0, 0, 640, 480);
        //Draw testing point indicator
        float ptSize = ofMap(cos(ofGetFrameNum()*0.1), -1, 1, 3, 10);
        ofDrawCircle(testPoint.x, testPoint.y, ptSize);
        
        ofRectangle imgROI;
        imgROI.setFromCenter(testPoint, 20, 20);
        kinectColorImage.setROI(imgROI);
        kinectColorImage.drawROI(650, 10, 100, 100);
        ofDrawCircle(700, 60, ptSize);

        if (firstImageReady) {
//            FilteredDepthImage.setROI(imgROI);
            float * roi_ptr = (float*)FilteredDepthImage.getFloatPixelsRef().getData() + ((int)(imgROI.y)*kinectResX) + (int)imgROI.x;
            ofFloatPixels ROIDpt;
            ROIDpt.setFromAlignedPixels(roi_ptr,imgROI.width,imgROI.height,1,kinectResX*4);
            Dptimg.setFromPixels(ROIDpt);
            Dptimg.contrastStretch();
            Dptimg.draw(650, 120, 100, 100);
            ofDrawCircle(700, 170, ptSize);
        }
    }
}

//--------------------------------------------------------------
void ofApp::drawProjWindow(ofEventArgs &args){ // Main draw call for proj window
    ofSetColor(ofColor::white);
    fboProjWindow.draw(0, 0);
}

//--------------------------------------------------------------
void ofApp::drawChessboard(int x, int y, int chessboardSize) { // Prepare proj window fbo
    float w = chessboardSize / chessboardX;
    float h = chessboardSize / chessboardY;
    currentProjectorPoints.clear();
    fboProjWindow.begin();
    ofClear(255, 0);
    ofSetColor(0);
    ofTranslate(x, y);
    for (int j=0; j<chessboardY; j++) {
        for (int i=0; i<chessboardX; i++) {
            int x0 = ofMap(i, 0, chessboardX, 0, chessboardSize);
            int y0 = ofMap(j, 0, chessboardY, 0, chessboardSize);
            if (j>0 && i>0) {
// Not-normalized (on proj screen)
                currentProjectorPoints.push_back(ofVec2f(x+x0, y+y0));
// Normalized coordinates (between 0 and 1)
//                currentProjectorPoints.push_back(ofVec2f(
//                                                         ofMap(x+x0, 0, fboProjWindow.getWidth(), 0, 1),
//                                                         ofMap(y+y0, 0, fboProjWindow.getHeight(), 0, 1)
//                                                         ));
            }
            if ((i+j)%2==0) ofDrawRectangle(x0, y0, w, h);
        }
    }
    ofSetColor(255);
    fboProjWindow.end();
}

//--------------------------------------------------------------
void ofApp::drawTestingPoint(ofVec2f projectedPoint) { // Prepare proj window fbo
    float ptSize = ofMap(sin(ofGetFrameNum()*0.1), -1, 1, 3, 40);
    fboProjWindow.begin();
    ofBackground(255);
    ofSetColor(0, 255, 0);
// Not-normalized (on proj screen)
    ofDrawCircle(projectedPoint.x, projectedPoint.y, ptSize);
// Normalized coordinates (between 0 and 1)
//    ofDrawCircle(
//             ofMap(projectedPoint.x, 0, 1, 0, fboProjWindow.getWidth()),
//             ofMap(projectedPoint.y, 0, 1, 0, fboProjWindow.getHeight()),
//             ptSize);
    ofSetColor(255);
    fboProjWindow.end();
}

//--------------------------------------------------------------
void ofApp::drawSandbox() { // Prepare proj window fbo
//    ofPoint result = computeTransform(kinectROI.getCenter());
    fboProjWindow.begin();
//    ofClear(0,0,0,255); // Don't clear the testing point that was previously drawn
    ofSetColor(ofColor::red);
    
    FilteredDepthImage.getTexture().bind();
    heightMapShader.begin();
    
//    heightMapShader.setUniformTexture("tex1", FilteredDepthImage.getTexture(), 1 ); //"1" means that it is texture 1
    heightMapShader.setUniformMatrix4f("kinectProjMatrix",kinectProjMatrix.getTransposedOf(kinectProjMatrix));
    heightMapShader.setUniformMatrix4f("kinectWorldMatrix",kinectWorldMatrix.getTransposedOf(kinectWorldMatrix));
    heightMapShader.setUniformTexture("heightColorMapSampler",heightMap.getTexture(), 2);
    heightMapShader.setUniform2f("heightColorMapTransformation",ofVec2f(heightMapScale,heightMapOffset));
    heightMapShader.setUniform4f("basePlaneEq", basePlaneEq);

//    kinectColorImage.getTexture().bind();
    mesh.draw();
//    FilteredDepthImage.draw(0,0);
    
    heightMapShader.end();
    
    FilteredDepthImage.getTexture().unbind();
    fboProjWindow.end();

//    //    ofPoint result = computeTransform(kinectROI.getCenter());
//    
//	/* Check if contour line rendering is enabled: */
//	if(drawContourLines)
//    {
//		/* Run the first rendering pass to create a half-pixel offset texture of surface elevations: */
//        //		prepareContourLines();
//        //        contourLineFramebufferObject.allocate(800, 600);
//    }
//    
//	/* Bind the single-pass surface shader: */
//    fboProjWindow.begin();
//    ofClear(255,255,255, 0);
//
//    heightMapShader.begin();
//	
//    heightMapShader.setUniformTexture( "depthSampler", FilteredDepthImage.getTexture(), 1 ); //"1" means that it is texture 1
//    heightMapShader.setUniformMatrix4f("kinectProjMatrix",kinectProjMatrix);
//    heightMapShader.setUniformMatrix4f("kinectWorldMatrix",kinectWorldMatrix);
//    heightMapShader.setUniform4f("basePlane",basePlaneEq);
//    heightMapShader.setUniform2f("heightColorMapTransformation",ofVec2f(heightMapScale,heightMapOffset));
//    //    heightMapShader.setUniformTexture("pixelCornerElevationSampler", contourLineFramebufferObject.getTexture(), 2);
//    heightMapShader.setUniform1f("contourLineFactor",contourLineFactor);
//    heightMapShader.setUniformTexture("heightColorMapSampler",heightMap.getTexture(), 3);
//    
//	/* Draw the surface: */
//    mesh.draw();
//    heightMapShader.end();
//    fboProjWindow.end();
}

//--------------------------------------------------------------
void ofApp::prepareContourLines() // Prepare contour line fbo
{
	/*********************************************************************
     Prepare the half-pixel-offset frame buffer for subsequent per-fragment
     Marching Squares contour line extraction.
     *********************************************************************/
	
	/* Adjust the projection matrix to render the corners of the final pixels: */
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	GLdouble proj[16];
	glGetDoublev(GL_PROJECTION_MATRIX,proj);
	GLdouble xs=GLdouble(800)/GLdouble(801);
	GLdouble ys=GLdouble(600)/GLdouble(601);
	for(int j=0;j<4;++j)
    {
		proj[j*4+0]*=xs;
		proj[j*4+1]*=ys;
    }
	glLoadIdentity();
	glMultMatrixd(proj);
	
	/*********************************************************************
     Render the surface's elevation into the half-pixel offset frame
     buffer.
     *********************************************************************/
	
	/* start the elevation shader and contourLineFramebufferObject: */
    contourLineFramebufferObject.begin();
    ofClear(255,255,255, 0);

	elevationShader.begin();

    elevationShader.setUniformTexture( "depthSampler", FilteredDepthImage.getTexture(), 1 ); //"1" means that it is texture 1
    elevationShader.setUniformMatrix4f("kinectWorldMatrix",kinectWorldMatrix);
    elevationShader.setUniform4f("basePlane",basePlaneEq);
	
	/* Draw the surface: */
//    mesh.draw();
	
    elevationShader.end();
    contourLineFramebufferObject.end();
	
	/*********************************************************************
     Restore previous OpenGL state.
     *********************************************************************/
	
	/* Restore the original viewport and projection matrix: */
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
    //	glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    //
    //	/* Restore the original clear color and frame buffer binding: */
    //	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,currentFrameBuffer);
    //	glClearColor(currentClearColor[0],currentClearColor[1],currentClearColor[2],currentClearColor[3]);
}

//--------------------------------------------------------------
void ofApp::addPointPair() {
    // Add point pair based on kinect world coordinates
    cout << "Adding point pair in kinect world coordinates" << endl;
    int nDepthPoints = 0;
    for (int i=0; i<cvPoints.size(); i++) {
        ofVec3f worldPoint = kinectgrabber.kinect.getWorldCoordinateAt(cvPoints[i].x, cvPoints[i].y);
        if (worldPoint.z > 0)   nDepthPoints++;
    }
    if (nDepthPoints == (chessboardX-1)*(chessboardY-1)) {
        for (int i=0; i<cvPoints.size(); i++) {
            ofVec3f worldPoint = kinectgrabber.kinect.getWorldCoordinateAt(cvPoints[i].x, cvPoints[i].y);
            worldPoint.z = worldPoint.z/depthNorm; // Normalize raw depth coordinate
            pairsKinect.push_back(worldPoint);
            pairsProjector.push_back(currentProjectorPoints[i]);
        }
        resultMessage = "Added " + ofToString((chessboardX-1)*(chessboardY-1)) + " points pairs.";
        resultMessageColor = ofColor(0, 255, 0);
    } else {
        resultMessage = "Points not added because not all chessboard\npoints' depth known. Try re-positionining.";
        resultMessageColor = ofColor(255, 0, 0);
    }
    cout << resultMessage << endl;
    
    // Add point pair base on kinect camera coordinate (x, y in 640x480, z in calibrated units)
//    cout << "Adding point pair in kinect camera coordinates" << endl;
//    int nDepthPoints = 0;
//    for (int i=0; i<cvPoints.size(); i++) {
//        ofVec3f worldPoint = ofVec3f(cvPoints[i].x, cvPoints[i].y, kinectgrabber.kinect.getDistanceAt(cvPoints[i].x, cvPoints[i].y));
//        if (worldPoint.z > 0)   nDepthPoints++;
//    }
//    if (nDepthPoints == (chessboardX-1)*(chessboardY-1)) {
//        for (int i=0; i<cvPoints.size(); i++) {
//            ofVec3f worldPoint = ofVec3f(cvPoints[i].x, cvPoints[i].y, kinectgrabber.kinect.getDistanceAt(cvPoints[i].x, cvPoints[i].y));
//            pairsKinect.push_back(worldPoint);
//            pairsProjector.push_back(currentProjectorPoints[i]);
//        }
//        resultMessage = "Added " + ofToString((chessboardX-1)*(chessboardY-1)) + " points pairs.";
//        resultMessageColor = ofColor(0, 255, 0);
//    } else {
//        resultMessage = "Points not added because not all chessboard\npoints' depth known. Try re-positionining.";
//        resultMessageColor = ofColor(255, 0, 0);
//    }
//    cout << resultMessage << endl;
//
}

//--------------------------------------------------------------
void ofApp::setHeightMapRange(int newHeightMapSize,float newMinElevation,float newMaxElevation)
{
	/* Calculate the new height map elevation scaling and offset coefficients: */
	heightMapScale =(newHeightMapSize-1)/((newMaxElevation-newMinElevation));
	heightMapOffset =0.5/newHeightMapSize-heightMapScale*newMinElevation;
}

//--------------------------------------------------------------
ofVec2f ofApp::computeTransform(ofVec4f vin) // vin is in kinect image depth coordinate with normalized z
{
    /* Transform the vertex from depth image space to world space: */
//    ofVec3f vertexCcxx = kinectgrabber.kinect.getWorldCoordinateAt(vertexDic.x, vertexDic.y, vertexDic.z);
    ofVec4f vertexCc = kinectWorldMatrix*vin*vin.z;
    vertexCc.w = 1;
    
    /* Plug camera-space vertex into the base plane equation: */
    float elevation=basePlaneEq.dot(vertexCc);///vertexCc.w;
    
    /* Transform elevation to height color map texture coordinate: */
//    heightColorMapTexCoord=elevation*heightColorMapTransformation.x+heightColorMapTransformation.y;
    
    /* Transform vertex to clip coordinates: */
    ofVec4f screenPos = kinectProjMatrix*vertexCc;
    ofVec2f projectedPoint(screenPos.x/screenPos.z, screenPos.y/screenPos.z);
    return projectedPoint;
}


//--------------------------------------------------------------
// Find kinect ROI of the sandbox
void ofApp::updateROI(){
    if (ROICalibrationState == ROI_CALIBRATION_STATE_INIT) { // set kinect to max depth range
        if (cvPoints.size() == 0) {
            cout << "Error: No points !!" << endl;
        } else {
        cout << "Initiating kinect clip planes" << endl;
//        kinectgrabber.nearclipchannel.send(500);
//        kinectgrabber.farclipchannel.send(4000);
        ROICalibrationState = ROI_CALIBRATION_STATE_MOVE_UP;
        
        large = ofPolyline();
        threshold = 220;
        }
    } else if (ROICalibrationState == ROI_CALIBRATION_STATE_MOVE_UP) {
        while (threshold < 255){
            cout << "Increasing threshold : " << threshold << endl;
            thresholdedImage.setFromPixels(FilteredDepthImage.getPixels());
            //                            thresholdedImage.mirror(verticalMirror, horizontalMirror);
            //cvThreshold(thresholdedImage.getCvImage(), thresholdedImage.getCvImage(), highThresh+10, 255, CV_THRESH_TOZERO_INV);
            cvThreshold(thresholdedImage.getCvImage(), thresholdedImage.getCvImage(), threshold, 255, CV_THRESH_TOZERO);
            
            contourFinder.findContours(thresholdedImage, 12, 640*480, 5, true);
            //contourFinder.findContours(thresholdedImage);
            //ofPoint cent = ofPoint(projectorWidth/2, projectorHeight/2);
            
            ofPolyline small = ofPolyline();
            for (int i = 0; i < contourFinder.nBlobs; i++) {
                ofxCvBlob blobContour = contourFinder.blobs[i];
                if (blobContour.hole) {
                    //								if(!blobContour.isClosed())
                    //									blobContour.close();
                    bool ok = true;
                    ofPolyline poly = ofPolyline(blobContour.pts);//.getResampledByCount(50);
                    for (int j = 0; j < cvPoints.size(); j++){ // We only take the 12 first point to speed up process
                        if (!poly.inside(cvPoints[j].x, cvPoints[j].y)) {
                            ok = false;
                            break;
                        }
                    }
                    if (ok) {
                        cout << "We found a contour lines surroundings the chessboard" << endl;
                        if (small.size() == 0 || poly.getArea() < small.getArea()) {
                            cout << "We take the smallest contour line surroundings the chessboard at a given threshold level" << endl;
                            small = poly;
                        }
                    }
                }
            }
            if (large.getArea() < small.getArea()) {
                cout << "We take the largest contour line surroundings the chessboard at all threshold level" << endl;
                large = small;
            }
            threshold+=1;
        } //else {
        kinectROI = large.getBoundingBox();
        //                        if (horizontalMirror) {
        //                            kinectROI.x = 640 -kinectROI.x;
        //                            kinectROI.width = -kinectROI.width;
        //                        }
        //                        if (verticalMirror) {
        //                            kinectROI.y = 480 -kinectROI.y;
        //                            kinectROI.height = -kinectROI.height;
        //                        }
        kinectROI.standardize();
        cout << kinectROI << endl;
        // We are finished, set back kinect depth range and update ROI
        ROICalibrationState = ROI_CALIBRATION_STATE_DONE;
        kinectgrabber.setKinectROI(kinectROI);
//        kinectgrabber.nearclipchannel.send(nearclip);
//        kinectgrabber.farclipchannel.send(farclip);
        //}
    } else if (ROICalibrationState == ROI_CALIBRATION_STATE_DONE){
        generalState = GENERAL_STATE_CALIBRATION;
        calibrationState = CALIBRATION_STATE_CALIBRATION_TEST;
    }
}

//--------------------------------------------------------------
void ofApp::updateMode(){
    cout << "General state: " << generalState << endl;
    cout << "Calibration state: " << calibrationState << endl;
#if __cplusplus>=201103
    kinectgrabber.generalStateChannel.send(std::move(generalState));
    kinectgrabber.calibrationStateChannel.send(std::move(calibrationState));
#else
    kinectgrabber.generalStateChannel.send(generalState);
    kinectgrabber.calibrationStateChannel.send(calibrationState);
#endif
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
    if (key==' '){
        addPointPair();
    } else if (key=='a') {
        chessboardSize -= 20;
    } else if (key=='z') {
        chessboardSize += 20;
    } else if (key=='q') {
        basePlaneOffset.z += 0.5;
        setRangesAndBasePlaneEquation();
    } else if (key=='s') {
        basePlaneOffset.z -= 0.5;
        setRangesAndBasePlaneEquation();
    }else if (key=='w') {
        heightMap.scaleRange(0.5);
        setRangesAndBasePlaneEquation();
    } else if (key=='x') {
        heightMap.scaleRange(2);
        setRangesAndBasePlaneEquation();
    } else if (key=='d') {
        heightMap.changeNumEntries(50, true); // Increase the color map's size
        setRangesAndBasePlaneEquation();
    } else if (key=='f') {
        heightMap.changeNumEntries(50, false); // Decrease the color map's size
        setRangesAndBasePlaneEquation();
    } else if (key=='u') {
        basePlaneNormal.rotate(-1, ofVec3f(1,0,0)); // Rotate the base plane normal
        setRangesAndBasePlaneEquation();
    } else if (key=='i') {
        basePlaneNormal.rotate(1, ofVec3f(1,0,0)); // Rotate the base plane normal
        setRangesAndBasePlaneEquation();
    } else if (key=='o') {
        basePlaneNormal.rotate(-1, ofVec3f(0,1,0)); // Rotate the base plane normal
        setRangesAndBasePlaneEquation();
    } else if (key=='p') {
        basePlaneNormal.rotate(1, ofVec3f(0,1,0)); // Rotate the base plane normal
        setRangesAndBasePlaneEquation();
    } else if (key=='c') {
        if (pairsKinect.size() == 0) {
            cout << "Error: No points acquired !!" << endl;
        } else {
            cout << "calibrating" << endl;
            kpt.calibrate(pairsKinect, pairsProjector);
            kinectProjMatrix = kpt.getProjectionMatrix();
            saved = false;
            loaded = false;
            calibrated = true;
        }
    } else if (key=='r') {
        if (cvPoints.size() == 0) {
            cout << "Error: Chessboard not found on screen !!" << endl;
        } else {
            cout << "Finding ROI" << endl;
            generalState = GENERAL_STATE_CALIBRATION;
            calibrationState = CALIBRATION_STATE_ROI_DETERMINATION;
            ROICalibrationState = ROI_CALIBRATION_STATE_INIT;
        }
    } else if (key=='t') {
        generalState = GENERAL_STATE_CALIBRATION;
        if (calibrationState == CALIBRATION_STATE_CALIBRATION_TEST) {
                calibrationState = CALIBRATION_STATE_PROJ_KINECT_CALIBRATION;
        }    else if (calibrationState == CALIBRATION_STATE_PROJ_KINECT_CALIBRATION){
                calibrationState = CALIBRATION_STATE_CALIBRATION_TEST;
        }
    } else if (key=='b') {
        if (generalState == GENERAL_STATE_CALIBRATION) {
            generalState = GENERAL_STATE_SANDBOX;
        }
        else if (generalState == GENERAL_STATE_SANDBOX){
            generalState = GENERAL_STATE_CALIBRATION;
        }
    } else if (key=='v') {
        if (kpt.saveCalibration("calibration.xml"))
        {
            cout << "Calibration saved " << endl;
            saved = true;
        } else {
            cout << "Calibration could not be saved " << endl;
        }
    } else if (key=='l') {
        if (kpt.loadCalibration("calibration.xml"))
        {
            cout << "Calibration loaded " << endl;
            kinectProjMatrix = kpt.getProjectionMatrix();
            loaded = true;
            calibrated = true;
        } else {
            cout << "Calibration could not be loaded " << endl;
        }
    }

    if (key=='r' || key=='b' || key=='t') {
        updateMode();
    }
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){

}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y ){

}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){
    testPoint.set(min(x, kinectResX-1), min(y, kinectResY-1));
    
    int idx = (int)testPoint.x+kinectResX*(int)testPoint.y;
    cout << "Depth value at point: " << FilteredDepthImage.getFloatPixelsRef().getData()[idx]<< endl;
    float* sPtr=kinectgrabber.framefilter.statBuffer+3*idx;
    cout << " Number of valid samples statBuffer[0]: " << sPtr[0] << endl;
    cout << " Sum of valid samples statBuffer[1]: " << sPtr[1] << endl; //
    cout << " Sum of squares of valid samples statBuffer[2]: " << sPtr[2] << endl; // Sum of squares of valid samples<< endl;
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y){

}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y){

}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){

}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}

//--------------------------------------------------------------
bool ofApp::loadSettings(string path){
    ofXml xml;
    if (!xml.load(path))
        return false;
    xml.setTo("KINECTSETTINGS");
    kinectROI = xml.getValue<ofRectangle>("kinectROI");
    basePlaneNormal = xml.getValue<ofVec3f>("basePlaneNormal");
    basePlaneOffset = xml.getValue<ofVec3f>("basePlaneOffset");
    basePlaneEq = xml.getValue<ofVec4f>("basePlaneEq");

    return true;
}

//--------------------------------------------------------------
bool ofApp::saveSettings(string path){
    ofXml xml;
    xml.addChild("KINECTSETTINGS");
    xml.setTo("KINECTSETTINGS");
    xml.addValue("kinectROI", kinectROI);
    xml.addValue("basePlaneNormal", basePlaneNormal);
    xml.addValue("basePlaneOffset", basePlaneOffset);
    xml.addValue("basePlaneEq", basePlaneEq);
    xml.setToParent();
    return xml.save(path);
}
