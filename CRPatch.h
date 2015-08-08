/* 
// Author: Juergen Gall, BIWI, ETH Zurich
// Email: gall@vision.ee.ethz.ch
*/

#pragma once

#include <cxcore.h>
#include <cv.h>

#include <vector>
#include <iostream>

#include "HoG.h"

namespace gall {

// structure for image patch
struct PatchFeature {
	explicit PatchFeature(int frame, int patch) : src_index{frame, patch} {}

	CvRect roi;
	std::vector<CvPoint> center;

    // index of the src image in the full training
    std::pair<int, int> src_index;

	std::vector<CvMat*> vPatch;
	void print() const {
		std::cout << roi.x << " " << roi.y << " " << roi.width << " " << roi.height;
		for(unsigned int i=0; i<center.size(); ++i) std::cout << " " << center[i].x << " " << center[i].y; std::cout << std::endl;
	}
	void show(int delay) const;

    // not copyable because of the const index
    PatchFeature &operator=(PatchFeature const &) = delete;
};

static HoG hog; 

class CRPatch {
public:
	CRPatch(CvRNG* pRNG, int w, int h, int num_l) : cvRNG(pRNG), width(w), height(h) { vLPatches.resize(num_l);}

	// Extract patches from image
	void extractPatches(IplImage *img, unsigned int n, int label, CvRect const * const box = 0, std::vector<CvPoint>* vCenter = 0);

	// Extract features from image
	static void extractFeatureChannels(IplImage *img, std::vector<IplImage*>& vImg);

	// min/max filter
	static void maxfilt(uchar* data, uchar* maxvalues, unsigned int step, unsigned int size, unsigned int width);
	static void maxfilt(uchar* data, unsigned int step, unsigned int size, unsigned int width);
	static void minfilt(uchar* data, uchar* minvalues, unsigned int step, unsigned int size, unsigned int width);
	static void minfilt(uchar* data, unsigned int step, unsigned int size, unsigned int width);
	static void maxminfilt(uchar* data, uchar* maxvalues, uchar* minvalues, unsigned int step, unsigned int size, unsigned int width);
	static void maxfilt(IplImage *src, unsigned int width);
	static void maxfilt(IplImage *src, IplImage *dst, unsigned int width);
	static void minfilt(IplImage *src, unsigned int width);
	static void minfilt(IplImage *src, IplImage *dst, unsigned int width);

	std::vector<std::vector<PatchFeature> > vLPatches;

private:
    CRPatch &operator=(CRPatch const &) = delete;

private:
	int const width;
	int const height;
	CvRNG *cvRNG;
};

}   // namespace gall
