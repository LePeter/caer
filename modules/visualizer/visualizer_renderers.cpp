#include "visualizer_renderers.hpp"

#include "ext/sfml/line.hpp"
#include "ext/sfml/helpers.hpp"

#include <libcaercpp/events/polarity.hpp>
#include <libcaercpp/events/frame.hpp>
#include <libcaercpp/events/imu6.hpp>
#include <libcaercpp/events/point2d.hpp>
#include <libcaercpp/events/point4d.hpp>
#include <libcaercpp/events/spike.hpp>
#include <libcaercpp/devices/dynapse.hpp> // Only for constants.

static bool caerVisualizerRendererPolarityEvents(caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererPolarityEvents("Polarity", &caerVisualizerRendererPolarityEvents);

static void *caerVisualizerRendererFrameEventsStateInit(caerVisualizerPublicState state);
static void caerVisualizerRendererFrameEventsStateExit(caerVisualizerPublicState state);
static bool caerVisualizerRendererFrameEvents(caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererFrameEvents("Frame", &caerVisualizerRendererFrameEvents, false, &caerVisualizerRendererFrameEventsStateInit, &caerVisualizerRendererFrameEventsStateExit);

static bool caerVisualizerRendererIMU6Events(caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererIMU6Events("IMU_6-axes", &caerVisualizerRendererIMU6Events);

static bool caerVisualizerRendererPoint2DEvents(caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererPoint2DEvents("2D_Points", &caerVisualizerRendererPoint2DEvents);

static bool caerVisualizerRendererSpikeEvents(caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererSpikeEvents("Spikes", &caerVisualizerRendererSpikeEvents);

static bool caerVisualizerRendererSpikeEventsRaster(caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererSpikeEventsRaster("Spikes_Raster_Plot", &caerVisualizerRendererSpikeEventsRaster);

static bool caerVisualizerRendererETF4D(caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererETF4D("ETF4D", &caerVisualizerRendererETF4D);

static bool caerVisualizerRendererPolarityAndFrameEvents(caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererPolarityAndFrameEvents("Polarity_and_Frames", &caerVisualizerRendererPolarityAndFrameEvents);

const std::string caerVisualizerRendererListOptionsString =
	"None,Polarity,Frame,IMU_6-axes,2D_Points,Spikes,Spikes_Raster_Plot,ETF4D,Polarity_and_Frames";

const struct caer_visualizer_renderer_info caerVisualizerRendererList[] = { { "None", nullptr }, rendererPolarityEvents,
	rendererFrameEvents, rendererIMU6Events, rendererPoint2DEvents, rendererSpikeEvents, rendererSpikeEventsRaster,
	rendererETF4D, rendererPolarityAndFrameEvents };

const size_t caerVisualizerRendererListLength = (sizeof(caerVisualizerRendererList) / sizeof(struct caer_visualizer_renderer_info));

static bool caerVisualizerRendererPolarityEvents(caerVisualizerPublicState state, caerEventPacketContainer container) {
	UNUSED_ARGUMENT(state);

	caerEventPacketHeader polarityPacketHeader = caerEventPacketContainerFindEventPacketByType(container, POLARITY_EVENT);

	// No packet of requested type or empty packet (no valid events).
	if (polarityPacketHeader == NULL || caerEventPacketHeaderGetEventValid(polarityPacketHeader) == 0) {
		return (false);
	}

	const libcaer::events::PolarityEventPacket polarityPacket(polarityPacketHeader, false);

	std::vector<sf::Vertex> vertices((size_t) polarityPacket.getEventValid() * 4);

	// Render all valid events.
	for (const auto &polarityEvent : polarityPacket) {
		if (!polarityEvent.isValid()) {
			continue; // Skip invalid events.
		}

		// ON polarity (green), OFF polarity (red).
		sfml::Helpers::addPixelVertices(vertices, sf::Vector2f(polarityEvent.getX(), polarityEvent.getY()), (polarityEvent.getPolarity()) ? (sf::Color::Green) : (sf::Color::Red));
	}

	state->renderWindow->draw(vertices.data(), vertices.size(), sf::Quads);

	return (true);
}

struct renderer_frame_events_state {
	sf::Sprite sprite;
	sf::Texture texture;
	std::vector<uint8_t> pixels;
};

typedef struct renderer_frame_events_state *rendererFrameEventsState;

static void *caerVisualizerRendererFrameEventsStateInit(caerVisualizerPublicState state) {
	// Allocate memory via C++ for renderer state, since we use C++ objects directly.
	rendererFrameEventsState renderState = new renderer_frame_events_state();

	// Create texture representing frame, set smoothing.
	renderState->texture.create(state->renderSizeX, state->renderSizeY);
	renderState->texture.setSmooth(true);

	// Assign texture to sprite.
	renderState->sprite.setTexture(renderState->texture);

	// 32-bit RGBA pixels (8-bit per channel), standard CG layout.
	renderState->pixels.reserve(state->renderSizeX * state->renderSizeY * 4);

	return (renderState);
}

static void caerVisualizerRendererFrameEventsStateExit(caerVisualizerPublicState state) {
	rendererFrameEventsState renderState = (rendererFrameEventsState) state->renderState;

	delete renderState;
}

static bool caerVisualizerRendererFrameEvents(caerVisualizerPublicState state, caerEventPacketContainer container) {
	UNUSED_ARGUMENT(state);

	caerEventPacketHeader framePacketHeader = caerEventPacketContainerFindEventPacketByType(container, FRAME_EVENT);

	// No packet of requested type or empty packet (no valid events).
	if (framePacketHeader == NULL || caerEventPacketHeaderGetEventValid(framePacketHeader) == 0) {
		return (false);
	}

	const libcaer::events::FrameEventPacket framePacket(framePacketHeader, false);

	// Render only the last, valid frame.
	auto rIter = framePacket.crbegin();
	while (rIter != framePacket.crend()) {
		if (rIter->isValid()) {
			break;
		}

		rIter++;
	}

	// Only operate on the last, valid frame. At least one must exist (see check above).
	const libcaer::events::FrameEvent &frameEvent = *rIter;

	rendererFrameEventsState renderState = (rendererFrameEventsState) state->renderState;

	// 32-bit RGBA pixels (8-bit per channel), standard CG layout.
	switch (frameEvent.getChannelNumber()) {
		case libcaer::events::FrameEvent::colorChannels::GRAYSCALE: {
			for (size_t srcIdx = 0, dstIdx = 0; srcIdx < frameEvent.getPixelsMaxIndex();) {
				uint8_t greyValue = frameEvent.getPixelArrayUnsafe()[srcIdx++] >> 8;
				renderState->pixels[dstIdx++] = greyValue; // R
				renderState->pixels[dstIdx++] = greyValue; // G
				renderState->pixels[dstIdx++] = greyValue; // B
				renderState->pixels[dstIdx++] = UINT8_MAX; // A
			}
			break;
		}

		case libcaer::events::FrameEvent::colorChannels::RGB: {
			for (size_t srcIdx = 0, dstIdx = 0; srcIdx < frameEvent.getPixelsMaxIndex();) {
				renderState->pixels[dstIdx++] = frameEvent.getPixelArrayUnsafe()[srcIdx++] >> 8; // R
				renderState->pixels[dstIdx++] = frameEvent.getPixelArrayUnsafe()[srcIdx++] >> 8; // G
				renderState->pixels[dstIdx++] = frameEvent.getPixelArrayUnsafe()[srcIdx++] >> 8; // B
				renderState->pixels[dstIdx++] = UINT8_MAX; // A
			}
			break;
		}

		case libcaer::events::FrameEvent::colorChannels::RGBA: {
			for (size_t srcIdx = 0, dstIdx = 0; srcIdx < frameEvent.getPixelsMaxIndex();) {
				renderState->pixels[dstIdx++] = frameEvent.getPixelArrayUnsafe()[srcIdx++] >> 8; // R
				renderState->pixels[dstIdx++] = frameEvent.getPixelArrayUnsafe()[srcIdx++] >> 8; // G
				renderState->pixels[dstIdx++] = frameEvent.getPixelArrayUnsafe()[srcIdx++] >> 8; // B
				renderState->pixels[dstIdx++] = frameEvent.getPixelArrayUnsafe()[srcIdx++] >> 8; // A
			}
			break;
		}
	}

	renderState->texture.update(renderState->pixels.data(), U32T(frameEvent.getLengthX()), U32T(frameEvent.getLengthY()),
		U32T(frameEvent.getPositionX()), U32T(frameEvent.getPositionY()));

	renderState->sprite.setTextureRect(sf::IntRect(frameEvent.getPositionX(), frameEvent.getPositionY(),
		frameEvent.getLengthX(), frameEvent.getLengthY()));
	renderState->sprite.setPosition(frameEvent.getPositionX(), frameEvent.getPositionY());

	state->renderWindow->draw(renderState->sprite);

	return (true);
}

#define RESET_LIMIT_POS(VAL, LIMIT) if ((VAL) > (LIMIT)) { (VAL) = (LIMIT); }
#define RESET_LIMIT_NEG(VAL, LIMIT) if ((VAL) < (LIMIT)) { (VAL) = (LIMIT); }

static bool caerVisualizerRendererIMU6Events(caerVisualizerPublicState state, caerEventPacketContainer container) {
	caerEventPacketHeader imu6PacketHeader = caerEventPacketContainerFindEventPacketByType(container, IMU6_EVENT);

	if (imu6PacketHeader == NULL || caerEventPacketHeaderGetEventValid(imu6PacketHeader) == 0) {
		return (false);
	}

	const libcaer::events::IMU6EventPacket imu6Packet(imu6PacketHeader, false);

	float scaleFactorAccel = 30;
	float scaleFactorGyro = 15;
	float lineThickness = 4;
	float maxSizeX = (float) state->renderSizeX;
	float maxSizeY = (float) state->renderSizeY;

	sf::Color accelColor = sf::Color::Green;
	sf::Color gyroColor = sf::Color::Magenta;

	float centerPointX = maxSizeX / 2;
	float centerPointY = maxSizeY / 2;

	float accelX = 0, accelY = 0, accelZ = 0;
	float gyroX = 0, gyroY = 0, gyroZ = 0;

	// Iterate over valid IMU events and average them.
	// This somewhat smoothes out the rendering.
	for (const auto &imu6Event : imu6Packet) {
		accelX += imu6Event.getAccelX();
		accelY += imu6Event.getAccelY();
		accelZ += imu6Event.getAccelZ();

		gyroX += imu6Event.getGyroX();
		gyroY += imu6Event.getGyroY();
		gyroZ += imu6Event.getGyroZ();
	}

	// Normalize values.
	int32_t validEvents = imu6Packet.getEventValid();

	accelX /= (float) validEvents;
	accelY /= (float) validEvents;
	accelZ /= (float) validEvents;

	gyroX /= (float) validEvents;
	gyroY /= (float) validEvents;
	gyroZ /= (float) validEvents;

	// Acceleration X, Y as lines. Z as a circle.
	float accelXScaled = centerPointX - accelX * scaleFactorAccel;
	RESET_LIMIT_POS(accelXScaled, maxSizeX - 2 - lineThickness);
	RESET_LIMIT_NEG(accelXScaled, 1 + lineThickness);
	float accelYScaled = centerPointY - accelY * scaleFactorAccel;
	RESET_LIMIT_POS(accelYScaled, maxSizeY - 2 - lineThickness);
	RESET_LIMIT_NEG(accelYScaled, 1 + lineThickness);
	float accelZScaled = fabsf(accelZ * scaleFactorAccel);
	RESET_LIMIT_POS(accelZScaled, centerPointY - 2 - lineThickness); // Circle max.
	RESET_LIMIT_NEG(accelZScaled, 1); // Circle min.

	sfml::Line accelLine(sf::Vector2f(centerPointX, centerPointY), sf::Vector2f(accelXScaled, accelYScaled), lineThickness, accelColor);
	state->renderWindow->draw(accelLine);

	sf::CircleShape accelCircle(accelZScaled);
	sfml::Helpers::setOriginToCenter(accelCircle);
	accelCircle.setFillColor(sf::Color::Transparent);
	accelCircle.setOutlineColor(accelColor);
	accelCircle.setOutlineThickness(-lineThickness);
	accelCircle.setPosition(sf::Vector2f(centerPointX, centerPointY));

	state->renderWindow->draw(accelCircle);

	// TODO: enhance IMU renderer with more text info.
	if (state->font != nullptr) {
		char valStr[128];
		snprintf(valStr, 128, "%.2f,%.2f g", (double) accelX, (double) accelY);

		sf::Text accelText(valStr, *state->font, 20);
		accelText.setFillColor(accelColor);
		accelText.setPosition(sf::Vector2f(accelXScaled, accelYScaled));

		state->renderWindow->draw(accelText);
	}

	// Gyroscope pitch(X), yaw(Y), roll(Z) as lines.
	float gyroXScaled = centerPointY + gyroX * scaleFactorGyro;
	RESET_LIMIT_POS(gyroXScaled, maxSizeY - 2 - lineThickness);
	RESET_LIMIT_NEG(gyroXScaled, 1 + lineThickness);
	float gyroYScaled = centerPointX + gyroY * scaleFactorGyro;
	RESET_LIMIT_POS(gyroYScaled, maxSizeX - 2 - lineThickness);
	RESET_LIMIT_NEG(gyroYScaled, 1 + lineThickness);
	float gyroZScaled = centerPointX - gyroZ * scaleFactorGyro;
	RESET_LIMIT_POS(gyroZScaled, maxSizeX - 2 - lineThickness);
	RESET_LIMIT_NEG(gyroZScaled, 1 + lineThickness);

	sfml::Line gyroLine1(sf::Vector2f(centerPointX, centerPointY), sf::Vector2f(gyroYScaled, gyroXScaled), lineThickness, gyroColor);
	state->renderWindow->draw(gyroLine1);

	sfml::Line gyroLine2(sf::Vector2f(centerPointX, centerPointY - 20), sf::Vector2f(gyroZScaled, centerPointY - 20), lineThickness, gyroColor);
	state->renderWindow->draw(gyroLine2);

	return (true);
}

static bool caerVisualizerRendererPoint2DEvents(caerVisualizerPublicState state, caerEventPacketContainer container) {
	UNUSED_ARGUMENT(state);

	caerEventPacketHeader point2DPacketHeader = caerEventPacketContainerFindEventPacketByType(container,
		POINT2D_EVENT);

	if (point2DPacketHeader == NULL || caerEventPacketHeaderGetEventValid(point2DPacketHeader) == 0) {
		return (false);
	}

	const libcaer::events::Point2DEventPacket point2DPacket(point2DPacketHeader, false);

	std::vector<sf::Vertex> vertices((size_t) point2DPacket.getEventValid() * 4);

	// Render all valid events.
	for (const auto &point2DEvent : point2DPacket) {
		if (!point2DEvent.isValid()) {
			continue; // Skip invalid events.
		}

		// Render points in color blue.
		sfml::Helpers::addPixelVertices(vertices, sf::Vector2f(point2DEvent.getX(), point2DEvent.getY()), sf::Color::Blue);
	}

	state->renderWindow->draw(vertices.data(), vertices.size(), sf::Quads);

	return (true);
}

static inline sf::Color dynapseCoreIdToColor(uint8_t coreId) {
	if (coreId == 3) {
		return (sf::Color::Yellow);
	}
	else if (coreId == 2) {
		return (sf::Color::Red);
	}
	else if (coreId == 1) {
		return (sf::Color::Blue);
	}

	// Core ID 0 has default.
	return (sf::Color::Green);
}

static bool caerVisualizerRendererSpikeEvents(caerVisualizerPublicState state, caerEventPacketContainer container) {
	UNUSED_ARGUMENT(state);

	caerEventPacketHeader spikePacketHeader = caerEventPacketContainerFindEventPacketByType(container, SPIKE_EVENT);

	if (spikePacketHeader == NULL || caerEventPacketHeaderGetEventValid(spikePacketHeader) == 0) {
		return (false);
	}

	const libcaer::events::SpikeEventPacket spikePacket(spikePacketHeader, false);

	std::vector<sf::Vertex> vertices((size_t) spikePacket.getEventValid() * 4);

	// Render all valid events.
	for (const auto &spikeEvent : spikePacket) {
		if (!spikeEvent.isValid()) {
			continue; // Skip invalid events.
		}

		// Render spikes with different colors based on core ID.
		uint8_t coreId = spikeEvent.getSourceCoreID();
		sfml::Helpers::addPixelVertices(vertices, sf::Vector2f(spikeEvent.getX(), spikeEvent.getY()), dynapseCoreIdToColor(coreId));
	}

	state->renderWindow->draw(vertices.data(), vertices.size(), sf::Quads);

	return (true);
}

static bool caerVisualizerRendererSpikeEventsRaster(caerVisualizerPublicState state, caerEventPacketContainer container) {
	UNUSED_ARGUMENT(state);

	caerEventPacketHeader spikePacketHeader = caerEventPacketContainerFindEventPacketByType(container,
		SPIKE_EVENT);

	if (spikePacketHeader == NULL || caerEventPacketHeaderGetEventValid(spikePacketHeader) == 0) {
		return (false);
	}

	const libcaer::events::SpikeEventPacket spikePacket(spikePacketHeader, false);

	// get bitmap's size
	uint32_t sizeX = state->renderSizeX;
	uint32_t sizeY = state->renderSizeY;

	// find max and min TS, event packets MUST be ordered by time, that's
	// an invariant property, so we can just select first and last event.
	// Also time is always positive, so we can use unsigned ints.
	uint32_t minTimestamp = U32T(spikePacket[0].getTimestamp());
	uint32_t maxTimestamp = U32T(spikePacket[-1].getTimestamp());

	// time span, +1 to divide space correctly in scaleX.
	uint32_t timeSpan = maxTimestamp - minTimestamp + 1;

	float scaleX = 0.0;
	if (timeSpan > 0) {
		scaleX = ((float) (sizeX / 2)) / ((float) timeSpan); // two rasterplots in x
	}
	float scaleY = ((float) (sizeY / 2)) / ((float) DYNAPSE_CONFIG_NUMNEURONS); // two rasterplots in y

	std::vector<sf::Vertex> vertices((size_t) spikePacket.getEventNumber() * 4);

	// Render all spikes.
	for (const auto &spikeEvent : spikePacket) {
		uint32_t ts = U32T(spikeEvent.getTimestamp());
		ts = ts - minTimestamp;

		// X is based on time.
		uint32_t plotX = U32T(floorf((float) ts * scaleX));

		uint8_t coreId = spikeEvent.getSourceCoreID();

		uint32_t linearIndex = spikeEvent.getNeuronID();
		linearIndex += (coreId * DYNAPSE_CONFIG_NUMNEURONS_CORE);

		// Y is based on all neurons.
		uint32_t plotY = U32T(floorf((float) linearIndex * scaleY));

		// Move plot X/Y based on chip ID, to get four quadrants with four chips.
		uint8_t chipId = spikeEvent.getChipID();

		if (chipId == DYNAPSE_CONFIG_DYNAPSE_U3) {
			plotX += (sizeX / 2);
			plotY += (sizeY / 2);
		}
		else if (chipId == DYNAPSE_CONFIG_DYNAPSE_U2) {
			plotY += (sizeY / 2);
		}
		else if (chipId == DYNAPSE_CONFIG_DYNAPSE_U1) {
			plotX += (sizeX / 2);
		}
		// DYNAPSE_CONFIG_DYNAPSE_U0 no changes.

		// Draw pixels of raster plot (neurons might be merged due to aliasing). TODO: increase size, no scale?
		sfml::Helpers::addPixelVertices(vertices, sf::Vector2f(plotX, plotY), dynapseCoreIdToColor(coreId));
	}

	state->renderWindow->draw(vertices.data(), vertices.size(), sf::Quads);

	// Draw middle borders, only once! TODO: this eats up a pixel +/-.
	sfml::Line horizontalBorderLine(sf::Vector2f(0, sizeY / 2), sf::Vector2f(sizeX - 1, sizeY / 2), 2, sf::Color::White);
	state->renderWindow->draw(horizontalBorderLine);

	sfml::Line verticalBorderLine(sf::Vector2f(sizeX / 2, 0), sf::Vector2f(sizeX / 2, sizeY - 1), 2, sf::Color::White);
	state->renderWindow->draw(verticalBorderLine);

	return (true);
}

static bool caerVisualizerRendererETF4D(caerVisualizerPublicState state, caerEventPacketContainer container) {
	UNUSED_ARGUMENT(state);

	caerEventPacketHeader Point4DEventPacketHeader = caerEventPacketContainerFindEventPacketByType(container,
		POINT4D_EVENT);
	if (Point4DEventPacketHeader == NULL || caerEventPacketHeaderGetEventValid(Point4DEventPacketHeader) == 0) {
		return (false);
	}

	// get bitmap's size
	int32_t sizeX = state->renderSizeX;
	int32_t sizeY = state->renderSizeY;

	float maxY = INT32_MIN;

	CAER_POINT4D_ITERATOR_VALID_START((caerPoint4DEventPacket) Point4DEventPacketHeader)
		float mean = caerPoint4DEventGetZ(caerPoint4DIteratorElement);
		if (maxY < mean) {
			maxY = mean;
		}
	CAER_POINT4D_ITERATOR_ALL_END

	float scaley = ((float) sizeY) / maxY; // two rasterplots in x
	float scalex = ((float) sizeX) / 5;

	int counter = 0;
	CAER_POINT4D_ITERATOR_VALID_START((caerPoint4DEventPacket) Point4DEventPacketHeader)
		float corex = caerPoint4DEventGetX(caerPoint4DIteratorElement);
		float corey = caerPoint4DEventGetY(caerPoint4DIteratorElement);
		float mean = caerPoint4DEventGetZ(caerPoint4DIteratorElement);

		//int coreid = (int) corex * 1 + (int) corey;	// color

		double range_check = floor( (double)mean * (double)scaley);
		int32_t new_y = 0;
		if(range_check < INT32_MAX && range_check > INT32_MIN ){
			new_y = (int32_t) range_check;
		}

		range_check = round((double)counter*(double)scalex);
		int32_t checked = 0;
		if(range_check < INT32_MAX && range_check > INT32_MIN ){
			checked = (int32_t) range_check;
		}

		uint8_t coreId = 0;
		if(corex == 0.0f && corey == 0.0f){ coreId = 0;}
		if(corex == 0.0f && corey == 1.0f){ coreId = 1;}
		if(corex == 1.0f && corey == 0.0f){ coreId = 2;}
		if(corex == 1.0f && corey == 1.0f){ coreId = 3;}

		if (coreId == 0) {
			// TODO: al_put_pixel((int32_t) sizeX - checked, (int32_t) new_y,al_map_rgb(0, 255, 0));
		}
		else if (coreId == 1) {
			// TODO: al_put_pixel((int32_t) sizeX - checked, (int32_t) new_y,al_map_rgb(0, 0, 255));
		}
		else if (coreId == 2) {
			// TODO: al_put_pixel((int32_t) sizeX - checked, (int32_t) new_y,al_map_rgb(255, 0, 0));
		}
		else if (coreId == 3) {
			// TODO: al_put_pixel((int32_t) sizeX - checked, (int32_t) new_y,al_map_rgb(255, 255, 0));
		}

		if(counter == 5){
			counter = 0;
		}else{
			counter++;
		}
	CAER_POINT4D_ITERATOR_ALL_END

	return (true);
}

static bool caerVisualizerRendererPolarityAndFrameEvents(caerVisualizerPublicState state,
	caerEventPacketContainer container) {
	bool drewFrameEvents = caerVisualizerRendererFrameEvents(state, container);

	bool drewPolarityEvents = caerVisualizerRendererPolarityEvents(state, container);

	return (drewFrameEvents || drewPolarityEvents);
}
