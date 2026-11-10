# RPi PicoW Orientation app

### Display data obtained from a BNO085 9-DoF sensor, using the rpi-harness bench



### Notes

This project makes use of the rpi-harness Skill, found in E:\\Projects\\ClaudeSkills\\rpi-harness  
Files referenced herein without a full path are found in the same folder as this document



### Hardware

A harness (hereinafter "RpiHarness") that controls the PicoW, as described in the rpi-harness Skill
A RaspberryPi PicoW board (hereinafter "PicoW"); see E:\\Projects\\Devices\\RPiPicoW\\RpiPicoW.md
A Raspberry Pi Debug Probe (hereinafter "DebugProbe"); see E:\\Projects\\Devices\\RPiPicoDebugProbe\\DebugProbe.md
A Waveshare128 display (hereinafter "WS128") connected to the PicoW; see E:\\Projects\\Devices\\Waveshare128\\WS128.md
A BNO085 9-DoF sensor (hereinafter "BNO085") connected to the PicoW; see E:\\Projects\\Devices\\BNO08x-9DoF\\BNO08x.md



### Connections

Connections between WS128 and the PicoW are listed in the file WS128ToPicoWPins.md
Connections between BNO085 and the PicoW are listed in the file BNO085ToPicoWPins.md
Other connections are described in the file PicoWToRPiHarness.md





### Tasks



#### 1\. Verify references

Check that all file references within this document are valid, and that the referenced files provide useful information



#### 2\. Verify connections

Establish communication with the PicoW via the RpiHarness
Verify proper connections between PicoW and WS128
Verify proper connections between PicoW and BNO085



#### 3\. Create a program that samples and displays data

Create a program for the PicoW, that:

* 4 times per second, reads angular velocity for X, Y, and Z axes from the BNO085
* For each axis, computes a running average of angular velocity over the previous second
* Once per second, displays on the WS128 these average angular velocities (in degrees per second)
* X-axis value is labeled "Pitch"; Y-axis value is labeled "Roll" ; Z-axis value is labeled "Yaw"

Flash the PicoW with this firmware, and run it
Task is complete when the WS128 continually displays the specified information

Note: This task was successfully completed on 07-Nov-2026



#### 4\. Enhance the program created in Task 3

Read quaternion rotation vectors from BNO085
Use these vectors to calculate Pitch, Roll and Yaw angles
Display these angles (in degrees) in place of the angular velocities of Task 3
Monitor angular velocity for X, Y, and Z axes at 2 Hz
If the absolute values of the angular velocities of all three axes remain below 1 deg per second for at least 5 seconds:

* Initialize Pitch, Roll and Yaw angles to zero
* Suppress further initialization events until the angular velocity around any axis exceeds 1 deg per second



Note: This task was successfully completed on 09-Nov-2026
