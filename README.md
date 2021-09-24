# ESP32BathroomScales
IoT Bathroom Scales made from ESP32, ePaper display, HX711 load cell

### Background
I had a set of bathroom scales, I also had some ESP32's, the rest is obvious.

### What it is
Modify a standard commercial set of scales to be controlled by an ESP32 so it can measure your weight, and send the value to an MQTT server.

Once the numbers are in the MQTT server, you can do with it whatever you want, NodeRed can help with this.

The scales wake up when you tap a touch sensor, then it calibrates and instructs you to stand on it. Once the weight measurement stabilises, it displays the weight on the epaper screen, and sends the value to MQTT, then it goes asleep.

If multiple people use it, and they have unique 'weight windows', ie, their normal weight is within a range that does not crossover another persons, then the scale can work out *who* you are based on your weight, and send the result to the correct MQTT publish string. Define these values in the configuration section at the top of the code. 

### Basic guide
Remove the back of the scales, and remove everything else inside, keeping only the load cells and their wiring, and if you have them, the wiring to any touch pads. (touch pads are originally to measure your body fat, allegedly, I highly doubt that feature does anything useful.)

Connect the load-cells to an HX711 amp according to instructions available all over the internet, and that's about it. As I had touch buttons on the face of the scales I used those to bring the ESP out of deep sleep.
