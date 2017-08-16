#include "visualizer.hpp"
#include "base/mainloop.h"
#include "ext/ringbuffer/ringbuffer.h"
#include "ext/threads_ext.h"
#include "ext/resources/LiberationSans-Bold.h"
#include "modules/statistics/statistics.h"

#include "visualizer_handlers.hpp"
#include "visualizer_renderers.hpp"

#include <atomic>
#include <thread>
#include <mutex>

static caerVisualizerState caerVisualizerInit(caerVisualizerRenderer renderer, caerVisualizerEventHandler eventHandler,
	int32_t bitmapSizeX, int32_t bitmapSizeY, float defaultZoomFactor, bool defaultShowStatistics,
	caerModuleData parentModule, int16_t eventSourceID);
static void caerVisualizerUpdate(caerVisualizerState state, caerEventPacketContainer container);
static void caerVisualizerExit(caerVisualizerState state);
static void caerVisualizerReset(caerVisualizerState state);
static void caerVisualizerSystemInit(void);

static std::once_flag visualizerSystemIsInitialized;

struct caer_visualizer_renderers {
	const char *name;
	caerVisualizerRenderer renderer;
};

static const char *caerVisualizerRendererListOptionsString =
	"Polarity,Frame,IMU_6-axes,2D_Points,Spikes,Spikes_Raster_Plot,ETF4D,Polarity_and_Frames";

static struct caer_visualizer_renderers caerVisualizerRendererList[] = { { "Polarity",
	&caerVisualizerRendererPolarityEvents }, { "Frame", &caerVisualizerRendererFrameEvents }, { "IMU_6-axes",
	&caerVisualizerRendererIMU6Events }, { "2D_Points", &caerVisualizerRendererPoint2DEvents }, { "Spikes",
	&caerVisualizerRendererSpikeEvents }, { "Spikes_Raster_Plot", &caerVisualizerRendererSpikeEventsRaster }, { "ETF4D",
	&caerVisualizerRendererETF4D }, { "Polarity_and_Frames", &caerVisualizerMultiRendererPolarityAndFrameEvents }, };

struct caer_visualizer_handlers {
	const char *name;
	caerVisualizerEventHandler handler;
};

static const char *caerVisualizerHandlerListOptionsString = "None,Spikes,Input";

static struct caer_visualizer_handlers caerVisualizerHandlerList[] = { { "None", NULL }, { "Spikes",
	&caerVisualizerEventHandlerSpikeEvents }, { "Input", &caerInputVisualizerEventHandler } };

struct caer_visualizer_state {
	sshsNode eventSourceConfigNode;
	sshsNode visualizerConfigNode;
	uint32_t displayWindowSizeX;
	uint32_t displayWindowSizeY;
	sf::RenderWindow *displayWindow;
	sf::Font *displayFont;
	std::atomic_bool running;
	std::atomic_bool displayWindowResize;
	bool bitmapDrawUpdate;
	RingBuffer dataTransfer;
	std::thread renderingThread;
	caerVisualizerRenderer renderer;
	caerVisualizerEventHandler eventHandler;
	caerModuleData parentModule;
	bool showStatistics;
	struct caer_statistics_state packetStatistics;
	std::atomic_uint_fast32_t packetSubsampleRendering;
	int32_t packetSubsampleCount;
};

static void updateDisplaySize(caerVisualizerState state, bool updateTransform);
static void updateDisplayLocation(caerVisualizerState state);
static void saveDisplayLocation(caerVisualizerState state);
static void caerVisualizerConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static bool caerVisualizerInitGraphics(caerVisualizerState state);
static void caerVisualizerUpdateScreen(caerVisualizerState state);
static void caerVisualizerExitGraphics(caerVisualizerState state);
static int caerVisualizerRenderThread(void *visualizerState);

#define GLOBAL_FONT_SIZE 20 // in pixels
#define GLOBAL_FONT_SPACING 5 // in pixels

// Calculated at system init.
static int STATISTICS_WIDTH = 0;
static int STATISTICS_HEIGHT = 0;

static void caerVisualizerSystemInit(void) {
	// Determine biggest possible statistics string.
	size_t maxStatStringLength = (size_t) snprintf(NULL, 0, CAER_STATISTICS_STRING_TOTAL, UINT64_MAX);

	char maxStatString[maxStatStringLength + 1];
	snprintf(maxStatString, maxStatStringLength + 1, CAER_STATISTICS_STRING_TOTAL, UINT64_MAX);
	maxStatString[maxStatStringLength] = '\0';

	// Load statistics font into memory.
	sf::Font font;
	if (!font.loadFromMemory(LiberationSans_Bold_ttf, LiberationSans_Bold_ttf_len)) {
		caerLog(CAER_LOG_ERROR, "Visualizer", "Failed to load display font.");
	}

	// Determine statistics string width.
	sf::Text maxStatText(maxStatString, font, GLOBAL_FONT_SIZE);
	STATISTICS_WIDTH = (2 * GLOBAL_FONT_SPACING) + (int) maxStatText.getLocalBounds().width;

	STATISTICS_HEIGHT = (3 * GLOBAL_FONT_SPACING) + (2 * (int) maxStatText.getLocalBounds().height);
}

caerVisualizerState caerVisualizerInit(caerVisualizerRenderer renderer, caerVisualizerEventHandler eventHandler,
	int32_t bitmapSizeX, int32_t bitmapSizeY, float defaultZoomFactor, bool defaultShowStatistics,
	caerModuleData parentModule, int16_t eventSourceID) {
	// Initialize visualizer framework (load fonts etc.). Do only once per startup!
	std::call_once(visualizerSystemIsInitialized, &caerVisualizerSystemInit);

	// Allocate memory for visualizer state.
	caerVisualizerState state = calloc(1, sizeof(struct caer_visualizer_state));
	if (state == NULL) {
		caerModuleLog(parentModule, CAER_LOG_ERROR, "Visualizer: Failed to allocate state memory.");
		return (NULL);
	}

	state->parentModule = parentModule;
	state->visualizerConfigNode = parentModule->moduleNode;
	if (eventSourceID >= 0) {
		state->eventSourceModuleState = caerMainloopGetSourceState(eventSourceID);
		state->eventSourceConfigNode = caerMainloopGetSourceNode(eventSourceID);
	}

	// Configuration.
	sshsNodeCreateInt(parentModule->moduleNode, "subsampleRendering", 1, 1, 1024 * 1024, SSHS_FLAGS_NORMAL,
		"Speed-up rendering by only taking every Nth EventPacketContainer to render.");
	sshsNodeCreateBool(parentModule->moduleNode, "showStatistics", defaultShowStatistics, SSHS_FLAGS_NORMAL,
		"Show event statistics above content (top of window).");
	sshsNodeCreateFloat(parentModule->moduleNode, "zoomFactor", defaultZoomFactor, 0.5f, 50.0f, SSHS_FLAGS_NORMAL,
		"Content zoom factor.");
	sshsNodeCreateInt(parentModule->moduleNode, "windowPositionX", VISUALIZER_DEFAULT_POSITION_X, 0, INT32_MAX,
		SSHS_FLAGS_NORMAL, "Position of window on screen (X coordinate).");
	sshsNodeCreateInt(parentModule->moduleNode, "windowPositionY", VISUALIZER_DEFAULT_POSITION_Y, 0, INT32_MAX,
		SSHS_FLAGS_NORMAL, "Position of window on screen (Y coordinate).");

	state->packetSubsampleRendering.store(sshsNodeGetInt(parentModule->moduleNode, "subsampleRendering"));

	// Remember sizes.
	state->bitmapRendererSizeX = bitmapSizeX;
	state->bitmapRendererSizeY = bitmapSizeY;

	updateDisplaySize(state, false);

	// Remember rendering and event handling function.
	state->renderer = renderer;
	state->eventHandler = eventHandler;

	// Enable packet statistics.
	if (!caerStatisticsStringInit(&state->packetStatistics)) {
		free(state);

		caerModuleLog(parentModule, CAER_LOG_ERROR, "Visualizer: Failed to initialize statistics string.");
		return (NULL);
	}

	// Initialize ring-buffer to transfer data to render thread.
	state->dataTransfer = ringBufferInit(64);
	if (state->dataTransfer == NULL) {
		caerStatisticsStringExit(&state->packetStatistics);
		free(state);

		caerModuleLog(parentModule, CAER_LOG_ERROR, "Visualizer: Failed to initialize ring-buffer.");
		return (NULL);
	}

	// Start separate rendering thread. Decouples presentation from
	// data processing and preparation. Communication over ring-buffer.
	state->running.store(true);

	try {
		state->renderingThread = std::thread(&caerVisualizerRenderThread, state);
	}
	catch (const std::system_error &)  {
		ringBufferFree(state->dataTransfer);
		caerStatisticsStringExit(&state->packetStatistics);
		free(state);

		caerModuleLog(parentModule, CAER_LOG_ERROR, "Visualizer: Failed to start rendering thread.");
		return (NULL);
	}

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(parentModule->moduleNode, state, &caerVisualizerConfigListener);

	caerModuleLog(parentModule, CAER_LOG_DEBUG, "Visualizer: Initialized successfully.");

	return (state);
}

static void updateDisplayLocation(caerVisualizerState state) {
	// Set current position to what is in configuration storage.
	const sf::Vector2i newPos(sshsNodeGetInt(state->parentModule->moduleNode, "windowPositionX"),
		sshsNodeGetInt(state->parentModule->moduleNode, "windowPositionY"));

	state->displayWindow->setPosition(newPos);
}

static void saveDisplayLocation(caerVisualizerState state) {
	const sf::Vector2i currPos = state->displayWindow->getPosition();

	// Update current position in configuration storage.
	sshsNodePutInt(state->parentModule->moduleNode, "windowPositionX", currPos.x);
	sshsNodePutInt(state->parentModule->moduleNode, "windowPositionY", currPos.y);
}

static void updateDisplaySize(caerVisualizerState state, bool updateTransform) {
	state->showStatistics = sshsNodeGetBool(state->parentModule->moduleNode, "showStatistics");
	float zoomFactor = sshsNodeGetFloat(state->parentModule->moduleNode, "zoomFactor");

	int32_t displayWindowSizeX = state->bitmapRendererSizeX;
	int32_t displayWindowSizeY = state->bitmapRendererSizeY;

	// When statistics are turned on, we need to add some space to the
	// X axis for displaying the whole line and the Y axis for spacing.
	if (state->showStatistics) {
		if (STATISTICS_WIDTH > displayWindowSizeX) {
			displayWindowSizeX = STATISTICS_WIDTH;
		}

		displayWindowSizeY += STATISTICS_HEIGHT;
	}

	state->displayWindowSizeX = I32T((float ) displayWindowSizeX * zoomFactor);
	state->displayWindowSizeY = I32T((float ) displayWindowSizeY * zoomFactor);

	// Update Allegro drawing transformation to implement scaling.
	if (updateTransform) {
		al_set_target_backbuffer(state->displayWindow);

		ALLEGRO_TRANSFORM t;
		al_identity_transform(&t);
		al_scale_transform(&t, zoomFactor, zoomFactor);
		al_use_transform(&t);

		al_resize_display(state->displayWindow, state->displayWindowSizeX, state->displayWindowSizeY);
	}
}

static void caerVisualizerConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerVisualizerState state = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_FLOAT && caerStrEquals(changeKey, "zoomFactor")) {
			// Set resize flag.
			state->displayWindowResize.store(true);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "showStatistics")) {
			// Set resize flag. This will then also update the showStatistics flag, ensuring
			// statistics are never shown without the screen having been properly resized first.
			state->displayWindowResize.store(true);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "subsampleRendering")) {
			state->packetSubsampleRendering.store(changeValue.iint);
		}
	}
}

void caerVisualizerUpdate(caerVisualizerState state, caerEventPacketContainer container) {
	if (state == NULL || container == NULL) {
		return;
	}

	// Keep statistics up-to-date with all events, always.
	CAER_EVENT_PACKET_CONTAINER_ITERATOR_START(container)
			caerStatisticsStringUpdate(caerEventPacketContainerIteratorElement, &state->packetStatistics);
		CAER_EVENT_PACKET_CONTAINER_ITERATOR_END

		// Only render every Nth container (or packet, if using standard visualizer).
	state->packetSubsampleCount++;

	if (state->packetSubsampleCount >= state->packetSubsampleRendering.load(std::memory_order_relaxed)) {
		state->packetSubsampleCount = 0;
	}
	else {
		return;
	}

	caerEventPacketContainer containerCopy = caerEventPacketContainerCopyAllEvents(container);
	if (containerCopy == NULL) {
		caerModuleLog(state->parentModule, CAER_LOG_ERROR,
			"Visualizer: Failed to copy event packet container for rendering.");

		return;
	}

	if (!ringBufferPut(state->dataTransfer, containerCopy)) {
		caerEventPacketContainerFree(containerCopy);

		caerModuleLog(state->parentModule, CAER_LOG_INFO,
			"Visualizer: Failed to move event packet container copy to ring-buffer (full).");
		return;
	}
}

void caerVisualizerExit(caerVisualizerState state) {
	if (state == NULL) {
		return;
	}

	// Update visualizer location
	saveDisplayLocation(state);

	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(state->parentModule->moduleNode, state, &caerVisualizerConfigListener);

	// Shut down rendering thread and wait on it to finish.
	state->running.store(false);

	try {
		state->renderingThread.join();
	}
	catch (const std::system_error &)  {
		// This should never happen!
		caerModuleLog(state->parentModule, CAER_LOG_CRITICAL, "Visualizer: Failed to join rendering thread. Error: %d.",
		errno);
	}

	// Now clean up the ring-buffer and its contents.
	caerEventPacketContainer container;
	while ((container = ringBufferGet(state->dataTransfer)) != NULL) {
		caerEventPacketContainerFree(container);
	}

	ringBufferFree(state->dataTransfer);

	// Then the statistics string.
	caerStatisticsStringExit(&state->packetStatistics);

	caerModuleLog(state->parentModule, CAER_LOG_DEBUG, "Visualizer: Exited successfully.");

	// And finally the state memory.
	free(state);
}

void caerVisualizerReset(caerVisualizerState state) {
	if (state == NULL) {
		return;
	}

	// Reset statistics and counters.
	caerStatisticsStringReset(&state->packetStatistics);
	state->packetSubsampleCount = 0;
}

static bool caerVisualizerInitGraphics(caerVisualizerState state) {
	// Create display window and set its title.
	state->displayWindow = new sf::RenderWindow(sf::VideoMode(state->displayWindowSizeX, state->displayWindowSizeY), state->parentModule->moduleSubSystemString, sf::Style::Titlebar | sf::Style::Close);
	if (state->displayWindow == nullptr) {
		caerModuleLog(state->parentModule, CAER_LOG_ERROR,
			"Visualizer: Failed to create display window with sizeX=%" PRIu32 ", sizeY=%" PRIu32 ".", state->displayWindowSizeX,
			state->displayWindowSizeY);
		return (false);
	}

	// Enable VSync to avoid tearing.
	state->displayWindow->setVerticalSyncEnabled(true);

	// Initialize window to all black.
	state->displayWindow->clear(sf::Color::Black);
	state->displayWindow->display();

	// Set scale transform for display window, update sizes.
	updateDisplaySize(state, true);

	// Set window position.
	updateDisplayLocation(state);

	if (state->bitmapRenderer == NULL) {
		// Clean up all memory that may have been used.
		caerVisualizerExitGraphics(state);

		caerModuleLog(state->parentModule, CAER_LOG_ERROR,
			"Visualizer: Failed to create bitmap element with sizeX=%d, sizeY=%d.", state->bitmapRendererSizeX,
			state->bitmapRendererSizeY);
		return (false);
	}

	// Clear bitmap to all black.
	al_set_target_bitmap(state->bitmapRenderer);
	al_clear_to_color(al_map_rgb(0, 0, 0));

	// Timers and event queues for the rendering side.
	state->displayEventQueue = al_create_event_queue();
	if (state->displayEventQueue == NULL) {
		// Clean up all memory that may have been used.
		caerVisualizerExitGraphics(state);

		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Visualizer: Failed to create event queue.");
		return (false);
	}

	state->displayTimer = al_create_timer((double) (1.00f / VISUALIZER_REFRESH_RATE));
	if (state->displayTimer == NULL) {
		// Clean up all memory that may have been used.
		caerVisualizerExitGraphics(state);

		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Visualizer: Failed to create timer.");
		return (false);
	}

	al_register_event_source(state->displayEventQueue, al_get_display_event_source(state->displayWindow));
	al_register_event_source(state->displayEventQueue, al_get_timer_event_source(state->displayTimer));
	al_register_event_source(state->displayEventQueue, al_get_keyboard_event_source());
	al_register_event_source(state->displayEventQueue, al_get_mouse_event_source());

	// Re-load font here so it's hardware accelerated.
	// A display must have been created and used as target for this to work.
	state->displayFont = al_load_font(globalFontPath, GLOBAL_FONT_SIZE, 0);
	if (state->displayFont == NULL) {
		caerModuleLog(state->parentModule, CAER_LOG_WARNING,
			"Visualizer: Failed to load display font '%s'. Text rendering will not be possible.", globalFontPath);
	}

	// Everything fine, start timer for refresh.
	al_start_timer(state->displayTimer);

	return (true);
}

static void caerVisualizerUpdateScreen(caerVisualizerState state) {
	caerEventPacketContainer container = ringBufferGet(state->dataTransfer);

	repeat: if (container != NULL) {
		// Are there others? Only render last one, to avoid getting backed up!
		caerEventPacketContainer container2 = ringBufferGet(state->dataTransfer);

		if (container2 != NULL) {
			caerEventPacketContainerFree(container);
			container = container2;
			goto repeat;
		}
	}

	if (container != NULL) {
		al_set_target_bitmap(state->bitmapRenderer);

		// Update bitmap with new content. (0, 0) is upper left corner.
		// NULL renderer is supported and simply does nothing (black screen).
		if (state->renderer != NULL) {
			bool didDrawSomething = (*state->renderer)((caerVisualizerPublicState) state, container,
				!state->bitmapDrawUpdate);

			// Remember if something was drawn, even just once.
			if (!state->bitmapDrawUpdate) {
				state->bitmapDrawUpdate = didDrawSomething;
			}
		}

		// Free packet container copy.
		caerEventPacketContainerFree(container);
	}

	bool redraw = false;
	ALLEGRO_EVENT displayEvent;

	handleEvents: al_wait_for_event(state->displayEventQueue, &displayEvent);

	if (displayEvent.type == ALLEGRO_EVENT_TIMER) {
		redraw = true;
	}
	else if (displayEvent.type == ALLEGRO_EVENT_DISPLAY_CLOSE) {
		sshsNodePutBool(state->parentModule->moduleNode, "running", false);
	}
	else if (displayEvent.type == ALLEGRO_EVENT_KEY_CHAR || displayEvent.type == ALLEGRO_EVENT_KEY_DOWN
		|| displayEvent.type == ALLEGRO_EVENT_KEY_UP) {
		// React to key presses, but only if they came from the corresponding display.
		if (displayEvent.keyboard.display == state->displayWindow) {
			if (displayEvent.type == ALLEGRO_EVENT_KEY_DOWN && displayEvent.keyboard.keycode == ALLEGRO_KEY_UP) {
				float currentZoomFactor = sshsNodeGetFloat(state->parentModule->moduleNode, "zoomFactor");

				currentZoomFactor += 0.5f;

				// Clip zoom factor.
				if (currentZoomFactor > 50) {
					currentZoomFactor = 50;
				}

				sshsNodePutFloat(state->parentModule->moduleNode, "zoomFactor", currentZoomFactor);
			}
			else if (displayEvent.type == ALLEGRO_EVENT_KEY_DOWN && displayEvent.keyboard.keycode == ALLEGRO_KEY_DOWN) {
				float currentZoomFactor = sshsNodeGetFloat(state->parentModule->moduleNode, "zoomFactor");

				currentZoomFactor -= 0.5f;

				// Clip zoom factor.
				if (currentZoomFactor < 0.5f) {
					currentZoomFactor = 0.5f;
				}

				sshsNodePutFloat(state->parentModule->moduleNode, "zoomFactor", currentZoomFactor);
			}
			else if (displayEvent.type == ALLEGRO_EVENT_KEY_DOWN && displayEvent.keyboard.keycode == ALLEGRO_KEY_W) {
				int32_t currentSubsampling = sshsNodeGetInt(state->parentModule->moduleNode, "subsampleRendering");

				currentSubsampling--;

				// Clip subsampling factor.
				if (currentSubsampling < 1) {
					currentSubsampling = 1;
				}

				sshsNodePutInt(state->parentModule->moduleNode, "subsampleRendering", currentSubsampling);
			}
			else if (displayEvent.type == ALLEGRO_EVENT_KEY_DOWN && displayEvent.keyboard.keycode == ALLEGRO_KEY_E) {
				int32_t currentSubsampling = sshsNodeGetInt(state->parentModule->moduleNode, "subsampleRendering");

				currentSubsampling++;

				// Clip subsampling factor.
				if (currentSubsampling > 100000) {
					currentSubsampling = 100000;
				}

				sshsNodePutInt(state->parentModule->moduleNode, "subsampleRendering", currentSubsampling);
			}
			else if (displayEvent.type == ALLEGRO_EVENT_KEY_DOWN && displayEvent.keyboard.keycode == ALLEGRO_KEY_Q) {
				bool currentShowStatistics = sshsNodeGetBool(state->parentModule->moduleNode, "showStatistics");

				sshsNodePutBool(state->parentModule->moduleNode, "showStatistics", !currentShowStatistics);
			}
			else {
				// Forward event to user-defined event handler.
				if (state->eventHandler != NULL) {
					(*state->eventHandler)((caerVisualizerPublicState) state, displayEvent);
				}
			}
		}
	}
	else if (displayEvent.type == ALLEGRO_EVENT_MOUSE_AXES || displayEvent.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN
		|| displayEvent.type == ALLEGRO_EVENT_MOUSE_BUTTON_UP || displayEvent.type == ALLEGRO_EVENT_MOUSE_ENTER_DISPLAY
		|| displayEvent.type == ALLEGRO_EVENT_MOUSE_LEAVE_DISPLAY || displayEvent.type == ALLEGRO_EVENT_MOUSE_WARPED) {
		// React to mouse movements, but only if they came from the corresponding display.
		if (displayEvent.mouse.display == state->displayWindow) {
			if (displayEvent.type == ALLEGRO_EVENT_MOUSE_AXES && displayEvent.mouse.dz > 0) {
				float currentZoomFactor = sshsNodeGetFloat(state->parentModule->moduleNode, "zoomFactor");

				currentZoomFactor += (0.1f * (float) displayEvent.mouse.dz);

				// Clip zoom factor.
				if (currentZoomFactor > 50) {
					currentZoomFactor = 50;
				}

				sshsNodePutFloat(state->parentModule->moduleNode, "zoomFactor", currentZoomFactor);
			}
			else if (displayEvent.type == ALLEGRO_EVENT_MOUSE_AXES && displayEvent.mouse.dz < 0) {
				float currentZoomFactor = sshsNodeGetFloat(state->parentModule->moduleNode, "zoomFactor");

				// Plus because dz is negative, so - and - is +.
				currentZoomFactor += (0.1f * (float) displayEvent.mouse.dz);

				// Clip zoom factor.
				if (currentZoomFactor < 0.5f) {
					currentZoomFactor = 0.5f;
				}

				sshsNodePutFloat(state->parentModule->moduleNode, "zoomFactor", currentZoomFactor);
			}
			else {
				// Forward event to user-defined event handler.
				if (state->eventHandler != NULL) {
					(*state->eventHandler)((caerVisualizerPublicState) state, displayEvent);
				}
			}
		}
	}

	if (!al_is_event_queue_empty(state->displayEventQueue)) {
		// Handle all events before rendering, to avoid
		// having them backed up too much.
		goto handleEvents;
	}

	// Handle display resize (zoom).
	if (state->displayWindowResize.load(std::memory_order_relaxed)) {
		state->displayWindowResize.store(false);

		// Update statistics flag and resize display appropriately.
		updateDisplaySize(state, true);
	}

	// Render content to display.
	if (redraw && state->bitmapDrawUpdate) {
		state->bitmapDrawUpdate = false;

		al_set_target_backbuffer(state->displayWindow);
		al_clear_to_color(al_map_rgb(0, 0, 0));

		// Render statistics string.
		bool doStatistics = (state->showStatistics && state->displayFont != NULL);

		if (doStatistics) {
			// Split statistics string in two to use less horizontal space.
			al_draw_text(state->displayFont, al_map_rgb(255, 255, 255), GLOBAL_FONT_SPACING,
			GLOBAL_FONT_SPACING, 0, state->packetStatistics.currentStatisticsStringTotal);

			al_draw_text(state->displayFont, al_map_rgb(255, 255, 255), GLOBAL_FONT_SPACING,
				(2 * GLOBAL_FONT_SPACING) + GLOBAL_FONT_SIZE, 0, state->packetStatistics.currentStatisticsStringValid);
		}

		// Blit bitmap to screen.
		al_draw_bitmap(state->bitmapRenderer, 0, (doStatistics) ? ((float) STATISTICS_HEIGHT) : (0), 0);

		al_flip_display();
	}
}

static void caerVisualizerExitGraphics(caerVisualizerState state) {
	al_set_target_bitmap(NULL);

	if (state->bitmapRenderer != NULL) {
		al_destroy_bitmap(state->bitmapRenderer);
		state->bitmapRenderer = NULL;
	}

	if (state->displayFont != NULL) {
		al_destroy_font(state->displayFont);
		state->displayFont = NULL;
	}

	// Destroy event queue first to ensure all sources get
	// unregistered before being destroyed in turn.
	if (state->displayEventQueue != NULL) {
		al_destroy_event_queue(state->displayEventQueue);
		state->displayEventQueue = NULL;
	}

	if (state->displayTimer != NULL) {
		al_destroy_timer(state->displayTimer);
		state->displayTimer = NULL;
	}

	if (state->displayWindow != NULL) {
		al_destroy_display(state->displayWindow);
		state->displayWindow = NULL;
	}

}

static int caerVisualizerRenderThread(void *visualizerState) {
	if (visualizerState == NULL) {
		return (thrd_error);
	}

	caerVisualizerState state = visualizerState;

	// Set thread name to AllegroGraphics, so that the internal Allegro
	// threads do get a generic, recognizable name, if any are
	// created when initializing the graphics sub-system.
	thrd_set_name("AllegroGraphics");

	if (!caerVisualizerInitGraphics(state)) {
		return (thrd_error);
	}

	// Set thread name.
	thrd_set_name(state->parentModule->moduleSubSystemString);

	while (state->running.load(std::memory_order_relaxed)) {
		caerVisualizerUpdateScreen(state);
	}

	caerVisualizerExitGraphics(state);

	return (thrd_success);
}

// InitSize is deferred and called from Run, because we need actual packets.
static bool caerVisualizerModuleInit(caerModuleData moduleData);
static bool caerVisualizerModuleInitSize(caerModuleData moduleData, int16_t *inputs, size_t inputsSize);
static void caerVisualizerModuleRun(caerModuleData moduleData, caerEventPacketContainer in,
	caerEventPacketContainer *out);
static void caerVisualizerModuleExit(caerModuleData moduleData);
static void caerVisualizerModuleReset(caerModuleData moduleData, int16_t resetCallSourceID);

static const struct caer_module_functions VisualizerFunctions = { .moduleInit = &caerVisualizerModuleInit, .moduleRun =
	&caerVisualizerModuleRun, .moduleConfig = NULL, .moduleExit = &caerVisualizerModuleExit, .moduleReset =
	&caerVisualizerModuleReset };

static const struct caer_event_stream_in VisualizerInputs[] = { { .type = -1, .number = -1, .readOnly = true } };

static const struct caer_module_info VisualizerInfo = { .version = 1, .name = "Visualizer", .description =
	"Visualize data in various forms.", .type = CAER_MODULE_OUTPUT, .memSize = 0, .functions = &VisualizerFunctions,
	.inputStreams = VisualizerInputs, .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(VisualizerInputs), .outputStreams =
		NULL, .outputStreamsSize = 0, };

caerModuleInfo caerModuleGetInfo(void) {
	return (&VisualizerInfo);
}

static bool caerVisualizerModuleInit(caerModuleData moduleData) {
	// Wait for input to be ready. All inputs, once they are up and running, will
	// have a valid sourceInfo node to query, especially if dealing with data.
	size_t inputsSize;
	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, &inputsSize);
	if (inputs == NULL) {
		return (false);
	}

	sshsNodeCreateString(moduleData->moduleNode, "renderer", "Polarity", 0, 100, SSHS_FLAGS_NORMAL,
		"Renderer to use to generate content.");
	sshsNodeRemoveAttribute(moduleData->moduleNode, "rendererListOptions", SSHS_STRING);
	sshsNodeCreateString(moduleData->moduleNode, "rendererListOptions", caerVisualizerRendererListOptionsString, 0, 200,
		SSHS_FLAGS_READ_ONLY, "List of available renderers.");
	sshsNodeCreateString(moduleData->moduleNode, "eventHandler", "None", 0, 100, SSHS_FLAGS_NORMAL,
		"Event handlers to handle mouse and keyboard events.");
	sshsNodeRemoveAttribute(moduleData->moduleNode, "eventHandlerListOptions", SSHS_STRING);
	sshsNodeCreateString(moduleData->moduleNode, "eventHandlerListOptions", caerVisualizerHandlerListOptionsString, 0,
		200, SSHS_FLAGS_READ_ONLY, "List of available event handlers.");

	// Initialize visualizer. Needs information from a packet (the source ID)!
	if (!caerVisualizerModuleInitSize(moduleData, inputs, inputsSize)) {
		return (false);
	}

	return (true);
}

static bool caerVisualizerModuleInitSize(caerModuleData moduleData, int16_t *inputs, size_t inputsSize) {
	// Default sizes if nothing else is specified in sourceInfo node.
	int16_t sizeX = 20;
	int16_t sizeY = 20;
	int16_t sourceID = -1;

	// Search for biggest sizes amongst all event packets.
	for (size_t i = 0; i < inputsSize; i++) {
		// Get size information from source.
		sourceID = inputs[i];

		sshsNode sourceInfoNode = caerMainloopGetSourceInfo(sourceID);
		if (sourceInfoNode == NULL) {
			return (false);
		}

		// Default sizes if nothing else is specified in sourceInfo node.
		int16_t packetSizeX = 0;
		int16_t packetSizeY = 0;

		// Get sizes from sourceInfo node. visualizer prefix takes precedence,
		// for APS and DVS images, alternative prefixes are provided, as well
		// as for generic data visualization.
		if (sshsNodeAttributeExists(sourceInfoNode, "visualizerSizeX", SSHS_SHORT)) {
			packetSizeX = sshsNodeGetShort(sourceInfoNode, "visualizerSizeX");
			packetSizeY = sshsNodeGetShort(sourceInfoNode, "visualizerSizeY");
		}
		else if (sshsNodeAttributeExists(sourceInfoNode, "dataSizeX", SSHS_SHORT)) {
			packetSizeX = sshsNodeGetShort(sourceInfoNode, "dataSizeX");
			packetSizeY = sshsNodeGetShort(sourceInfoNode, "dataSizeY");
		}

		if (packetSizeX > sizeX) {
			sizeX = packetSizeX;
		}

		if (packetSizeY > sizeY) {
			sizeY = packetSizeY;
		}
	}

	// Search for renderer in list.
	caerVisualizerRenderer renderer = NULL;

	char *rendererChoice = sshsNodeGetString(moduleData->moduleNode, "renderer");

	for (size_t i = 0; i < (sizeof(caerVisualizerRendererList) / sizeof(struct caer_visualizer_renderers)); i++) {
		if (strcmp(rendererChoice, caerVisualizerRendererList[i].name) == 0) {
			renderer = caerVisualizerRendererList[i].renderer;
			break;
		}
	}

	free(rendererChoice);

	// Search for event handler in list.
	caerVisualizerEventHandler eventHandler = NULL;

	char *eventHandlerChoice = sshsNodeGetString(moduleData->moduleNode, "eventHandler");

	for (size_t i = 0; i < (sizeof(caerVisualizerHandlerList) / sizeof(struct caer_visualizer_handlers)); i++) {
		if (strcmp(eventHandlerChoice, caerVisualizerHandlerList[i].name) == 0) {
			eventHandler = caerVisualizerHandlerList[i].handler;
			break;
		}
	}

	free(eventHandlerChoice);

	moduleData->moduleState = caerVisualizerInit(renderer, eventHandler, sizeX, sizeY, VISUALIZER_DEFAULT_ZOOM, true,
		moduleData, sourceID);
	if (moduleData->moduleState == NULL) {
		return (false);
	}

	return (true);
}

static void caerVisualizerModuleExit(caerModuleData moduleData) {
	// Shut down rendering.
	caerVisualizerExit(moduleData->moduleState);
	moduleData->moduleState = NULL;
}

static void caerVisualizerModuleReset(caerModuleData moduleData, int16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	// Reset counters for statistics on reset.
	caerVisualizerReset(moduleData->moduleState);
}

static void caerVisualizerModuleRun(caerModuleData moduleData, caerEventPacketContainer in,
	caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(out);

	// Without a packet container with events, we cannot initialize or render anything.
	if (in == NULL || caerEventPacketContainerGetEventsNumber(in) == 0) {
		return;
	}

	// Render given packet container.
	caerVisualizerUpdate(moduleData->moduleState, in);
}
