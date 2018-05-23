#include "Particle.h"

#include "Nextion.h"
#include "NextionDownloadRK.h"

SerialLogHandler logHandler(LOG_LEVEL_INFO);
NextionDownload displayDownload(Serial1, 0);
USARTSerial& nexSerial = Serial1;

// Declare a text object [page id:0,component id:2, component name: "t1"].
NexText t0 = NexText(0, 1, "t0");

void setup() {
	Serial.begin(9600);

	displayDownload.withHostname("download.example.com").withPort(8080).withPathPartOfUrl("2-text.tft");

	displayDownload.setup();

}

void loop() {
	displayDownload.loop();

	if (displayDownload.getHasRun() && displayDownload.getIsDone()) {
		static bool hasSet = false;

		if (!hasSet) {
			hasSet = true;
			Log.info("setting text");
			t0.setText("test 2");
		}
	}
}

