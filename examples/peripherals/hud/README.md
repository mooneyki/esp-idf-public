# PROJECT: HUD

This example uses GPIO and timer interrupts, I2C, and WiFi to implement a type of HUD.

## Functionality Overview

* Two timers are configured
* The first timer determines the polling rate for the GPIO interrupt 
* The second timer is used to determine vehicle speed and also to determine when to end the program. 
* WiFi is used to control the start of data recording. 



