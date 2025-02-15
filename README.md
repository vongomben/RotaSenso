# RotaSenso
RotaSenso: Feel the presence of someone afar. A connected rotary device that mirrors emotions and gestures in real-time. 🔄💙
The RotaSenso is also on [Hackster](https://www.hackster.io/vongomben/how-to-build-a-rotasenso-aka-connecting-with-your-sos-5eb97d)!

![](https://github.com/vongomben/RotaSenso/blob/main/gif/full.gif)

## Part list

The cost of this project is approximately 40 USD without shipping and fees, but you can heavily limit the cost by solder directly servo and potentiometer to your xiao (or go-to ESP32 board), based on your soldering skills. As you can see from the description this project is aimed to a nwcomer in the IoT world. 

1) [Seeed Studio XIAO ESP32S3](https://www.seeedstudio.com/XIAO-ESP32S3-p-5627.html) (USD 7.49) ×	2	
2) [Grove Shield for Seeeduino XIAO](https://www.seeedstudio.com/Grove-Shield-for-Seeeduino-XIAO-p-4621.html) (USD 3.90) × 2	
3) [Seeed Studio Grove Servo](https://www.seeedstudio.com/Grove-Servo.html) (5.90) ×	2	
4) [Seeed Studio Rotary Angle Sensor](https://www.seeedstudio.com/Grove-Rotary-Angle-Sensor.html) (USD 3.20) ×	2	


## Story
This project is a very humble tribute to a mania, a trend I've seen in the Iot-verse since its beginning.

(you came here to build the actual RotaSenso? skip to this symbol 🔄💙)

## A Little Bit of History
I think the first project going in this direction (aka connecting to enviromnments enabling the sharing of a feeling, or in this case the sleeping time was "[the Good Night Lamp](https://www.onnstudio.com/portfolio/good-night-lamp)" by Alexandra Des-champs Sonsino

![image](https://github.com/user-attachments/assets/19a3573c-9e0b-4e0c-b152-e5ef74359ad8)

The little lamps used to have cellular connection: I'm not sure it' still so.

We did something similar in Casa Jasmina: the [EmojiLamp](https://gitlab.com/officine-innesto/emoji-lamp) (2017). In this case we were using Telegram APIs to control the behaviour of a Ws2812 LED Strip.

![image](https://github.com/user-attachments/assets/bae7645e-b770-4fb1-b4db-3aa984b55b2b)


Later, during the Pandemic, I've seen a wonderful project that still make me smile: the [Yo-Yo Machines](https://www.yoyomachines.io/) from Interaction Research Studio (2020).

![image](https://github.com/user-attachments/assets/a23ea815-3086-4962-b0df-e32f5a2f4a44)

In this case, you could build yourself for different devices, out of paper and glue: The [Light Touch](https://www.yoyomachines.io/lighttouch), the [Speed Dial](https://www.yoyomachines.io/speed-dial), the [Knock Knock](https://www.yoyomachines.io/knock-knock) and the [Flutter By](https://www.yoyomachines.io/flutterby). they were using wifimanager as **Captive Portal** and **Socket.IO**.

## And then it's time for the RotaSenso!

Well, my project (still) doesn't have a Captive Portal (that would be amazing when we deliver the RotaSenso to somebody allowing adding an unknown Wifi Network: in my TODO list!

What RotaSenso doed is sharing the position of the potentiometer in a public or private MQTT channel (I've tested many, and many are free). The Other RotaSenso move the Servo in the correspondingo postion.

[Here is a little short explaining it!](https://www.youtube.com/shorts/WKS2UPSjkJc)

## Hardware Build

The hardware is very simple and doesn't need soldering. The Servo will be hooked up to pin DO/A0, the Potentiometer to pin A5/D5

![image](https://github.com/user-attachments/assets/ae14ce0b-ea54-4f68-a024-585a3d24bffc)


## Software Setup

1. Installing Arduino IDE and Required Libraries

* **Download and Install Arduino IDE**: Visit the Arduino website and download the latest version of the Arduino IDE suitable for your operating system. Install the software following the instructions provided on the website.
* **Install the ESP32 Board Package**: Open the Arduino IDE, go to File > Preferences, and add the following URL to the Additional Board Manager URLs field:
`https://dl.espressif.com/dl/package_esp32_index.json`
* Next, navigate to `Tools > Board > Boards Manager`, search for "ESP32, " and install the package.
* Add Required Libraries:Go to `Sketch > Include Library > Manage Libraries...`, and search for and install `PubSubClient` and `ESP32Servo`

2. Calibrate the positions

You will have to create two similar boards or little "things" to put the RotaSenso in. Feel free to amaze us. Once you are done you will need to calibrate the two RotaSensos in order to know where are the 8 numbers

![](https://github.com/vongomben/RotaSenso/blob/main/gif/calibra-01.gif)


Calibration.ino is the code for calibrate the devices, while Rotasenso-v01.ino is the firmware for both devices. 
Please remember to:

* Add your wifi and password!
* Change the MQTT Broker (line 14) with others (HiveMQ or EMQX are two of many) is that one is giving you problems
* the First will publish to `test/SV02` (line 113) and receive on `test/SV01` (line 51). The second RotaSenso will need to have these two variables in the opposite positions (be carefule about this!
* Increase the number of time the message is sent! On lin 110 I sent the data 3 times. You can increase this numeber in order to be mor sure the data was sent correctly. Possibly in the future we should sort out a way to have the two devices handshake.

![](https://github.com/vongomben/RotaSenso/blob/main/gif/calibrate-02.gif)

