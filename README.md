# audio_visualizer_esp32_bluetooth

Overview
A RGB LED strip that is controlled by the Fourier spectrum of music.

System
Lyrion music server is used to send Bluetooth audio to the ESP-32 and to a speaker simultaniously. Lyrion music server is hosted locally on a windows laptop and then broadcasts to multiple devices running squeezebox. For this setup the devices running squeezebox are a windows laptop and an iphone. The windows laptop streams to a speaker and the iphone streams to the ESP-32. The ESP-32 then controls the LED strips.

Hardware
- ELEGOO ESP32 type C development board https://manuals.plus/asin/B0D8T7LZF2
- WS2812B LED strip
- 1000 uF capaccitor
- 470 Ω resistor

The ESP-32 data pins are used to control the WS2812B LED strip. A decoupling capacitor is used to prevent voltage spikes and a resistor is used to protect the data line. The circuit schematic for this project is based on the schematic provided at https://esp32io.com/tutorials/esp32-ws2812b-led-strip. The diagram can be seen below:
![circuit diagram](images/esp32-ws2812b-led-strip-wiring-diagram.jpg)

Due too the lack of resistors available at the required values, two resistors are placed in series and two capacitors are placed in parallel. The breadboard configuration is shown below:
![breadboard](images/breadboard.jpg)

The breadboard integrated with the LED strip can be seen below:
![system](images/system.jpg)

Software
The ESP-32 is flashed with a Platform IO script using the Aurdiono architecture. The ESP-32 processing the bluetooth signal using an audio data callback function. The function converts the bluetooth data into a usable format and uses a double-buffered handoff flag to ensure data is not overwritten. The audio data is then proceesed using a fast forier transform to convert it to the frequency domain. This allows frequency bins to be mapped to RGB pixel values. Multiple different algorithms can be used to do this:

Spatial Gradient
The frequency axis is spaciely mapped to the LED strip. The start of the strip being low frequencies maped to red transitioning the end of the strip being high frequencies mapped to blue. The colors stay the same while the amplitude of each frequency bin is mapped to the intensity of LED.

Spatial Gradient Improved
Instead of using linear mapping of frequency bins this algorithm using logrithmic mapping. This simulates how the human ear regognizes sound. Additonally, instead of mapping one frequency bin value directly to an LED, this algorithm takes an average of multiple bins. This algorithm also uses past volumes to ajust the noise thresholds. 

Flash with Bass Energy
This algorithm ignores higher frequency ranges relies soley on the bass amplitude. The bass amplitude is mapped to a brightness value causing the lights to flash with the bass. Base is used instead of higher frequencies because it's more periodic. The lights cycle through a repeating color pattern unaffected by the audio input.

Dominant Frequency Mapping
This algorithm finds the frequency bin with the highest magnitude. This bin index is then mapped to the hue of the lights.

A demonstration of the flash with base algorithm can be seen below:
https://github.com/user-attachments/assets/1921ffde-b979-4599-bade-37820933f76e



