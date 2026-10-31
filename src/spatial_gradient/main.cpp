#include <Arduino.h>
#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include "dsps_fft2r.h"
#include <FastLED.h>

// --- LED Configuration ---
#define LED_PIN     4          // GPIO pin connected to LED data line
#define NUM_LEDS    50        // Number of LEDs on your strip
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
CRGB leds[NUM_LEDS];

// --- Audio & FFT Configuration ---
#define SAMPLE_RATE     44100   // A2DP standard sampling rate
#define SAMPLES         512     // Power of 2 buffer sizing

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
    const float minThreshold = 80000.0f;
    const float maxThreshold = 1800000.0f;

    if (newAudioDataAvailable) {
        
        // 1. Snapshot copy the audio buffer instantly to fix the multi-core race condition
        memcpy(vReal_Processing, vReal_Input, sizeof(vReal_Input));
        newAudioDataAvailable = false; // Immediately let the Bluetooth core handle the next incoming audio block

        // 2. Run the hardware-accelerated FFT math
        dsps_fft2r_fc32(vReal_Processing, SAMPLES);
        dsps_bit_rev_fc32(vReal_Processing, SAMPLES);

        // 3. Map physical LEDs directly to frequency magnitudes in a single loop
        for (int i = 0; i < NUM_LEDS; i++) {
            // Figure out which frequency bin corresponds to this physical LED
            int binIndex = map(i, 0, NUM_LEDS - 1, 0, NUM_BINS - 1);
            int binOffset = (binIndex + START_BIN) * 2;
            
            float real = vReal_Processing[binOffset];
            float imag = vReal_Processing[binOffset + 1];
            
            // Calculate true volume amplitude directly for this LED
            float amplitude = sqrtf(real * real + imag * imag) * 2.0f;

            // Apply noise gate
            if (amplitude < minThreshold) {
                amplitude = 0.0f;
            }

            // Scale to raw 0-255 brightness values
            float mappedVal = ((amplitude - minThreshold) / (maxThreshold - minThreshold)) * 255.0f;
            uint8_t intensity = (uint8_t)constrain(mappedVal, 25.0f, 255.0f);

            // Smoothly decay individual LEDs down to prevent flickering
            uint8_t oldVal = leds[i].g; 
            if (intensity < oldVal) {
                // If the new volume dropped off a cliff, only drop the LED brightness down a tiny bit
                intensity = max(25, oldVal - 15); 
            }

            // Assign the spatial gradient color across your strip length
            uint8_t hue = map(i, 0, NUM_LEDS - 1, 0, 190);
            leds[i] = CHSV(hue, 255, intensity);
        }

        // 4. Push the updated frame to your physical strip
        FastLED.show();
    }

    delay(1); 
}