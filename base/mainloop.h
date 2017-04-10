/*
 * mainloop.h
 *
 *  Created on: Dec 9, 2013
 *      Author: chtekk
 */

#ifndef MAINLOOP_H_
#define MAINLOOP_H_

#include "main.h"
#include "module.h"

#ifdef __cplusplus
extern "C" {
#endif

void caerMainloopRun(void);
caerModuleData caerMainloopFindModule(uint16_t moduleID, const char *moduleShortName, enum caer_module_type type);
void caerMainloopFreeAfterLoop(void (*func)(void *mem), void *memPtr);
void caerMainloopDataNotifyIncrease(void *p);
void caerMainloopDataNotifyDecrease(void *p);
sshsNode caerMainloopGetSourceNode(uint16_t sourceID);
sshsNode caerMainloopGetSourceInfo(uint16_t sourceID);
void *caerMainloopGetSourceState(uint16_t sourceID);
void caerMainloopResetInputs(uint16_t sourceID);
void caerMainloopResetOutputs(uint16_t sourceID);
void caerMainloopResetProcessors(uint16_t sourceID);

#ifdef __cplusplus
}
#endif

#endif /* MAINLOOP_H_ */
