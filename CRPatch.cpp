/* 
// Author: Juergen Gall, BIWI, ETH Zurich
// Email: gall@vision.ee.ethz.ch
*/

#include "CRPatch.h"
#include <highgui.h>
#ifdef CR_PROGRESS
#include "timer.h"
#endif

#include <deque>

#define _copysign copysign

namespace gall {

using namespace std;

void CRPatch::extractPatches(IplImage *img, unsigned int n, int label, CvRect const * const box, std::vector<CvPoint>* vCenter)
{
    if (width > img->width  ||  height > img->height)
        return;

	// extract features
	vector<IplImage*> vImg;
	extractFeatureChannels(img, vImg);
    extractPatches(vImg, n, label, box, vCenter);

	for(unsigned int c=0; c<vImg.size(); ++c)
		cvReleaseImage(&vImg[c]);
}

void CRPatch::extractPatches(vector<IplImage*> const &vImg, unsigned int n, int label, CvRect const * const box, std::vector<CvPoint>* vCenter)
{
	CvMat tmp;
	int offx = width/2; 
	int offy = height/2;

    assert(vImg.size() != 0);

	// generate x,y locations
	CvMat* locations = cvCreateMat( n, 1, CV_32SC2 );
	if(box==0)
		cvRandArr( &rng, locations, CV_RAND_UNI, cvScalar(0,0,0,0), cvScalar(vImg[0]->width-width,vImg[0]->height-height,0,0) );
	else
		cvRandArr( &rng, locations, CV_RAND_UNI, cvScalar(box->x,box->y,0,0), cvScalar(box->x+box->width-width,box->y+box->height-height,0,0) );

	// reserve memory
	size_t offset = vLPatches[label].size();
	vLPatches[label].reserve(offset+n);

    if (offset/n > std::numeric_limits<int>::max())
        throw std::runtime_error("Too many images at " + std::string(__FILE__) + " (" + std::to_string(__LINE__) + ')');

    if (n > (unsigned)std::numeric_limits<int>::max())
        throw std::runtime_error("Too many samples at " + std::string(__FILE__) + " (" + std::to_string(__LINE__) + ')');

	for(int i=0; i<int(n); ++i) {
		CvPoint pt = *(CvPoint*)cvPtr1D( locations, i, 0 );
		
		vLPatches[label].emplace_back(int(offset/n));
		PatchFeature &pf = vLPatches[label].back();

		pf.roi.x = pt.x;
        pf.roi.y = pt.y;
		pf.roi.width = width;
        pf.roi.height = height; 

		if(vCenter!=0) {
			pf.center.resize(vCenter->size());
			for(unsigned int c = 0; c<vCenter->size(); ++c) {
				pf.center[c].x = pt.x + offx - (*vCenter)[c].x;
				pf.center[c].y = pt.y + offy - (*vCenter)[c].y;
			}
		}

		pf.vPatch.resize(vImg.size());
		for(unsigned int c=0; c<vImg.size(); ++c) {
			cvGetSubRect( vImg[c], &tmp,  pf.roi );
			pf.vPatch[c] = cvCloneMat(&tmp);
		}
	}

	cvReleaseMat(&locations);
}

// inspired by https://github.com/rahul411/GLCM-parameters-using-Opencv-Library/blob/master/glcm_parameters.cpp
cv::Mat GLCM(cv::Mat img)
{
    int rows = img.rows;
    int cols = img.cols;
    cv::Mat glcm = cv::Mat::zeros(256,256,CV_32FC1);
  
    if (img.channels() == 3)
        cvtColor(img, img, cv::COLOR_BGR2GRAY);

    for (int j=0; j<rows; j++)
    {
        for (int i=0; i<cols-1; i++)
            glcm.at<float>(img.at<uchar>(j,i),img.at<uchar>(j,i+1)) = 1.f + glcm.at<float>(img.at<uchar>(j,i),img.at<uchar>(j,i+1));
    }

    // normalize glcm matrix
    glcm = glcm + glcm.t();            
    glcm = glcm / sum(glcm)[0];
 
    return glcm;
}

// inspired by https://github.com/rahul411/GLCM-parameters-using-Opencv-Library/blob/master/glcm_parameters.cpp
float const GLCM_contrast(cv::Mat const &img)
{
    cv::Mat glcm = GLCM(img);
  
    float contrast = 0.0f;
    for (int j=0; j<glcm.rows; j++)
    {
        for (int i=0; i<glcm.cols-1; i++)
            contrast += (j-i) * (j-i) * glcm.at<float>(j,i);
    }
 
    return contrast / float(glcm.cols * glcm.rows);
}


// re-implementation of CRPatch::extractPatches() that selects positive
// and negative patches based on the GLCM contrast
// low contrast patches are added to the negative training set and the search
// continues for high (less-low) contrast patches that
void CRPatch::extract_patches_of_texture(
    cv::Mat                const &image,
    std::vector<IplImage*> const &vImg,
    unsigned int                  n,
    CvRect const         * const  box,
    std::vector<CvPoint>         *vCenter)
{
	CvMat tmp;
	int offx = width/2; 
	int offy = height/2;

    assert(vImg.size() != 0);

	// generate x,y locations
    int const rnds = n * 4;
	CvMat *locations = cvCreateMat( rnds, 1, CV_32SC2 );

    const int LABEL_POSITIVE = 1;
    const int LABEL_NEGATIVE = 0;

	// reserve memory
	size_t offset = vLPatches[LABEL_POSITIVE].size();
	vLPatches[LABEL_POSITIVE].reserve(offset+n);

    if (offset/n > std::numeric_limits<int>::max())
        throw std::runtime_error("Too many images at " + std::string(__FILE__) + " (" + std::to_string(__LINE__) + ')');

    if (n > (unsigned)std::numeric_limits<int>::max())
        throw std::runtime_error("Too many samples at " + std::string(__FILE__) + " (" + std::to_string(__LINE__) + ')');

    cv::Mat visualise = image.clone();

    size_t count=0;
	while (count < n)
    {
	    if (box==0)
		    cvRandArr(&rng, locations, CV_RAND_UNI, cvScalar(0,0,0,0), cvScalar(vImg[0]->width-width,vImg[0]->height-height,0,0));
	    else
		    cvRandArr(&rng, locations, CV_RAND_UNI, cvScalar(box->x,box->y,0,0), cvScalar(box->x+box->width-width,box->y+box->height-height,0,0));

        int rnd = 0;
	    while (count<n  &&  rnd < rnds)
        {
		    CvPoint pt = *(CvPoint*)cvPtr1D( locations, rnd++, 0 );

            int label;
            cv::Mat patch(image(cv::Rect(pt, cv::Size(width,height))));
            auto const contrast = GLCM_contrast(patch);
            if (contrast > .015f)
            {
                label = LABEL_POSITIVE;
                ++count;
            }
            else
                label = LABEL_NEGATIVE;

            vLPatches[label].emplace_back(int(offset/n));
		    PatchFeature &pf = vLPatches[label].back();

		    pf.roi.x = pt.x;
            pf.roi.y = pt.y;
		    pf.roi.width = width;
            pf.roi.height = height; 

		    if (label == LABEL_POSITIVE)
            {
			    pf.center.resize(vCenter->size());
			    for (unsigned int c = 0; c<vCenter->size(); ++c)
                {
				    pf.center[c].x = pt.x + offx - (*vCenter)[c].x;
				    pf.center[c].y = pt.y + offy - (*vCenter)[c].y;
			    }
		    }
            rectangle(visualise, pf.roi, (label == LABEL_POSITIVE)? CV_RGB(0,255,0) : CV_RGB(255,0,0));

		    pf.vPatch.resize(vImg.size());
		    for(unsigned int c=0; c<vImg.size(); ++c)
            {
			    cvGetSubRect(vImg[c], &tmp,  pf.roi);
			    pf.vPatch[c] = cvCloneMat(&tmp);
		    }
	    }
    }

	cvReleaseMat(&locations);
}

void CRPatch::extractFeatureChannels(IplImage *img, std::vector<IplImage*>& vImg) {

#ifdef CR_PROGRESS
    cdmh::timer t("CRPatch::extractFeatureChannels()");
#endif

	// 32 feature channels
	// 7+9 channels: L, a, b, |I_x|, |I_y|, |I_xx|, |I_yy|, HOGlike features with 9 bins (weighted orientations 5x5 neighborhood)
	// 16+16 channels: minfilter + maxfilter on 5x5 neighborhood 

	vImg.resize(32);
	for(unsigned int c=0; c<vImg.size(); ++c)
		vImg[c] = cvCreateImage(cvSize(img->width,img->height), IPL_DEPTH_8U , 1); 

	// Get intensity
	cvCvtColor( img, vImg[0], CV_RGB2GRAY );

	// Temporary images for computing I_x, I_y (Avoid overflow for cvSobel)
	IplImage* I_x = cvCreateImage(cvSize(img->width,img->height), IPL_DEPTH_16S, 1); 
	IplImage* I_y = cvCreateImage(cvSize(img->width,img->height), IPL_DEPTH_16S, 1); 
	
	// |I_x|, |I_y|
	cvSobel(vImg[0],I_x,1,0,3);			

	cvSobel(vImg[0],I_y,0,1,3);			

	cvConvertScaleAbs( I_x, vImg[3], 0.25);

	cvConvertScaleAbs( I_y, vImg[4], 0.25);
	
	{
	  short* dataX;
	  short* dataY;
	  uchar* dataZ;
	  int stepX, stepY, stepZ;
	  CvSize size;
	  int x, y;

	  cvGetRawData( I_x, (uchar**)&dataX, &stepX, &size);
	  cvGetRawData( I_y, (uchar**)&dataY, &stepY);
	  cvGetRawData( vImg[1], (uchar**)&dataZ, &stepZ);
	  stepX /= sizeof(dataX[0]);
	  stepY /= sizeof(dataY[0]);
	  stepZ /= sizeof(dataZ[0]);
	  
	  // Orientation of gradients
	  for( y = 0; y < size.height; y++, dataX += stepX, dataY += stepY, dataZ += stepZ  )
	    for( x = 0; x < size.width; x++ ) {
	      // Avoid division by zero
	      float tx = (float)dataX[x] + (float)_copysign(0.000001f, (float)dataX[x]);
	      // Scaling [-pi/2 pi/2] -> [0 80*pi]
	      dataZ[x]=uchar( ( atan((float)dataY[x]/tx)+3.14159265f/2.0f ) * 80 ); 
	    }
	}
	
	{
	  short* dataX;
	  short* dataY;
	  uchar* dataZ;
	  int stepX, stepY, stepZ;
	  CvSize size;
	  int x, y;
	  
	  cvGetRawData( I_x, (uchar**)&dataX, &stepX, &size);
	  cvGetRawData( I_y, (uchar**)&dataY, &stepY);
	  cvGetRawData( vImg[2], (uchar**)&dataZ, &stepZ);
	  stepX /= sizeof(dataX[0]);
	  stepY /= sizeof(dataY[0]);
	  stepZ /= sizeof(dataZ[0]);
	  
	  // Magnitude of gradients
	  for( y = 0; y < size.height; y++, dataX += stepX, dataY += stepY, dataZ += stepZ  )
	    for( x = 0; x < size.width; x++ ) {
	      dataZ[x] = (uchar)( hypot(dataX[x], dataY[x]) );
	    }
	}

	// 9-bin HOG feature stored at vImg[7] - vImg[15] 
	hog.extractOBin(vImg[1], vImg[2], vImg, 7);
	
	// |I_xx|, |I_yy|

	cvSobel(vImg[0],I_x,2,0,3);
	cvConvertScaleAbs( I_x, vImg[5], 0.25);	
	
	cvSobel(vImg[0],I_y,0,2,3);
	cvConvertScaleAbs( I_y, vImg[6], 0.25);
	
	// L, a, b
	cvCvtColor( img, img, CV_RGB2Lab  );

	cvReleaseImage(&I_x);
	cvReleaseImage(&I_y);	
	
	cvSplit( img, vImg[0], vImg[1], vImg[2], 0);

	// min filter
	for(int c=0; c<16; ++c)
		minfilt(vImg[c], vImg[c+16], 5);

	//max filter
	for(int c=0; c<16; ++c)
		maxfilt(vImg[c], 5);


	
#if 0
	// for debugging only
	char buffer[40];
	for(unsigned int i = 0; i<vImg.size();++i) {
		sprintf_s(buffer,"out-%d.png",i);
		cvNamedWindow(buffer,1);
		cvShowImage(buffer, vImg[i]);
		//cvSaveImage( buffer, vImg[i] );
	}

	cvWaitKey();

	for(unsigned int i = 0; i<vImg.size();++i) {
		sprintf_s(buffer,"%d",i);
		cvDestroyWindow(buffer);
	}
#endif
}

void CRPatch::maxfilt(IplImage *src, unsigned int width) {

	uchar* s_data;
	int step;
	CvSize size;

	cvGetRawData( src, (uchar**)&s_data, &step, &size );
	step /= sizeof(s_data[0]);

	for(int  y = 0; y < size.height; y++) {
		maxfilt(s_data+y*step, 1, size.width, width);
	}

	cvGetRawData( src, (uchar**)&s_data);

	for(int  x = 0; x < size.width; x++)
		maxfilt(s_data+x, step, size.height, width);

}

void CRPatch::maxfilt(IplImage *src, IplImage *dst, unsigned int width) {

	uchar* s_data;
	uchar* d_data;
	int step;
	CvSize size;

	cvGetRawData( src, (uchar**)&s_data, &step, &size );
	cvGetRawData( dst, (uchar**)&d_data, &step, &size );
	step /= sizeof(s_data[0]);

	for(int  y = 0; y < size.height; y++)
		maxfilt(s_data+y*step, d_data+y*step, 1, size.width, width);

	cvGetRawData( src, (uchar**)&d_data);

	for(int  x = 0; x < size.width; x++)
		maxfilt(d_data+x, step, size.height, width);

}

void CRPatch::minfilt(IplImage *src, unsigned int width) {

	uchar* s_data;
	int step;
	CvSize size;

	cvGetRawData( src, (uchar**)&s_data, &step, &size );
	step /= sizeof(s_data[0]);

	for(int  y = 0; y < size.height; y++)
		minfilt(s_data+y*step, 1, size.width, width);

	cvGetRawData( src, (uchar**)&s_data);

	for(int  x = 0; x < size.width; x++)
		minfilt(s_data+x, step, size.height, width);

}

void CRPatch::minfilt(IplImage *src, IplImage *dst, unsigned int width) {

	uchar* s_data;
	uchar* d_data;
	int step;
	CvSize size;

	cvGetRawData( src, (uchar**)&s_data, &step, &size );
	cvGetRawData( dst, (uchar**)&d_data, &step, &size );
	step /= sizeof(s_data[0]);

	for(int  y = 0; y < size.height; y++)
		minfilt(s_data+y*step, d_data+y*step, 1, size.width, width);

	cvGetRawData( src, (uchar**)&d_data);

	for(int  x = 0; x < size.width; x++)
		minfilt(d_data+x, step, size.height, width);

}


void CRPatch::maxfilt(uchar* data, uchar* maxvalues, unsigned int step, unsigned int size, unsigned int width) {

	unsigned int d = int((width+1)/2)*step; 
	size *= step;
	width *= step;

	maxvalues[0] = data[0];
	for(unsigned int i=0; i < d-step; i+=step) {
		for(unsigned int k=i; k<d+i; k+=step) {
			if(data[k]>maxvalues[i]) maxvalues[i] = data[k];
		}
		maxvalues[i+step] = maxvalues[i];
	}

	maxvalues[size-step] = data[size-step];
	for(unsigned int i=size-step; i > size-d; i-=step) {
		for(unsigned int k=i; k>i-d; k-=step) {
			if(data[k]>maxvalues[i]) maxvalues[i] = data[k];
		}
		maxvalues[i-step] = maxvalues[i];
	}

    deque<int> maxfifo;
    for(unsigned int i = step; i < size; i+=step) {
		if(i >= width) {
			maxvalues[i-d] = data[maxfifo.size()>0 ? maxfifo.front(): i-step];
		}
    
		if(data[i] < data[i-step]) { 

			maxfifo.push_back(i-step);
			if(i==  width+maxfifo.front()) 
				maxfifo.pop_front();

		} else {

			while(maxfifo.size() > 0) {
				if(data[i] <= data[maxfifo.back()]) {
					if(i==  width+maxfifo.front()) 
						maxfifo.pop_front();
				break;
				}
				maxfifo.pop_back();
			}

		}

    }  

    maxvalues[size-d] = data[maxfifo.size()>0 ? maxfifo.front():size-step];
 
}

void CRPatch::maxfilt(uchar* data, unsigned int step, unsigned int size, unsigned int width) {

	unsigned int d = int((width+1)/2)*step; 
	size *= step;
	width *= step;

	deque<uchar> tmp;

	tmp.push_back(data[0]);
	for(unsigned int k=step; k<d; k+=step) {
		if(data[k]>tmp.back()) tmp.back() = data[k];
	}

	for(unsigned int i=step; i < d-step; i+=step) {
		tmp.push_back(tmp.back());
		if(data[i+d-step]>tmp.back()) tmp.back() = data[i+d-step];
	}


    deque<int> minfifo;
    for(unsigned int i = step; i < size; i+=step) {
		if(i >= width) {
			tmp.push_back(data[minfifo.size()>0 ? minfifo.front(): i-step]);
			data[i-width] = tmp.front();
			tmp.pop_front();
		}
    
		if(data[i] < data[i-step]) { 

			minfifo.push_back(i-step);
			if(i==  width+minfifo.front()) 
				minfifo.pop_front();

		} else {

			while(minfifo.size() > 0) {
				if(data[i] <= data[minfifo.back()]) {
					if(i==  width+minfifo.front()) 
						minfifo.pop_front();
				break;
				}
				minfifo.pop_back();
			}

		}

    }  

	tmp.push_back(data[minfifo.size()>0 ? minfifo.front():size-step]);
	
	for(unsigned int k=size-step-step; k>=size-d; k-=step) {
		if(data[k]>data[size-step]) data[size-step] = data[k];
	}

	for(unsigned int i=size-step-step; i >= size-d; i-=step) {
		data[i] = data[i+step];
		if(data[i-d+step]>data[i]) data[i] = data[i-d+step];
	}

	for(unsigned int i=size-width; i<=size-d; i+=step) {
		data[i] = tmp.front();
		tmp.pop_front();
	}
 
}

void CRPatch::minfilt(uchar* data, uchar* minvalues, unsigned int step, unsigned int size, unsigned int width) {

	unsigned int d = int((width+1)/2)*step; 
	size *= step;
	width *= step;

	minvalues[0] = data[0];
	for(unsigned int i=0; i < d-step; i+=step) {
		for(unsigned int k=i; k<d+i; k+=step) {
			if(data[k]<minvalues[i]) minvalues[i] = data[k];
		}
		minvalues[i+step] = minvalues[i];
	}

	minvalues[size-step] = data[size-step];
	for(unsigned int i=size-step; i > size-d; i-=step) {
		for(unsigned int k=i; k>i-d; k-=step) {
			if(data[k]<minvalues[i]) minvalues[i] = data[k];
		}
		minvalues[i-step] = minvalues[i];
	}

    deque<int> minfifo;
    for(unsigned int i = step; i < size; i+=step) {
		if(i >= width) {
			minvalues[i-d] = data[minfifo.size()>0 ? minfifo.front(): i-step];
		}
    
		if(data[i] > data[i-step]) { 

			minfifo.push_back(i-step);
			if(i==  width+minfifo.front()) 
				minfifo.pop_front();

		} else {

			while(minfifo.size() > 0) {
				if(data[i] >= data[minfifo.back()]) {
					if(i==  width+minfifo.front()) 
						minfifo.pop_front();
				break;
				}
				minfifo.pop_back();
			}

		}

    }  

    minvalues[size-d] = data[minfifo.size()>0 ? minfifo.front():size-step];
 
}

void CRPatch::minfilt(uchar* data, unsigned int step, unsigned int size, unsigned int width) {

	unsigned int d = int((width+1)/2)*step; 
	size *= step;
	width *= step;

	deque<uchar> tmp;

	tmp.push_back(data[0]);
	for(unsigned int k=step; k<d; k+=step) {
		if(data[k]<tmp.back()) tmp.back() = data[k];
	}

	for(unsigned int i=step; i < d-step; i+=step) {
		tmp.push_back(tmp.back());
		if(data[i+d-step]<tmp.back()) tmp.back() = data[i+d-step];
	}


    deque<int> minfifo;
    for(unsigned int i = step; i < size; i+=step) {
		if(i >= width) {
			tmp.push_back(data[minfifo.size()>0 ? minfifo.front(): i-step]);
			data[i-width] = tmp.front();
			tmp.pop_front();
		}
    
		if(data[i] > data[i-step]) { 

			minfifo.push_back(i-step);
			if(i==  width+minfifo.front()) 
				minfifo.pop_front();

		} else {

			while(minfifo.size() > 0) {
				if(data[i] >= data[minfifo.back()]) {
					if(i==  width+minfifo.front()) 
						minfifo.pop_front();
				break;
				}
				minfifo.pop_back();
			}

		}

    }  

	tmp.push_back(data[minfifo.size()>0 ? minfifo.front():size-step]);
	
	for(unsigned int k=size-step-step; k>=size-d; k-=step) {
		if(data[k]<data[size-step]) data[size-step] = data[k];
	}

	for(unsigned int i=size-step-step; i >= size-d; i-=step) {
		data[i] = data[i+step];
		if(data[i-d+step]<data[i]) data[i] = data[i-d+step];
	}
 
	for(unsigned int i=size-width; i<=size-d; i+=step) {
		data[i] = tmp.front();
		tmp.pop_front();
	}
}

void CRPatch::maxminfilt(uchar* data, uchar* maxvalues, uchar* minvalues, unsigned int step, unsigned int size, unsigned int width) {

	unsigned int d = int((width+1)/2)*step; 
	size *= step;
	width *= step;

	maxvalues[0] = data[0];
	minvalues[0] = data[0];
	for(unsigned int i=0; i < d-step; i+=step) {
		for(unsigned int k=i; k<d+i; k+=step) {
			if(data[k]>maxvalues[i]) maxvalues[i] = data[k];
			if(data[k]<minvalues[i]) minvalues[i] = data[k];
		}
		maxvalues[i+step] = maxvalues[i];
		minvalues[i+step] = minvalues[i];
	}

	maxvalues[size-step] = data[size-step];
	minvalues[size-step] = data[size-step];
	for(unsigned int i=size-step; i > size-d; i-=step) {
		for(unsigned int k=i; k>i-d; k-=step) {
			if(data[k]>maxvalues[i]) maxvalues[i] = data[k];
			if(data[k]<minvalues[i]) minvalues[i] = data[k];
		}
		maxvalues[i-step] = maxvalues[i];
		minvalues[i-step] = minvalues[i];
	}

    deque<int> maxfifo, minfifo;

    for(unsigned int i = step; i < size; i+=step) {
		if(i >= width) {
			maxvalues[i-d] = data[maxfifo.size()>0 ? maxfifo.front(): i-step];
			minvalues[i-d] = data[minfifo.size()>0 ? minfifo.front(): i-step];
		}
    
		if(data[i] > data[i-step]) { 

			minfifo.push_back(i-step);
			if(i==  width+minfifo.front()) 
				minfifo.pop_front();
			while(maxfifo.size() > 0) {
				if(data[i] <= data[maxfifo.back()]) {
					if (i==  width+maxfifo.front()) 
						maxfifo.pop_front();
					break;
				}
				maxfifo.pop_back();
			}

		} else {

			maxfifo.push_back(i-step);
			if (i==  width+maxfifo.front()) 
				maxfifo.pop_front();
			while(minfifo.size() > 0) {
				if(data[i] >= data[minfifo.back()]) {
					if(i==  width+minfifo.front()) 
						minfifo.pop_front();
				break;
				}
				minfifo.pop_back();
			}

		}

    }  

    maxvalues[size-d] = data[maxfifo.size()>0 ? maxfifo.front():size-step];
	minvalues[size-d] = data[minfifo.size()>0 ? minfifo.front():size-step];
 
}

}   // namespace gall
