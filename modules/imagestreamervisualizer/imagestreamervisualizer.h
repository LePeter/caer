#ifndef IMAGESTREAMERVISUALIZER_H_
#define IMAGESTREAMERVISUALIZER_H_

#include "main.h"

#include <libcaer/events/polarity.h>
#include <libcaer/events/frame.h>

#define IMAGESTREAMERVISUALIZER_SCREEN_WIDTH 400 
#define IMAGESTREAMERVISUALIZER_SCREEN_HEIGHT 400 

#define PIXEL_ZOOM 1 
#define DIRECTORY_IMG "/home/ubuntu/caffe/examples/images/retina/"
#define SIZE_IMG_W 64 
#define SIZE_IMG_H 64 

void caerImagestreamerVisualizer(uint16_t moduleID, caerPolarityEventPacket polarity);

#endif /* IMAGESTREAMERVISUALIZER_H_ */
