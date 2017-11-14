#ifndef __NEXTIONDOWNLOADRK_H
#define __NEXTIONDOWNLOADRK_H

#include "Particle.h"

///class NextionDownload; // forward declaration

///typedef void (NextionDownload::*StateHandler)(void);

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

	static const size_t BUFFER_SIZE = 4096; // This size is part of the Nextion protocol and can't really be changed
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
	void retryWaitState(void);
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

	// Misc stuff
	TCPClient client;
	char *buffer = 0;
	size_t bufferOffset;
	size_t bufferSize;
	size_t dataOffset;
	size_t dataSize;

	// State handler stuff
	std::function<void(NextionDownload&)> stateHandler = &NextionDownload::startState;
	unsigned long stateTime = 0;

};



#endif /* __NEXTIONDOWNLOADRK_H */
