#include <Arduino.h>
#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include "dsps_fft2r.h"
#include <algorithm>
#define FASTLED_ALL_PINS_HARDWARE_SPI // Unlocks hardware clock channels
#include <FastLED.h>

// --- LED Configuration ---
#define LED_PIN     4          // GPIO pin connected to LED data line
#define NUM_LEDS    300
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
CRGB leds[NUM_LEDS];

// --- Audio & FFT Configuration ---
#define SAMPLE_RATE     44100   // A2DP standard sampling rate
#define SAMPLES         512     // Power of 2 buffer sizing
float vReal[SAMPLES * 2] __attribute__((aligned(16)));

float smoothedHue = 0.0f;

BluetoothA2DPSink a2dp_sink;

// Volatile tells the compiler this variable is shared across two CPU cores
volatile bool newAudioDataAvailable = false;

// Callback function triggered immediately when Bluetooth audio data arrives
void audio_data_callback(const uint8_t *data, uint32_t length) {
    // Only copy data if we aren't currently busy analyzing the last batch
    if (!newAudioDataAvailable) {
        int16_t *samples = (int16_t *)data;
        uint32_t sample_count = length / 4; // 4 bytes per stereo sample (2 L, 2 R)
        
        for (uint32_t i = 0; i < SAMPLES; i++) {
            if (i < sample_count) {
                vReal[i * 2]     = (float)samples[i * 2]; // Real part (Audio)
                vReal[i * 2 + 1] = 0.0f;                  // Imaginary part (0)
            } else {
                vReal[i * 2]     = 0.0f;                  // Padding
                vReal[i * 2 + 1] = 0.0f;
            }
        }
        
        // Tell the main loop that new data is ready for analysis!
        newAudioDataAvailable = true; 
    }
}

void setup() {
    Serial.begin(115200);

    // Initialize FastLED strip configuration
    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(255); // We can handle brightness per-LED now!
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();

    esp_err_t ret = dsps_fft2r_init_fc32(NULL, SAMPLES * 2);
    if (ret != ESP_OK) {
        Serial.println("Failed to initialize FFT!");
    }

    Serial.println("Starting Bluetooth Spectrum Visualizer...");
    a2dp_sink.set_stream_reader(audio_data_callback, false); 
    a2dp_sink.start("ESP32-Spectrum-Analyzer");
}

void loop() {

    if (newAudioDataAvailable) {
        
        float maxAmplitude = 0.0f;
        int peakBinIndex = 0;

        // 1. Run your standard FFT and bit-reversal
        dsps_fft2r_fc32(vReal, SAMPLES);
        dsps_bit_rev_fc32(vReal, SAMPLES);

        // 2. Clear out the complex interleaving.
        dsps_cplx2reC_fc32(vReal, SAMPLES);

        // We clear out Bin 0 and 1 to ignore DC offset and extreme sub-bass rumble
        vReal[0] = 0.0f;
        vReal[1] = 0.0f;

        // --- FIXED LOOP WITH FREQUENCY EQUALIZATION ---
        // We stop at bin 65 (~5.6 kHz) because that's where music actually lives.
        // Bins above that are usually empty high-end hiss.
        for (int i = 2; i < 65; i++) {
            
            // Equalization multiplier: The higher the bin index 'i', the more we boost it.
            // This allows treble notes to actually win against the heavy bass.
            float equalizationFactor = 1.0f + ((float)i * 0.15f); 
            float currentAmplitude = vReal[i] * equalizationFactor;

            if (currentAmplitude > maxAmplitude) {
                maxAmplitude = currentAmplitude;
                peakBinIndex = i;
            }
        }

        // Noise gate: If the whole room is dead silent, don't jump to crazy random colors
        if (maxAmplitude < 50000.0f) {
            peakBinIndex = 2; // Default to a low bass index when quiet
        }

        // Map your winning bin index to the 0-190 hue range
        // We change the upper bound from SAMPLES/2 to 65 to match our search loop ceiling!
        uint8_t hue = map(peakBinIndex, 2, 65, 0, 190);

        // Smoothly glide the color transitions
        smoothedHue += 0.1f * (hue - smoothedHue);
        uint8_t finalHue = (uint8_t)smoothedHue;

        // Flood the strip and push to hardware
        fill_solid(leds, NUM_LEDS, CHSV(finalHue, 255, 255));
        FastLED.show();
        
        newAudioDataAvailable = false;
    }

    delay(1); 
}