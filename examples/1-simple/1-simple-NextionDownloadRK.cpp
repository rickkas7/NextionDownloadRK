#include "Particle.h"

#include "NextionDownloadRK.h"

SerialLogHandler logHandler(LOG_LEVEL_INFO);
NextionDownload display(Serial1, 0);

void setup() {
	Serial.begin(9600);

	display.withHostname("dev4.rickk.com").withPort(8080).withPathPartOfUrl("/CodeSync2/CompPicture_v0_32.tft");

	display.setup();
}

void loop() {
	display.loop();
}

