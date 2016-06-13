/* C wrapper to caffe interface
 *  Author: federico.corradi@inilabs.com
 */
#include "classify.hpp"
#include "wrapper.h"

extern "C" {

MyClass* newMyClass() {
	return new MyClass();
}

void MyClass_file_set(MyClass* v, char * i, double *b, double thr, bool printoutputs, caerFrameEvent single_frame) {
	v->file_set(i, b, thr, printoutputs, single_frame);
}

char * MyClass_file_get(MyClass* v) {
	return v->file_get();
}

void MyClass_init_network(MyClass *v) {
	return v->init_network();
}

void deleteMyClass(MyClass* v) {
	delete v;
}

}
