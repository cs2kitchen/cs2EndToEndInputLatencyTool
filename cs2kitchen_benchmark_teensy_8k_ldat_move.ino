#include <Mouse.h>

// High–level constants for the sensor and LED pins ,no need for interrupt mode
static const int sensorPin = A0;
static const int ledPin    = LED_BUILTIN;

// Measurement parameters default is around 400 (low luminace is voltage off)
static const int luminanceThreshold = 800;
static const int trialCount        = 1000;

// Fixed movement units.  for a single HID report (it maxes out around 127 and can bleed into multiple polls)
static const int moveUnits = 100;
static const int leftMove  = -moveUnits;
static const int rightMove = moveUnits;

// just added as reminder so I never forget
static_assert(moveUnits >= 1 && moveUnits <= 127, "moveUnits must be within 1..127");

// Timing parameters
static const uint32_t detectionTimeoutUs = 50000;
static const int settleAfterResetMs  = 225;
static const int resetDelayMs        = 125;

// Compute a running average of analog readings
int readAverage(int count) {
    long sum = 0;
    for (int i = 0; i < count; ++i) {
        sum += analogRead(sensorPin);
    }
    return sum / count;
}

// Send a fixed left move for measurement
void sendLeftMove() {
    Mouse.move(leftMove, 0, 0);
}

// Send a fixed right move for reset
void sendRightMove() {
    Mouse.move(rightMove, 0, 0);
}

// Run a single latency trial
bool runTrial(
    int idx,
    int &baseline,
    int &peak,
    uint32_t &latencyUs,
    uint32_t &callDurationUs,
    uint32_t &samples,
    const char* &status) {

    status  = "OK";
    samples = 0;

    // Establish baseline and ensure we start in the dark
    //to do in future need to add the setpos and setang to make it easier
    baseline = readAverage(32);
    if (baseline >= luminanceThreshold) {
        status    = "START_NOT_DARK";
        peak      = baseline;
        latencyUs = 0;
        callDurationUs = 0;
        sendRightMove();
        delay(settleAfterResetMs);
        return false;
    }

    // Insert a short randomized delay before the test move
    // i know its just a pseudorandom but it is good enough to do our main job that is get off sync
    delayMicroseconds(random(1000, 16000));

    bool crossed = false;
    peak         = 0;
    latencyUs    = 0;

    digitalWriteFast(ledPin, HIGH);

    uint32_t startTime = micros();
    sendLeftMove();
    callDurationUs = micros() - startTime;

    // Poll the photodiode as fast as possible until timeout or threshold cross
    while ((micros() - startTime) < detectionTimeoutUs) {
        int value = analogRead(sensorPin);
        ++samples;
        if (value >= luminanceThreshold) {
            crossed  = true;
            peak     = value;
            latencyUs = micros() - startTime;
            break;
        }
    }

    digitalWriteFast(ledPin, LOW);

    if (!crossed) {
        status = "NO_THRESHOLD_CROSS";
    }

    delay(resetDelayMs);
    sendRightMove();
    delay(settleAfterResetMs);

    return crossed;
}
//we can change resolution but at this scale (1000 samples) this is already in the error margin of 0.1 ms
void setup() {
    pinMode(ledPin, OUTPUT);
    pinMode(sensorPin, INPUT_PULLDOWN);
    Serial.begin(115200);
    analogReadResolution(12);
    analogReadAveraging(1);
    Mouse.begin();

    // Wait up to 15 seconds for the serial console
    uint32_t waitStart = millis();
    while (!Serial && millis() - waitStart < 15000) {
        digitalWrite(ledPin, !digitalRead(ledPin));
        delay(150);
    }
    digitalWrite(ledPin, LOW);
    randomSeed(analogRead(A1) ^ micros());

    // Intro text with explanation, remove if you don't like but I need it for videos
    Serial.println();
    Serial.println("====================================");
    Serial.println("HID(mouseMove) to PHOTON LATENCY TEST");
    Serial.println("Measured movement = fixed LEFT");
    Serial.println("Reset movement = fixed RIGHT");
    Serial.println("Direction never depends on luminance.");
    Serial.println("Luminance only decides valid/crossed.");
    Serial.println("Photodiode is read as fast as possible.");
    Serial.println("8kHz polling rate");
    Serial.println("Tab into CS2. Starts in 10 sec.");
    Serial.println("====================================");

    //timer to tab into game
    //on my local machine I will use this timer to connect my serial to a visualiser via pyserial
    for (int i = 10; i > 0; --i) {
        Serial.print("TEST STARTING IN ");
        Serial.println(i);
        delay(1000);
    }

    Serial.println();
    Serial.println("CONFIG");
    Serial.print("TRIALS: ");
    Serial.println(trialCount);
    Serial.print("moveUnits: ");
    Serial.println(moveUnits);
    Serial.print("leftMove: ");
    Serial.println(leftMove);
    Serial.print("rightMove: ");
    Serial.println(rightMove);
    Serial.print("luminanceThreshold: ");
    Serial.println(luminanceThreshold);

    Serial.println();
    Serial.println("CSV_START");
    Serial.println("trial,valid,reason,start_baseline,cross_value,latency_us,latency_ms,move_call_done_us,adc_reads");

    int validSamples = 0;
    uint32_t minLatency = 0xFFFFFFFF;
    uint32_t maxLatency = 0;
    double sumLatency   = 0.0;

    //running loop
    for (int trial = 1; trial <= trialCount; ++trial) {
        int baseline        = 0;
        int peak            = 0;
        uint32_t latencyUs  = 0;
        uint32_t callDuration = 0;
        uint32_t reads       = 0;
        const char* reason   = "OK";

        bool valid = runTrial(trial, baseline, peak, latencyUs, callDuration, reads, reason);

//inputs  to draw the graph in realtime without affecting adc reads with very slight delay
        Serial.print(trial);
        Serial.print(",");
        Serial.print(valid ? 1 : 0);
        Serial.print(",");
        Serial.print(reason);
        Serial.print(",");
        Serial.print(baseline);
        Serial.print(",");
        Serial.print(peak);
        Serial.print(",");
        Serial.print(latencyUs);
        Serial.print(",");
        Serial.print(latencyUs / 1000.0, 3);
        Serial.print(",");
        Serial.print(callDuration);
        Serial.print(",");
        Serial.println(reads);
        Serial.flush();

        if (valid) {
            ++validSamples;
            if (latencyUs < minLatency) minLatency = latencyUs;
            if (latencyUs > maxLatency) maxLatency = latencyUs;
            sumLatency += latencyUs;
        }
    }

    Serial.println("CSV_END");

    Serial.println();
    Serial.println("====================================");
    Serial.println("SUMMARY");
    Serial.println("====================================");

    Serial.print("VALID_SAMPLES: ");
    Serial.println(validSamples);
    Serial.print("INVALID_SAMPLES: ");
    Serial.println(trialCount - validSamples);

    if (validSamples > 0) {
        double avgLatencyUs = sumLatency / validSamples;
        Serial.print("MIN_LATENCY_MS: ");
        Serial.println(minLatency / 1000.0, 3);
        Serial.print("AVG_LATENCY_MS: ");
        Serial.println(avgLatencyUs / 1000.0, 3);
        Serial.print("MAX_LATENCY_MS: ");
        Serial.println(maxLatency / 1000.0, 3);
    } else {
        Serial.println("NO_VALID_SAMPLES");
    }

    Serial.println("====================================");
    Serial.println("DONE.");
}

void loop() {}