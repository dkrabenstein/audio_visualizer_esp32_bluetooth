#include <Arduino.h>
#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include "dsps_fft2r.h"
#include <FastLED.h>

// --- LED Configuration ---
#define LED_PIN     2          // GPIO pin connected to LED data line
#define NUM_LEDS    300        // Number of LEDs on your strip
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
CRGB leds[NUM_LEDS];

// --- Audio & FFT Configuration ---
#define SAMPLE_RATE     44100   // A2DP standard sampling rate
#define SAMPLES         512     // Power of 2 buffer sizing

#define BASS_BIN_START 1
#define BASS_BIN_END   3

uint8_t targetBrightness = 0;
uint8_t currentBrightness = 0;
uint8_t hueCycle = 0; // Tracks the slowly shifting background color wheel

// Double-buffered arrays to completely prevent multi-core audio data corruption
float vReal_Input[SAMPLES * 2] __attribute__((aligned(16)));       // Core 0 writes here
float vReal_Processing[SAMPLES * 2] __attribute__((aligned(16)));  // Core 1 reads here

BluetoothA2DPSink a2dp_sink;

// Spectrum Visualizer Mapping
#define START_BIN 2    // Ignore DC offset and sub-bass rumble
#define END_BIN   60   // Max frequency to visualize (60 * 86 Hz ≈ 5.1 kHz)
#define NUM_BINS  (END_BIN - START_BIN + 1)

volatile bool newAudioDataAvailable = false;

// Callback function triggered immediately when Bluetooth audio data arrives (Core 0)
void audio_data_callback(const uint8_t *data, uint32_t length) {
    if (!newAudioDataAvailable) {
        int16_t *samples = (int16_t *)data;
        uint32_t sample_count = length / 4; // 4 bytes per stereo sample (2 L, 2 R)
        
        for (uint32_t i = 0; i < SAMPLES; i++) {
            if (i < sample_count) {
                vReal_Input[i * 2]     = (float)samples[i * 2]; // Real part
                vReal_Input[i * 2 + 1] = 0.0f;                  // Imaginary part
            } else {
                vReal_Input[i * 2]     = 0.0f;                  // Padding
                vReal_Input[i * 2 + 1] = 0.0f;
            }
        }
        newAudioDataAvailable = true; 
    }
}

void setup() {
    Serial.begin(115200);

    // Initialize FastLED strip configuration
    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(255); 
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();

    esp_err_t ret = dsps_fft2r_init_fc32(NULL, SAMPLES * 2);
    if (ret != ESP_OK) {
        Serial.println("Failed to initialize FFT!");
    }

    Serial.println("Starting Bluetooth Multi-Color Spectrum Visualizer...");
    a2dp_sink.set_stream_reader(audio_data_callback, false); 
    a2dp_sink.start("ESP32-Spectrum-Analyzer");
}

void loop() {
    // Noise gates and scaling parameters
    // Change these two lines inside your loop() function:
    const double bassThresholdMin = 10000.0;   // Lowered from 100000.0
    const double bassThresholdMax = 300000.0;  // Lowered from 1500000.0

    if (newAudioDataAvailable) {
        newAudioDataAvailable = false; // Immediately let the Bluetooth core handle the next incoming audio block

        // 1. Snapshot copy the audio buffer instantly to fix the multi-core race condition
        memcpy(vReal_Processing, vReal_Input, sizeof(vReal_Input));
        
        // 2. Run the hardware-accelerated FFT math
        dsps_fft2r_fc32(vReal_Processing, SAMPLES);
        dsps_bit_rev_fc32(vReal_Processing, SAMPLES);

        double bassEnergy = 0.0;
        for (int bin = BASS_BIN_START; bin <= BASS_BIN_END; bin++) {
            int i = bin * 2; // This targets index 2 (Bin 1 Real) and index 4 (Bin 2 Real)
            float real = vReal_Processing[i];
            float imag = vReal_Processing[i + 1];
            float amplitude = sqrtf(real * real + imag * imag) * 2.0f;
            bassEnergy += amplitude;
        }

        Serial.println(bassEnergy);

        const double bassThresholdMin = 50.0; 
        const double bassThresholdMax = 20000.0;

        if (bassEnergy > bassThresholdMin) {
            // Map the magnitude of the bass directly to a brightness scale (0 to 255)
            double mapped = ((bassEnergy - bassThresholdMin) / (bassThresholdMax - bassThresholdMin)) * 255.0;
            targetBrightness = (uint8_t)constrain(mapped, 0, 255);
        } else {
            targetBrightness = 20;
        }

        if (targetBrightness > currentBrightness) {
            currentBrightness = targetBrightness; // Instant snap up on a hard hit/beat
        } else {
            if(currentBrightness >= 24)
                currentBrightness -= 4; 
            else
                currentBrightness = 20;
        }
        
        fill_solid(leds, NUM_LEDS, CHSV(hueCycle, 255, currentBrightness));
        FastLED.show();
    }

    EVERY_N_MILLISECONDS(50) { 
                hueCycle++; 
            }

    delay(1);
}