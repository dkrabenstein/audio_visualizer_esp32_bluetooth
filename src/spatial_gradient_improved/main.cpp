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
    // Dynamic Gain Tracking (auto-adjusts to quiet or loud music)
    static float dynamicMax = 500000.0f;

    if (newAudioDataAvailable) {
        
        // 1. Snapshot copy the audio buffer
        memcpy(vReal_Processing, vReal_Input, sizeof(vReal_Input));
        newAudioDataAvailable = false; 

        // 2. Hardware FFT
        dsps_fft2r_fc32(vReal_Processing, SAMPLES);
        dsps_bit_rev_fc32(vReal_Processing, SAMPLES);

        float currentFrameMax = 0.0f;

        // 3. Map LEDs using Logarithmic Bin Grouping
        for (int i = 0; i < NUM_LEDS; i++) {
            
            // Logarithmic mapping: lower LEDs get tight bass bins, higher LEDs group wider treble bins
            float normVal = (float)i / (float)(NUM_LEDS - 1);
            float logVal = powf(normVal, 2.0f); // Exponential curve for human ear perception
            
            int binStart = START_BIN + (int)(logVal * (NUM_BINS - 2));
            int binEnd = START_BIN + (int)(powf((float)(i + 1) / NUM_LEDS, 2.0f) * (NUM_BINS - 2));
            if (binEnd <= binStart) binEnd = binStart + 1;

            // Average the magnitude across the bin group for this LED
            float binSum = 0.0f;
            int count = 0;
            for (int b = binStart; b < binEnd && b <= END_BIN; b++) {
                int binOffset = b * 2;
                float real = vReal_Processing[binOffset];
                float imag = vReal_Processing[binOffset + 1];
                binSum += sqrtf(real * real + imag * imag) * 2.0f;
                count++;
            }
            
            float amplitude = (count > 0) ? (binSum / count) : 0.0f;
            if (amplitude > currentFrameMax) currentFrameMax = amplitude;

            // --- Noise Gate & Brightness Scaling (Floor = 0) ---
            uint8_t intensity = 0; // Default floor is 0 (off)

            const float noiseFloor = 40000.0f; // Ignore quiet background noise
            if (amplitude > noiseFloor) {
                float normalized = (amplitude - noiseFloor) / (dynamicMax - noiseFloor);
                intensity = (uint8_t)constrain(normalized * 255.0f, 0.0f, 255.0f);
            }

            // Smooth decay: retrieve current light level
            uint8_t oldVal = leds[i].getAverageLight(); 
            if (intensity < oldVal) {
                // Smoothly fade down toward 0 instead of stopping at an ambient floor
                intensity = (oldVal > 15) ? (oldVal - 15) : 0;
            }

            // Assign spatial color gradient (Red -> Green -> Blue)
            uint8_t hue = map(i, 0, NUM_LEDS - 1, 0, 190);

            if (intensity > 0) {
                leds[i] = CHSV(hue, 255, intensity);
            } else {
                leds[i] = CRGB::Black; // Completely turn off silent LEDs
            }
        }

        // Auto-Gain Adjustment: Slowly decay dynamic max down, or jump up on audio peaks
        if (currentFrameMax > dynamicMax) {
            dynamicMax = currentFrameMax; // Instant peak tracking
        } else {
            dynamicMax *= 0.995f; // Gradual decay over time
            if (dynamicMax < 100000.0f) dynamicMax = 100000.0f; // Minimum ceiling
        }

        // 4. Push frame to physical strip
        FastLED.show();
    }

    delay(1); 
}