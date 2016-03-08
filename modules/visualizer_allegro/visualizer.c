#include "visualizer.h"
#include "base/mainloop.h"
#include "base/module.h"
#include "ext/portable_time.h"

#define TEXT_SPACING 20 // in pixels

#define SYSTEM_TIMEOUT 10 // in seconds

void caerVisualizerSystemInit(void) {
	// Initialize the Allegro library.
	if (al_init()) {
		// Successfully initialized Allegro.
		caerLog(CAER_LOG_DEBUG, "Visualizer", "Allegro library initialized successfully.");
	}
	else {
		// Failed to initialize Allegro.
		caerLog(CAER_LOG_EMERGENCY, "Visualizer", "Failed to initialize Allegro library.");
		exit(EXIT_FAILURE);
	}

	// Set correct names.
	al_set_org_name("iniLabs");
	al_set_app_name("cAER");

	// Now load addons: primitives to draw, fonts (and TTF) to write text.
	if (al_init_primitives_addon()) {
		// Successfully initialized Allegro primitives addon.
		caerLog(CAER_LOG_DEBUG, "Visualizer", "Allegro primitives addon initialized successfully.");
	}
	else {
		// Failed to initialize Allegro primitives addon.
		caerLog(CAER_LOG_EMERGENCY, "Visualizer", "Failed to initialize Allegro primitives addon.");
		exit(EXIT_FAILURE);
	}

	al_init_font_addon();

	if (al_init_ttf_addon()) {
		// Successfully initialized Allegro TTF addon.
		caerLog(CAER_LOG_DEBUG, "Visualizer", "Allegro TTF addon initialized successfully.");
	}
	else {
		// Failed to initialize Allegro TTF addon.
		caerLog(CAER_LOG_EMERGENCY, "Visualizer", "Failed to initialize Allegro TTF addon.");
		exit(EXIT_FAILURE);
	}

	// Install main event sources: mouse and keyboard.
	if (al_install_mouse()) {
		// Successfully initialized Allegro mouse event source.
		caerLog(CAER_LOG_DEBUG, "Visualizer", "Allegro mouse event source initialized successfully.");
	}
	else {
		// Failed to initialize Allegro mouse event source.
		caerLog(CAER_LOG_EMERGENCY, "Visualizer", "Failed to initialize Allegro mouse event source.");
		exit(EXIT_FAILURE);
	}

	if (al_install_keyboard()) {
		// Successfully initialized Allegro keyboard event source.
		caerLog(CAER_LOG_DEBUG, "Visualizer", "Allegro keyboard event source initialized successfully.");
	}
	else {
		// Failed to initialize Allegro keyboard event source.
		caerLog(CAER_LOG_EMERGENCY, "Visualizer", "Failed to initialize Allegro keyboard event source.");
		exit(EXIT_FAILURE);
	}
}

bool caerVisualizerInit(caerVisualizerState state, int32_t bitmapSizeX, int32_t bitmapSizeY) {
		// Create display window.
		state->displayWindow = al_create_display(bitmapSizeX, bitmapSizeX);
		if (state->displayWindow == NULL) {
			caerLog(CAER_LOG_ERROR, "Visualizer", "Failed to create display element with sizeX=%d, sizeY=%d.", bitmapSizeX, bitmapSizeY);
			return (false);
		}

		state->displayWindowSizeX = bitmapSizeX;
		state->displayWindowSizeY = bitmapSizeY;

		// Get video buffer
		state->bitmapRenderer = al_get_backbuffer(state->displayWindow);
		state->bitmapRendererSizeX = bitmapSizeX;
		state->bitmapRendererSizeY = bitmapSizeY;

		// Initi
		al_clear_to_color(al_map_rgb(0, 0, 0));
		al_flip_display();

		return (true);
}

void caerVisualizerUpdate(caerEventPacketHeader packetHeader, caerVisualizerState state) {

}

void caerVisualizerExit(caerVisualizerState state) {

}

struct visualizer_module_state {
	struct caer_visualizer_state eventVisualizer;
	struct caer_visualizer_state frameVisualizer;
	int32_t frameRendererPositionX;
	int32_t frameRendererPositionY;
	enum caer_frame_event_color_channels frameRendererChannels;
};

typedef struct visualizer_module_state *visualizerModuleState;

static bool caerVisualizerModuleInit(caerModuleData moduleData);
static void caerVisualizerModuleRun(caerModuleData moduleData, size_t argsNumber, va_list args);
static void caerVisualizerModuleExit(caerModuleData moduleData);
static bool allocateEventRenderer(visualizerModuleState state, int16_t sourceID);
static bool allocateFrameRenderer(visualizerModuleState state, int16_t sourceID);

static struct caer_module_functions caerVisualizerFunctions = { .moduleInit = &caerVisualizerModuleInit, .moduleRun =
	&caerVisualizerModuleRun, .moduleConfig =
NULL, .moduleExit = &caerVisualizerModuleExit };

void caerVisualizer(uint16_t moduleID, caerPolarityEventPacket polarity, caerFrameEventPacket frame) {
	caerModuleData moduleData = caerMainloopFindModule(moduleID, "Visualizer");

	caerModuleSM(&caerVisualizerFunctions, moduleData, sizeof(struct visualizer_module_state), 2, polarity, frame);
}

static bool caerVisualizerModuleInit(caerModuleData moduleData) {
	visualizerModuleState state = moduleData->moduleState;

	// Configuration.
	sshsNodePutBoolIfAbsent(moduleData->moduleNode, "showEvents", true);
#ifdef DVS128
	sshsNodePutBoolIfAbsent(moduleData->moduleNode, "showFrames", false);
#else
	sshsNodePutBoolIfAbsent(moduleData->moduleNode, "showFrames", true);
#endif

	sshsNodePutShortIfAbsent(moduleData->moduleNode, "subsampleRendering", 1);

	state->subsampleRendering = sshsNodeGetShort(moduleData->moduleNode, "subsampleRendering");
	state->subsampleCount = 1;

	if (!caerStatisticsStringInit(&state->eventStatistics)) {
		return (false);
	}

	state->eventStatistics.divisionFactor = 1000;

	if (!caerStatisticsStringInit(&state->frameStatistics)) {
		return (false);
	}

	state->frameStatistics.divisionFactor = 1;

	return (true);
}

static void caerVisualizerModuleExit(caerModuleData moduleData) {
	visualizerModuleState state = moduleData->moduleState;

	// Ensure render maps are freed.
	if (state->eventRenderer != NULL) {
		free(state->eventRenderer);
		state->eventRenderer = NULL;
	}

	if (state->frameRenderer != NULL) {
		free(state->frameRenderer);
		state->frameRenderer = NULL;
	}

	// Statistics text.
	caerStatisticsStringExit(&state->eventStatistics);
	caerStatisticsStringExit(&state->frameStatistics);
}

static void caerVisualizerModuleRun(caerModuleData moduleData, size_t argsNumber, va_list args) {
	UNUSED_ARGUMENT(argsNumber);

	visualizerModuleState state = moduleData->moduleState;

	// Subsampling, only render every Nth packet.
	if (state->subsampleCount >= state->subsampleRendering) {
		// Polarity events to render.
		caerPolarityEventPacket polarity = va_arg(args, caerPolarityEventPacket);
		bool renderPolarity = sshsNodeGetBool(moduleData->moduleNode, "showEvents");

		// Frames to render.
		caerFrameEventPacket frame = va_arg(args, caerFrameEventPacket);
		bool renderFrame = sshsNodeGetBool(moduleData->moduleNode, "showFrames");

		// Update polarity event rendering map.
		if (renderPolarity && polarity != NULL) {
			// If the event renderer is not allocated yet, do it.
			if (state->eventRenderer == NULL) {
				if (!allocateEventRenderer(state, caerEventPacketHeaderGetEventSource(&polarity->packetHeader))) {
					// Failed to allocate memory, nothing to do.
					caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString,
						"Failed to allocate memory for eventRenderer.");
					return;
				}
			}

			if (state->subsampleRendering > 1) {
				memset(state->eventRenderer, 0,
					(size_t) (state->eventRendererSizeX * state->eventRendererSizeY) * sizeof(uint32_t));
			}

			CAER_POLARITY_ITERATOR_VALID_START (polarity)
				if (caerPolarityEventGetPolarity(caerPolarityIteratorElement)) {
					// Green.
					state->eventRenderer[(caerPolarityEventGetY(caerPolarityIteratorElement) * state->eventRendererSizeX)
						+ caerPolarityEventGetX(caerPolarityIteratorElement)] = (U32T(0x00FFUL << 8));
				}
				else {
					// Red.
					state->eventRenderer[(caerPolarityEventGetY(caerPolarityIteratorElement) * state->eventRendererSizeX)
						+ caerPolarityEventGetX(caerPolarityIteratorElement)] = (U32T(0x00FFUL << 0));
				}CAER_POLARITY_ITERATOR_VALID_END

			// Accumulate events over four polarity packets.
			if (state->subsampleRendering <= 1) {
				if (state->eventRendererSlowDown++ == 4) {
					state->eventRendererSlowDown = 0;

					memset(state->eventRenderer, 0,
						(size_t) (state->eventRendererSizeX * state->eventRendererSizeY) * sizeof(uint32_t));
				}
			}
		}

		// Select latest frame to render.
		if (renderFrame && frame != NULL) {
			// If the event renderer is not allocated yet, do it.
			if (state->frameRenderer == NULL) {
				if (!allocateFrameRenderer(state, caerEventPacketHeaderGetEventSource(&frame->packetHeader))) {
					// Failed to allocate memory, nothing to do.
					caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString,
						"Failed to allocate memory for frameRenderer.");
					return;
				}
			}

			caerFrameEvent currFrameEvent;

			for (int32_t i = caerEventPacketHeaderGetEventNumber(&frame->packetHeader) - 1; i >= 0; i--) {
				currFrameEvent = caerFrameEventPacketGetEvent(frame, i);

				// Only operate on the last valid frame.
				if (caerFrameEventIsValid(currFrameEvent)) {
					// Copy the frame content to the permanent frameRenderer.
					// Use frame sizes to correctly support small ROI frames.
					state->frameRendererSizeX = caerFrameEventGetLengthX(currFrameEvent);
					state->frameRendererSizeY = caerFrameEventGetLengthY(currFrameEvent);
					state->frameRendererPositionX = caerFrameEventGetPositionX(currFrameEvent);
					state->frameRendererPositionY = caerFrameEventGetPositionY(currFrameEvent);
					state->frameChannels = caerFrameEventGetChannelNumber(currFrameEvent);

					memcpy(state->frameRenderer, caerFrameEventGetPixelArrayUnsafe(currFrameEvent),
						((size_t) state->frameRendererSizeX * (size_t) state->frameRendererSizeY
							* (size_t) state->frameChannels * sizeof(uint16_t)));

					break;
				}
			}
		}

		// Detect if nothing happened for a long time.
		struct timespec currentTime;
		portable_clock_gettime_monotonic(&currentTime);

		uint64_t diffNanoTimeEvents = (uint64_t) (((int64_t) (currentTime.tv_sec
			- state->eventStatistics.lastTime.tv_sec) * 1000000000LL)
			+ (int64_t) (currentTime.tv_nsec - state->eventStatistics.lastTime.tv_nsec));
		bool noEventsTimeout = (diffNanoTimeEvents >= U64T(SYSTEM_TIMEOUT * 1000000000LL));

		uint64_t diffNanoTimeFrames = (uint64_t) (((int64_t) (currentTime.tv_sec
			- state->frameStatistics.lastTime.tv_sec) * 1000000000LL)
			+ (int64_t) (currentTime.tv_nsec - state->frameStatistics.lastTime.tv_nsec));
		bool noFramesTimeout = (diffNanoTimeFrames >= U64T(SYSTEM_TIMEOUT * 1000000000LL));

		// TO DO SEPARATE THREAD
		//
		// All rendering calls at the end.
		// Only execute if something actually changed (packets not null).
		if ((renderPolarity && (polarity != NULL || noEventsTimeout))
			|| (renderFrame && (frame != NULL || noFramesTimeout))) {

			//set backbuffer as target
			al_clear_to_color(al_map_rgb(0, 0, 0));

			// Render polarity events.
			if (renderPolarity) {
				// Write statistics text.
				caerStatisticsStringUpdate((caerEventPacketHeader) polarity, &state->eventStatistics);
				//lock bitmap
				al_set_target_bitmap(state->bb);
				ALLEGRO_LOCKED_REGION * lock;
				lock = al_lock_bitmap(state->bb, ALLEGRO_PIXEL_FORMAT_ABGR_8888_LE, ALLEGRO_LOCK_WRITEONLY);
				if (lock != NULL) {
					uint8_t *data = lock->data;
					uint8_t *eventRR = state->eventRenderer;
					for (int y = 0; y < state->eventRendererSizeY; y++) {
						memcpy(data, eventRR, state->eventRendererSizeX * sizeof(uint32_t));

						data += lock->pitch;
						eventRR += state->eventRendererSizeX * sizeof(uint32_t);
					}
					//unlock
					al_unlock_bitmap(state->bb);
					// update display
					al_flip_display();
				}
			}

			// Render latest frame.
			if (renderFrame) {
				// Write statistics text.
				caerStatisticsStringUpdate((caerEventPacketHeader) frame, &state->frameStatistics);

				//lock bitmap
				al_set_target_bitmap(state->bbframes);
				ALLEGRO_LOCKED_REGION * lock;
				lock = al_lock_bitmap(state->bbframes, ALLEGRO_PIXEL_FORMAT_ABGR_8888_LE, ALLEGRO_LOCK_WRITEONLY);
				if (lock != NULL) {
					uint8_t *data = lock->data;
					uint8_t *eventRR = state->frameRenderer;
					for (int y = 0; y < state->eventRendererSizeY; y++) {
						memcpy(data, eventRR, state->frameRendererSizeX * sizeof(uint16_t));

						data += lock->pitch;
						eventRR += state->frameRendererSizeX * sizeof(uint16_t);
					}
					//unlock
					al_unlock_bitmap(state->bbframes);
					// update display
					al_flip_display();
				}

				switch (state->frameChannels) {
					case GRAYSCALE:
						break;

					case RGB:
						break;

					case RGBA:
						break;
				}
			}

		}

		state->subsampleCount = 1;
	}

	else {
		state->subsampleCount++;
	}

}

