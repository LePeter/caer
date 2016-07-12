#ifndef VISUALIZER_H_
#define VISUALIZER_H_

#include "main.h"
#include "base/module.h"
#include <libcaer/events/polarity.h>
#include <libcaer/events/frame.h>
#include <libcaer/events/imu6.h>
#include <libcaer/events/point2d.h>
#include <libcaer/events/packetContainer.h>
#include <allegro5/allegro.h>

#define VISUALIZER_DEFAULT_ZOOM 2.0f
#define VISUALIZER_REFRESH_RATE 60.0f

typedef struct caer_visualizer_state *caerVisualizerState;
typedef bool (*caerVisualizerRenderer)(caerVisualizerState state, caerEventPacketContainer container);
typedef void (*caerVisualizerEventHandler)(caerVisualizerState state, ALLEGRO_EVENT event);

// For reuse inside other modules.
caerVisualizerState caerVisualizerInit(caerVisualizerRenderer renderer, caerVisualizerEventHandler eventHandler,
	int32_t bitmapSizeX, int32_t bitmapSizeY, float defaultZoomFactor, bool defaultShowStatistics,
	caerModuleData parentModule);
void caerVisualizerUpdate(caerVisualizerState state, caerEventPacketContainer container);
void caerVisualizerExit(caerVisualizerState state);

bool caerVisualizerRendererPolarityEvents(caerVisualizerState state, caerEventPacketContainer container);
bool caerVisualizerRendererFrameEvents(caerVisualizerState state, caerEventPacketContainer container);
bool caerVisualizerRendererIMU6Events(caerVisualizerState state, caerEventPacketContainer container);
bool caerVisualizerRendererPoint2DEvents(caerVisualizerState state, caerEventPacketContainer container);

void caerVisualizer(uint16_t moduleID, const char *name, caerVisualizerRenderer renderer,
	caerVisualizerEventHandler eventHandler, caerEventPacketHeader packetHeader);

void caerVisualizerMulti(uint16_t moduleID, const char *name, caerVisualizerRenderer renderer,
	caerVisualizerEventHandler eventHandler, caerEventPacketContainer container);

void caerVisualizerSystemInit(void);

#endif /* VISUALIZER_H_ */
