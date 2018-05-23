#include "NextionDownloadRK.h"


// https://www.itead.cc/blog/nextion-hmi-upload-protocol

// For debugging purposes, it's handy to be able to verify the download part vs. the serial part if you're
// getting checksum errors. Use md5 to generate the md5sum for your .tft file on your server, then enable
// this to compare the two.
//#define USE_MD5 1


NextionDownload::NextionDownload(USARTSerial &serial, int eepromLocation) : serial(serial), eepromLocation(eepromLocation)  {
}

NextionDownload::~NextionDownload() {

}

void NextionDownload::setup() {
	delay(4000);
	tryBaud(9600);
}


void NextionDownload::loop() {
	if (stateHandler != NULL) {
		stateHandler(*this);
	}
}

bool NextionDownload::testDisplay() {

	return findBaud();
}



size_t NextionDownload::readData(char *buf, size_t bufSize, uint32_t timeoutMs, bool exitAfter05) {
	unsigned long startMs = millis();
	size_t count = 0;

	while(millis() - startMs < timeoutMs) {
		int c = serial.read();
		if (c != -1) {
			if (buf && count < bufSize) {
				buf[count] = (char) c;
			}
			count++;

			if (exitAfter05 && c == 0x05) {
				break;
			}
		}
	}
	// Make sure buf is null-terminated since we use strstr on it
	if (count < (bufSize -1)) {
		buf[count] = 0;
	}
	else {
		buf[bufSize - 1] = 0;
	}

	return count;
}

bool NextionDownload::readAndDiscard(uint32_t timeoutMs, bool exitAfter05) {
	unsigned long startMs = millis();
	bool have05 = false;

	while(millis() - startMs < timeoutMs) {
		int c = serial.read();
		if (c != -1) {
			if (c == 0x05) {
				have05 = true;
				if (exitAfter05) {
					break;
				}

			}
		}
	}

	return have05;
}

void NextionDownload::readAvailableAndDiscard() {
	while(serial.available()) {
		(void) serial.read();
	}
}

void NextionDownload::sendCommand(const char *fmt, ...) {
	readAvailableAndDiscard();

	char buf[64];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	serial.write(buf);

	serial.write(0xff);
	serial.write(0xff);
	serial.write(0xff);
}

bool NextionDownload::tryBaud(int baud) {
	serial.begin(baud);

	sendCommand("");
	sendCommand("connect");

	// Read
	char buf[128];
	readData(buf, sizeof(buf), 100, false);

	bool result = strstr(buf, "comok") != 0;

	Log.info("tryBaud %d: %d", baud, result);

	return result;
}

bool NextionDownload::findBaud() {
	static const int bauds[] = {9600,115200,19200,57600,38400,4800,2400};
	bool result = false;

	for(size_t ii = 0; ii < sizeof(bauds)/sizeof(bauds[0]); ii++) {
		result = tryBaud(bauds[ii]);
		if (result) {
			break;
		}
	}

	if (!result) {
		// Reset to 9600 if not found
		serial.begin(9600);
	}
	return result;
}

bool NextionDownload::startDownload() {

	Log.info("start download dataSize=%d downloadBaud=%d", dataSize, downloadBaud);

	sendCommand("");
	sendCommand("whmi-wri %d,%d,0", dataSize, downloadBaud);
	delay(50);
	serial.begin(downloadBaud);

#if USE_MD5
	MD5_Init(&md5_ctx);
#endif


	return readAndDiscard(500, false);
}


bool NextionDownload::networkReady() {
#if Wiring_WiFi
	return WiFi.ready();
#elif Wiring_Cellular
	return Cellular.ready();
#else
	return false;
#endif
}


void NextionDownload::startState(void) {
	if (checkMode == CHECK_MODE_AT_BOOT) {
		stateHandler = &NextionDownload::waitConnectState;
	}
	else {
		stateHandler = &NextionDownload::doneState;
	}
}
void NextionDownload::waitConnectState(void) {
	// This is basically WiFi.ready() or Cellular.ready() depending
	if (networkReady()) {
		// We only get here when using checkMode == CHECK_MODE_AT_BOOT and network is ready

		requestCheck(forceDownload);
	}
}

void NextionDownload::requestCheck(bool forceDownload /* = false */) {
	this->forceDownload = forceDownload;

	isDone = false;
	hasRun = false;

	// Allocate new buffers
	freeBuffers();
	downloadBuffer = new NextionDownloadBuffer();
	freeBuffer = new NextionDownloadBuffer();


	// If we get this far, once we get to done state we can assume that we probably downloaded
	// firmware, or we gave up
	hasRun = true;

	// Make sure display can be found
	if (!findBaud()) {
		Log.info("could not detect display");
		stateHandler = &NextionDownload::cleanupState;
		return;
	}

	// Connect to server by TCP
	if (client.connect(hostname, port)) {
		// Connected by TCP

		char ifModifiedSince[64];
		ifModifiedSince[0] = 0;

		char eepromBuffer[EEPROM_BUFFER_SIZE];
		EEPROM.get(eepromLocation, eepromBuffer);
		if (forceDownload) {
			Log.info("forceDownload");
		}
		else
		if (eepromBuffer[0] != 0xff) {
			snprintf(ifModifiedSince, sizeof(ifModifiedSince), "If-Modified-Since: %s GMT\r\n", eepromBuffer);

			Log.info("If-Modified-Since %s", eepromBuffer);
		}
		else {
			Log.info("no last modification date");
		}

		// Send request header
		size_t count = snprintf(downloadBuffer->buffer, NextionDownloadBuffer::BUFFER_SIZE,
				"GET %s HTTP/1.1\r\n"
				"Host: %s\r\n"
				"%s"
				"Connection: close\r\n"
				"\r\n",
				pathPartOfUrl.c_str(),
				hostname.c_str(),
				ifModifiedSince
				);

		client.write((const uint8_t *)downloadBuffer->buffer, count);

		Log.info("sent request to %s:%d", hostname.c_str(), port);
		stateTime = millis();
		stateHandler = &NextionDownload::headerWaitState;
	}
	else {
		Log.info("failed to connect to %s:%d", hostname.c_str(), port);
		stateTime = millis();
		stateHandler = &NextionDownload::retryWaitState;
	}

}

void NextionDownload::headerWaitState(void) {
	if (!client.connected()) {
		Log.info("server disconnected unexpectedly - headerWaitState");
		stateTime = millis();
		stateHandler = &NextionDownload::retryWaitState;
		return;
	}
	if (millis() - stateTime >= DATA_TIMEOUT_TIME_MS) {
		Log.info("timed out waiting for response header");
		client.stop();

		stateTime = millis();
		stateHandler = &NextionDownload::retryWaitState;
		return;
	}

	// Read some data
	int count = client.read((uint8_t *)&downloadBuffer->buffer[downloadBuffer->bufferOffset], NextionDownloadBuffer::BUFFER_SIZE - downloadBuffer->bufferOffset);
	if (count > 0) {
		Log.info("bufferOffset=%d count=%d", downloadBuffer->bufferOffset, count);

		downloadBuffer->bufferOffset += count;
		downloadBuffer->buffer[downloadBuffer->bufferOffset] = 0;

		char *end = strstr(downloadBuffer->buffer, "\r\n\r\n");
		if (end != NULL) {
			// Have a complete response header
			*end = 0;

			// Check status code, namely 200 (OK), 304 (not modified) or any other error
			{
				int code = 0;

				char *cp = strchr(downloadBuffer->buffer, ' ');
				if (cp) {
					cp++;
					code = atoi(cp);
				}
				Log.info("http response code=%d", code);

				if (code == 304) {
					Log.info("file not modified, not downloading again");
					stateHandler = &NextionDownload::cleanupState;
					return;
				}

				if (code != 200) {
					Log.info("not an OK response, was %d", code);
					stateHandler = &NextionDownload::cleanupState;
					return;
				}
			}


			{
				// Note the data from the Last-Modified header
				// Last-Modified: <day-name>, <day> <month> <year> <hour>:<minute>:<second> GMT
				// Last-Modified: Wed, 21 Oct 2015 07:28:00 GMT
				char *cp = strstr(downloadBuffer->buffer, "Last-Modified:");
				if (cp) {
					cp += 14; // length of "Last-Modified:"

					// Skip the space after the Last-Modified
					if (*cp == ' ') {
						cp++;
					}

					char eepromBuffer[EEPROM_BUFFER_SIZE];
					size_t ii = 0;
					while(*cp != '\r' && ii < (EEPROM_BUFFER_SIZE - 1)) {
						eepromBuffer[ii++] = *cp++;
					}
					eepromBuffer[ii] = 0;

					Log.info("last modified: %s", eepromBuffer);
					EEPROM.put(eepromLocation, eepromBuffer);
				}
			}

			// Note the data size from the Content-Length. This is required as the Nextion protocol requires
			// the length before sending segments and we don't have enough RAM to buffer it first.
			{
				char *cp = strstr(downloadBuffer->buffer, "Content-Length:");
				if (cp) {
					cp += 15; // length of "Content-Length:"
					while(*cp == ' ') {
						cp++;
					}
					dataSize = atoi(cp);
				}
			}
			if (dataSize == 0) {
				Log.info("unable to get length of data");
				stateHandler = &NextionDownload::cleanupState;
				return;
			}


			dataOffset = 0;

			// Send the request to start downloading to the display
			if (!startDownload()) {
				Log.info("display did not acknowledge download start");
				stateHandler = &NextionDownload::cleanupState;
				return;
			}

			Log.info("downloading %d bytes", dataSize);

			// Discard the header
			end += 4; // the \r\n\r\n part

			size_t newLength = downloadBuffer->bufferOffset - (end - downloadBuffer->buffer);
			if (newLength > 0) {
				memmove(downloadBuffer->buffer, end, newLength);
			}
			downloadBuffer->bufferOffset = newLength;
			downloadBuffer->bufferSize = NextionDownloadBuffer::BUFFER_SIZE;
			if (downloadBuffer->bufferSize > dataSize) {
				downloadBuffer->bufferSize = dataSize;
			}
			downloadBuffer->dataOffset = 0;


			stateTime = millis();
			stateHandler = &NextionDownload::dataWaitState;
		}
	}

}

void NextionDownload::dataWaitState(void) {
	if (downloadBuffer && !client.connected()) {
		Log.info("server disconnected unexpectedly - dataWaitState");
		stateTime = millis();
		stateHandler = &NextionDownload::retryWaitState;
		return;
	}
	if (millis() - stateTime >= DATA_TIMEOUT_TIME_MS) {
		Log.info("timed out waiting for data - dataWaitState");
		client.stop();

		stateTime = millis();
		stateHandler = &NextionDownload::retryWaitState;
		return;
	}

	if (downloadBuffer == 0 && serialWaitBuffer == 0 && freeBuffer != 0) {
		// Start using a new download buffer
		downloadBuffer = freeBuffer;
		freeBuffer = 0;

		// This is the amount of data that's left to read from the server
		downloadBuffer->dataOffset = dataOffset;
		size_t dataLeft = dataSize - dataOffset;
		if (dataLeft > NextionDownloadBuffer::BUFFER_SIZE) {
			dataLeft = NextionDownloadBuffer::BUFFER_SIZE;
		}

		if (dataLeft > 0) {
			// This part takes into account that we may have partial data in buf already (bufferOffset bytes)
			downloadBuffer->bufferOffset = 0;

			downloadBuffer->bufferSize = dataLeft;
			if (downloadBuffer->bufferSize > NextionDownloadBuffer::BUFFER_SIZE) {
				downloadBuffer->bufferSize = NextionDownloadBuffer::BUFFER_SIZE;
			}

			Log.info("reading new buffer at dataOffset=%d bufferSize=%d", dataOffset, downloadBuffer->bufferSize);
		}
		else {
			// Read all data, free the buffer
			delete downloadBuffer;
			downloadBuffer = 0;
		}
	}

	if (downloadBuffer) {
		size_t reqSize = downloadBuffer->bufferSize - downloadBuffer->bufferOffset;
		//Log.info("bufferOffset=%d reqSize=%d", downloadBuffer->bufferOffset, dataLeft, reqSize);

		int count = client.read((uint8_t *)&downloadBuffer->buffer[downloadBuffer->bufferOffset], reqSize);
		if (count > 0) {
			downloadBuffer->bufferOffset += count;
			stateTime = millis();

			if (downloadBuffer->bufferOffset == downloadBuffer->bufferSize) {
				// Got a whole buffer of data (or last partial buffer), send to display

#if USE_MD5
				MD5_Update(&md5_ctx, downloadBuffer->buffer, downloadBuffer->bufferOffset);
#endif

				dataOffset += downloadBuffer->bufferOffset;

				downloadBuffer->bufferOffset = 0;
				serialWaitBuffer = downloadBuffer;
				downloadBuffer = 0;
			}
		}
	}

	// Do we have sendWait buffer data to promote to sending?
	if (serialWaitBuffer && serialBuffer == 0) {
		serialBuffer = serialWaitBuffer;
		serialWaitBuffer = 0;
		Log.info("sending dataOffset=%d, bufferSize=%d to serial", serialBuffer->dataOffset, serialBuffer->bufferSize);
	}

	// Do we have serial data to send?
	if (serialBuffer) {
		size_t count = serialBuffer->bufferSize - serialBuffer->bufferOffset;

		size_t spaceLeft = serial.availableForWrite();
		if (count > spaceLeft) {
			count = spaceLeft;
		}

		serial.write((const uint8_t *)&serialBuffer->buffer[serialBuffer->bufferOffset], count);
		serialBuffer->bufferOffset += count;

		if (serialBuffer->bufferOffset >= serialBuffer->bufferSize) {

			// Wait for the display to acknowledge
			if (!readAndDiscard(500, true)) {
				Log.info("display did not acknowledge block");
				stateHandler = &NextionDownload::cleanupState;
				return;
			}

			if ((serialBuffer->dataOffset + serialBuffer->bufferOffset) >= dataSize) {
				Log.info("successfully downloaded");

#if USE_MD5
				unsigned char out[16];
				char str[34];

				MD5_Final(out, &md5_ctx);
				for(size_t ii = 0; ii < sizeof(out); ii++) {
					sprintf(&str[ii * 2], "%02x", out[ii]);
				}
				str[32] = 0;
				Log.info("md5 hash=%s", str);
#endif

				delete serialBuffer;
				serialBuffer = 0;

				// Wait a few seconds for the display to restart
				stateHandler = &NextionDownload::restartWaitState;
				stateTime = millis();
			}
			else {
				// Allow this buffer to be filled again
				freeBuffer = serialBuffer;
				serialBuffer = 0;
			}
		}
		else {
			// Still writing to display
		}

	}
}

void NextionDownload::restartWaitState(void) {
	if (millis() - stateTime >= restartWaitTime) {
		// Reset the baud rate
		findBaud();

		stateHandler = &NextionDownload::cleanupState;
	}
}

void NextionDownload::retryWaitState(void) {
	if (!retryOnFailure) {
		// Not retrying on failure (default), so just clean up
		stateHandler = &NextionDownload::cleanupState;
		return;
	}

	if (millis() - stateTime >= RETRY_WAIT_TIME_MS) {
		stateHandler = &NextionDownload::waitConnectState;
	}
}

void NextionDownload::freeBuffers(void) {
	if (downloadBuffer) {
		delete downloadBuffer;
		downloadBuffer = 0 ;
	}
	if (serialWaitBuffer) {
		delete serialWaitBuffer;
		serialWaitBuffer = 0;
	}
	if (serialBuffer) {
		delete serialBuffer;
		serialBuffer = 0;
	}
	if (freeBuffer) {
		delete freeBuffer;
		freeBuffer = 0;
	}
}

void NextionDownload::cleanupState(void) {
	Log.info("cleanupState");
	freeBuffers();

	client.stop();

	stateHandler = &NextionDownload::doneState;
}


void NextionDownload::doneState(void) {
	if (!isDone) {
		Log.info("done");
		isDone = true;
	}
}


NextionDownloadBuffer::NextionDownloadBuffer() {
}

NextionDownloadBuffer::~NextionDownloadBuffer() {

}



