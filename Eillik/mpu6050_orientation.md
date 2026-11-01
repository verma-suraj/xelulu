# MPU6050 Orientation Datasheet
Version: 1.0
Status: VERIFIED
Project: Eilik Robot
MCU: ESP32 Dev Module (ESP32-WROOM-32)
Sensor: MPU6050
Interface: I2C
I2C Address: 0x68

------------------------------------------------------------
1. Hardware Configuration
------------------------------------------------------------

Sensor        : MPU6050
Communication : I2C
Address       : 0x68

ESP32 Wiring

SDA -> GPIO21
SCL -> GPIO22
VCC -> 3.3V
GND -> GND

------------------------------------------------------------
2. Sensor Orientation (Verified)
------------------------------------------------------------

The following orientation has been verified experimentally by
placing the robot in Flat, Front, Back, Left and Right positions.

Robot Coordinate Frame

                +Z
                 ↑
                 │
                 │
+Y (Left) ◄──────┼──────► -Y (Right)
                 │
                 │
                 ↓
               +X
             (Forward)

Definitions

+X = Robot Forward
-X = Robot Back

+Y = Robot Left
-Y = Robot Right

+Z = Robot Up
-Z = Robot Down

------------------------------------------------------------
3. MPU6050 Sensor Frame
------------------------------------------------------------

Raw MPU6050 Accelerometer

ax
ay
az

Raw MPU6050 Gyroscope

gx
gy
gz

------------------------------------------------------------
4. Verified Axis Mapping
------------------------------------------------------------

Robot Forward  (+X)
=
MPU6050 -Y

Robot Back (-X)
=
MPU6050 +Y

Robot Left (+Y)
=
MPU6050 +X

Robot Right (-Y)
=
MPU6050 -X

Robot Up (+Z)
=
MPU6050 +Z

Robot Down (-Z)
=
MPU6050 -Z

------------------------------------------------------------
5. Required Software Mapping
------------------------------------------------------------

Accelerometer

robotAccelX = -ay;
robotAccelY =  ax;
robotAccelZ =  az;

Gyroscope

robotGyroX = -gy;
robotGyroY =  gx;
robotGyroZ =  gz;

These mapped values SHALL be used everywhere.

DO NOT use raw MPU6050 axes directly.

------------------------------------------------------------
6. Flat Position Reference
------------------------------------------------------------

Robot Rest Position

Accel

X ≈ 0 g
Y ≈ 0 g
Z ≈ +1 g

Gyro

X ≈ 0 dps
Y ≈ 0 dps
Z ≈ 0 dps

Typical measured values

Accel

X = +0.023 g
Y = -0.007 g
Z = +1.015 g

Roll  ≈ 0°
Pitch ≈ 0°

------------------------------------------------------------
7. Motion Behaviour
------------------------------------------------------------

Robot tilted Forward

robotAccelX becomes Positive

Robot tilted Back

robotAccelX becomes Negative

Robot tilted Left

robotAccelY becomes Positive

Robot tilted Right

robotAccelY becomes Negative

Robot lifted upward

robotAccelZ remains Positive

------------------------------------------------------------
8. Software Rules
------------------------------------------------------------

ALL motion algorithms MUST use the Robot Coordinate Frame.

Forbidden

roll = function(ax, ay, az)

Correct

roll = function(robotAccelX,
                robotAccelY,
                robotAccelZ)

The mapping SHALL occur immediately after reading the sensor.

------------------------------------------------------------
9. Helper Function
------------------------------------------------------------

inline void mapMPU6050(
    float ax,
    float ay,
    float az,
    float gx,
    float gy,
    float gz,
    float &robotAccelX,
    float &robotAccelY,
    float &robotAccelZ,
    float &robotGyroX,
    float &robotGyroY,
    float &robotGyroZ)
{
    robotAccelX = -ay;
    robotAccelY =  ax;
    robotAccelZ =  az;

    robotGyroX = -gy;
    robotGyroY =  gx;
    robotGyroZ =  gz;
}

------------------------------------------------------------
10. Coding Standard
------------------------------------------------------------

Future code SHALL NEVER reference

ax
ay
az
gx
gy
gz

outside the IMU driver.

The driver SHALL expose only

robotAccelX
robotAccelY
robotAccelZ

robotGyroX
robotGyroY
robotGyroZ

All higher-level software
(balance control,
gesture recognition,
animation,
fall detection,
head tracking,
odometry,
Kalman filter,
Madgwick filter,
Mahony filter)

MUST use the mapped Robot Coordinate Frame.

------------------------------------------------------------
END OF DOCUMENT
------------------------------------------------------------