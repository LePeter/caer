#include "visualizer.h"
#include "base/mainloop.h"
#include "base/module.h"
#include "ext/portable_time.h"

#define SYSTEM_TIMEOUT 10 // in seconds

#define GLOBAL_RESOURCES_DIR "ext/resources/"
#define GLOBAL_FONT_NAME "LiberationSans-Bold.ttf"
#define GLOBAL_FONT_SIZE 20 // in pixels
#define GLOBAL_FONT_SPACING 5 // in pixels

static ALLEGRO_PATH *globalResourcesPath = NULL;

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

	// Set up path to find local resources.
	globalResourcesPath = al_get_standard_path(ALLEGRO_RESOURCES_PATH);
	if (globalResourcesPath != NULL) {
		// Successfully loaded standard resources path.
		caerLog(CAER_LOG_DEBUG, "Visualizer", "Allegro standard resources path loaded successfully.");
	}
	else {
		// Failed to load standard resources path.
		caerLog(CAER_LOG_EMERGENCY, "Visualizer", "Failed to load Allegro standard resources path.");
		exit(EXIT_FAILURE);
	}

	al_append_path_component(globalResourcesPath, GLOBAL_RESOURCES_DIR);

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

bool caerVisualizerInit(caerVisualizerState state, int32_t bitmapSizeX, int32_t bitmapSizeY, int32_t zoomFactor,
bool doStatistics) {
	// Create display window.
	// Add 30 pixels to Y for automatic statistics if needed (5 spacing + 20 text + 5 spacing).
	state->displayWindow = al_create_display(bitmapSizeX * zoomFactor,
		bitmapSizeY * zoomFactor
			+ ((doStatistics) ? (GLOBAL_FONT_SPACING + GLOBAL_FONT_SIZE + GLOBAL_FONT_SPACING) : (0)));
	if (state->displayWindow == NULL) {
		caerLog(CAER_LOG_ERROR, "Visualizer",
			"Failed to create display element with sizeX=%d, sizeY=%d, zoomFactor=%d.", bitmapSizeX, bitmapSizeY,
			zoomFactor);
		return (false);
	}

	// Initialize window to all black.
	al_set_target_backbuffer(state->displayWindow);
	al_clear_to_color(al_map_rgb(0, 0, 0));
	al_flip_display();

	// Load font here so it's hardware accelerated.
	// A display must be created and used as target for this to work.
	al_set_path_filename(globalResourcesPath, GLOBAL_FONT_NAME);
	state->displayFont = al_load_font(al_path_cstr(globalResourcesPath, ALLEGRO_NATIVE_PATH_SEP), GLOBAL_FONT_SIZE, 0);
	if (state->displayFont == NULL) {
		caerLog(CAER_LOG_ERROR, "Visualizer", "Failed to load display font.");
		return (false);
	}

	// Create video buffer for drawing.
	al_set_new_bitmap_flags(ALLEGRO_MEMORY_BITMAP | ALLEGRO_MIN_LINEAR | ALLEGRO_MAG_LINEAR);
	state->bitmapRenderer = al_create_bitmap(bitmapSizeX, bitmapSizeY);
	if (state->bitmapRenderer == NULL) {
		caerLog(CAER_LOG_ERROR, "Visualizer", "Failed to create bitmap element with sizeX=%d, sizeY=%d.", bitmapSizeX,
			bitmapSizeY);
		return (false);
	}

	// Clear bitmap to all black.
	al_set_target_bitmap(state->bitmapRenderer);
	al_clear_to_color(al_map_rgb(0, 0, 0));

	// Remember sizes.
	state->bitmapRendererSizeX = bitmapSizeX;
	state->bitmapRendererSizeY = bitmapSizeY;
	state->displayWindowZoomFactor = zoomFactor;

	// Enable packet statistics and sub-sampling support.
	if (doStatistics) {
		caerStatisticsStringInit(&state->packetStatistics);
	}

	state->packetSubsampleRendering = 1;
	state->packetSubsampleCount = 0;

	// Initialize mutex for locking between update and screen draw operations.
	mtx_init(&state->bitmapMutex, mtx_plain);

	atomic_store(&state->running, true);

	return (true);
}

void caerVisualizerUpdate(caerEventPacketHeader packetHeader, caerVisualizerState state) {
	// Only render ever Nth packet.
	state->packetSubsampleCount++;

	if (state->packetSubsampleCount == state->packetSubsampleRendering) {
		state->packetSubsampleCount = 0;
	}
	else {
		return;
	}

	mtx_lock(&state->bitmapMutex);

	// Update statistics (if enabled).
	if (state->packetStatistics.currentStatisticsString != NULL) {
		caerStatisticsStringUpdate(packetHeader, &state->packetStatistics);
	}

	al_set_target_bitmap(state->bitmapRenderer);
	al_clear_to_color(al_map_rgb(0, 0, 0));

	// Update bitmap with new content.
	if (caerEventPacketHeaderGetEventType(packetHeader) == POLARITY_EVENT) {
		CAER_POLARITY_ITERATOR_ALL_START((caerPolarityEventPacket) packetHeader)
			if (caerPolarityEventGetPolarity(caerPolarityIteratorElement)) {
				// ON polarity (green).
				al_put_pixel(caerPolarityEventGetX(caerPolarityIteratorElement),
					caerPolarityEventGetY(caerPolarityIteratorElement), al_map_rgb(0, 255, 0));
			}
			else {
				// OFF polarity (red).
				al_put_pixel(caerPolarityEventGetX(caerPolarityIteratorElement),
					caerPolarityEventGetY(caerPolarityIteratorElement), al_map_rgb(255, 0, 0));
			}
		CAER_POLARITY_ITERATOR_ALL_END
	}

	mtx_unlock(&state->bitmapMutex);
}

void caerVisualizerUpdateScreen(caerVisualizerState state) {
	al_set_target_backbuffer(state->displayWindow);
	al_clear_to_color(al_map_rgb(0, 0, 0));

	mtx_lock(&state->bitmapMutex);

	// Render statistics string.
	bool doStatistics = (state->packetStatistics.currentStatisticsString != NULL);

	if (doStatistics) {
		al_draw_text(state->displayFont, al_map_rgb(255, 255, 255), GLOBAL_FONT_SPACING,
		GLOBAL_FONT_SPACING, 0, state->packetStatistics.currentStatisticsString);
	}

	// Blit bitmap to screen, taking zoom factor into consideration.
	al_draw_scaled_bitmap(state->bitmapRenderer, 0, 0, state->bitmapRendererSizeX, state->bitmapRendererSizeY, 0,
		(doStatistics) ? (GLOBAL_FONT_SPACING + GLOBAL_FONT_SIZE + GLOBAL_FONT_SPACING) : (0),
		state->bitmapRendererSizeX * state->displayWindowZoomFactor,
		state->bitmapRendererSizeY * state->displayWindowZoomFactor, 0);

	mtx_unlock(&state->bitmapMutex);

	al_flip_display();
}

void caerVisualizerExit(caerVisualizerState state) {
	al_set_target_bitmap(NULL);

	al_destroy_bitmap(state->bitmapRenderer);
	state->bitmapRenderer = NULL;

	al_destroy_display(state->displayWindow);
	state->displayWindow = NULL;

	if (state->packetStatistics.currentStatisticsString != NULL) {
		caerStatisticsStringExit(&state->packetStatistics);
	}

	mtx_destroy(&state->bitmapMutex);

	atomic_store(&state->running, false);
}

struct visualizer_module_state {
	thrd_t renderingThread;
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
static int caerVisualizerModuleRenderThread(void *moduleData);
static bool initializeEventRenderer(visualizerModuleState state, int16_t sourceID);
static bool initializeFrameRenderer(visualizerModuleState state, int16_t sourceID);

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

	sshsNodePutIntIfAbsent(moduleData->moduleNode, "subsampleRendering", 1);

	// Start separate rendering thread. Decouples presentation from
	// data processing and preparation. Communication over properly
	// locked bitmap.
	thrd_create(&state->renderingThread, &caerVisualizerModuleRenderThread, moduleData);

	return (true);
}

static void caerVisualizerModuleExit(caerModuleData moduleData) {
	visualizerModuleState state = moduleData->moduleState;

	// Wait on rendering thread.
	thrd_join(state->renderingThread, NULL);

	// Ensure render maps are freed.
	if (atomic_load_explicit(&state->eventVisualizer.running, memory_order_relaxed)) {
		caerVisualizerExit(&state->eventVisualizer);
	}

	if (atomic_load_explicit(&state->frameVisualizer.running, memory_order_relaxed)) {
		caerVisualizerExit(&state->frameVisualizer);
	}
}

static void caerVisualizerModuleRun(caerModuleData moduleData, size_t argsNumber, va_list args) {
	UNUSED_ARGUMENT(argsNumber);

	visualizerModuleState state = moduleData->moduleState;

	// Polarity events to render.
	caerPolarityEventPacket polarity = va_arg(args, caerPolarityEventPacket);
	bool renderPolarity = sshsNodeGetBool(moduleData->moduleNode, "showEvents");

	// Frames to render.
	caerFrameEventPacket frame = va_arg(args, caerFrameEventPacket);
	bool renderFrame = sshsNodeGetBool(moduleData->moduleNode, "showFrames");

	// Update polarity event rendering map.
	if (renderPolarity && polarity != NULL) {
		// If the event renderer is not allocated yet, do it.
		if (!atomic_load_explicit(&state->eventVisualizer.running, memory_order_relaxed)) {
			if (!initializeEventRenderer(state, caerEventPacketHeaderGetEventSource(&polarity->packetHeader))) {
				caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Failed to initialize event visualizer.");
				return;
			}
		}

		// Actually update polarity rendering.
		caerVisualizerUpdate(&polarity->packetHeader, &state->eventVisualizer);
	}

	// Select latest frame to render.
	if (renderFrame && frame != NULL) {
		// If the event renderer is not allocated yet, do it.
		if (!atomic_load_explicit(&state->frameVisualizer.running, memory_order_relaxed)) {
			if (!initializeFrameRenderer(state, caerEventPacketHeaderGetEventSource(&frame->packetHeader))) {
				caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Failed to initialize frame visualizer.");
				return;
			}
		}

		// Actually update frame rendering.
		caerVisualizerUpdate(&frame->packetHeader, &state->frameVisualizer);
	}
}

static int caerVisualizerModuleRenderThread(void *moduleData) {
	caerModuleData data = moduleData;
	visualizerModuleState state = data->moduleState;

	while (atomic_load_explicit(&data->running, memory_order_relaxed)) {
		if (atomic_load_explicit(&state->eventVisualizer.running, memory_order_relaxed)) {
			caerVisualizerUpdateScreen(&state->eventVisualizer);
		}

		if (atomic_load_explicit(&state->frameVisualizer.running, memory_order_relaxed)) {
			caerVisualizerUpdateScreen(&state->frameVisualizer);
		}
	}

	return (thrd_success);
}

static bool initializeEventRenderer(visualizerModuleState state, int16_t sourceID) {
	// Get size information from source.
	sshsNode sourceInfoNode = caerMainloopGetSourceInfo((uint16_t) sourceID);
	int16_t sizeX = sshsNodeGetShort(sourceInfoNode, "dvsSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoNode, "dvsSizeY");

	if (!caerVisualizerInit(&state->eventVisualizer, sizeX, sizeY, 1, true)) {
		return (false);
	}

	return (true);
}

static bool initializeFrameRenderer(visualizerModuleState state, int16_t sourceID) {
	// Get size information from source.
	sshsNode sourceInfoNode = caerMainloopGetSourceInfo((uint16_t) sourceID);
	int16_t sizeX = sshsNodeGetShort(sourceInfoNode, "apsSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoNode, "apsSizeY");

	if (!caerVisualizerInit(&state->frameVisualizer, sizeX, sizeY, 1, true)) {
		return (false);
	}

	return (true);
}
