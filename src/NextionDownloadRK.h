#ifndef __NEXTIONDOWNLOADRK_H
#define __NEXTIONDOWNLOADRK_H

#include "Particle.h"


class NextionDownloadBuffer {
public:
	NextionDownloadBuffer();
	virtual ~NextionDownloadBuffer();

	static const size_t BUFFER_SIZE = 4096; // This size is part of the Nextion protocol and can't really be changed
	char buffer[BUFFER_SIZE];
	size_t bufferOffset = 0;
	size_t bufferSize = 0;
	size_t dataOffset = 0;
};


class NextionDownload {
public:
	/**
	 * eepromLocation is the location to store the download modification timestamp.
	 * It must point to 32 available bytes.
	 */
	NextionDownload(USARTSerial &serial, int eepromLocation);
	virtual ~NextionDownload();

	NextionDownload &withHostname(const char *hostname) { this->hostname = hostname; return *this; }
	NextionDownload &withPort(int port) { this->port = port; return *this; }
	NextionDownload &withPathPartOfUrl(const char *pathPartOfUrl) { this->pathPartOfUrl = pathPartOfUrl; return *this; }

	NextionDownload &withCheckModeManual() { checkMode = CHECK_MODE_MANUAL; return *this; }
	NextionDownload &withCheckModeAtBoot() { checkMode = CHECK_MODE_AT_BOOT; return *this; }

	NextionDownload &withForceDownload() { forceDownload = true; return *this; }

	NextionDownload &withRetryOnFailure() { retryOnFailure = true; return *this; }


	void setup();

	void loop();

	bool networkReady();

	void requestCheck(bool forceDownload = false);

	bool testDisplay();

	size_t readData(char *buf, size_t bufSize, uint32_t timeoutMs, bool exitAfter05);

	bool readAndDiscard(uint32_t timeoutMs, bool exitAfter05);

	void readAvailableAndDiscard();

	void sendCommand(const char *fmt, ...);

	bool tryBaud(int baud);

	bool findBaud();

	bool startDownload();

	bool getIsDone() const { return isDone; }

	bool getHasRun() const { return hasRun; }


	static const unsigned long RETRY_WAIT_TIME_MS = 30000;
	static const unsigned long DATA_TIMEOUT_TIME_MS = 60000;
	static const size_t EEPROM_BUFFER_SIZE = 32;

	// Check mode constants
	static const int CHECK_MODE_AT_BOOT = 0;
	static const int CHECK_MODE_MANUAL = 1;


protected:

	// State handlers
	void startState(void);
	void waitConnectState(void);
	void headerWaitState(void);
	void dataWaitState(void);
	void restartWaitState(void);
	void retryWaitState(void);
	void freeBuffers(void);
	void cleanupState(void);
	void doneState(void);

	// Settings
	USARTSerial &serial;
	int eepromLocation;
	String hostname;
	int port = 80;
	String pathPartOfUrl;
	int checkMode = CHECK_MODE_AT_BOOT;
	bool forceDownload = false;
	int downloadBaud = 115200;
	bool retryOnFailure = false;
	unsigned long restartWaitTime = 4000;

	// Misc stuff
	TCPClient client;

	// Only two of these will ever be allocated at the same time
	NextionDownloadBuffer *downloadBuffer = 0;
	NextionDownloadBuffer *serialWaitBuffer = 0;
	NextionDownloadBuffer *serialBuffer = 0;
	NextionDownloadBuffer *freeBuffer = 0;

	size_t dataOffset;
	size_t dataSize;
	bool hasRun = false;
	bool isDone = false;


	// State handler stuff
	std::function<void(NextionDownload&)> stateHandler = &NextionDownload::startState;
	unsigned long stateTime = 0;

};



#endif /* __NEXTIONDOWNLOADRK_H */
