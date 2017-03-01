/* C wrapper to caffe interface
 *  Author: federico.corradi@inilabs.com
 */
#include "classify.hpp"
#include "wrapper.h"

extern "C" {

MyCaffe* newMyCaffe() {
	return new MyCaffe();
}

void MyCaffe_file_set(MyCaffe* v, int * i, int size, char *b, int *resId, double thr,
		bool printoutputs, caerFrameEvent *single_frame,
		bool showactivations, bool norminput) {
	v->file_set(i, size, b, resId, thr, printoutputs, single_frame, showactivations, norminput);
}

void MyCaffe_init_network(MyCaffe *v) {
	return v->init_network();
}

void deleteMyCaffe(MyCaffe* v) {
	delete v;
}

}
