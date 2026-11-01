/*
==========================================================
MPU6050 Orientation Tester
ESP32 Dev Module
SDA -> GPIO21
SCL -> GPIO22
I2C Address -> 0x68
==========================================================
*/

#include <Wire.h>

#define MPU_ADDR 0x68

// MPU6050 Registers
#define PWR_MGMT_1 0x6B
#define ACCEL_XOUT_H 0x3B

int16_t ax, ay, az;
int16_t gx, gy, gz;

void writeRegister(uint8_t reg, uint8_t value)
{
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

void readMPU()
{
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(ACCEL_XOUT_H);
  Wire.endTransmission(false);

  Wire.requestFrom(MPU_ADDR, 14, true);

  ax = Wire.read() << 8 | Wire.read();
  ay = Wire.read() << 8 | Wire.read();
  az = Wire.read() << 8 | Wire.read();

  Wire.read(); // Temperature High
  Wire.read(); // Temperature Low

  gx = Wire.read() << 8 | Wire.read();
  gy = Wire.read() << 8 | Wire.read();
  gz = Wire.read() << 8 | Wire.read();
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Wire.begin(21, 22);
  Wire.setClock(400000);

  writeRegister(PWR_MGMT_1, 0x00); // Wake MPU6050

  delay(100);

  Serial.println();
  Serial.println("====================================");
  Serial.println(" MPU6050 ORIENTATION TEST");
  Serial.println(" Address : 0x68");
  Serial.println("====================================");
}

void loop()
{
  readMPU();

  float Ax = ax / 16384.0;
  float Ay = ay / 16384.0;
  float Az = az / 16384.0;

  float Gx = gx / 131.0;
  float Gy = gy / 131.0;
  float Gz = gz / 131.0;

  float roll = atan2(Ay, Az) * 180.0 / PI;
  float pitch = atan2(-Ax, sqrt(Ay * Ay + Az * Az)) * 180.0 / PI;

  Serial.println("------------------------------------");

  Serial.print("Accel (g)");
  Serial.print("\tX=");
  Serial.print(Ax, 3);
  Serial.print("\tY=");
  Serial.print(Ay, 3);
  Serial.print("\tZ=");
  Serial.println(Az, 3);

  Serial.print("Gyro (dps)");
  Serial.print("\tX=");
  Serial.print(Gx, 1);
  Serial.print("\tY=");
  Serial.print(Gy, 1);
  Serial.print("\tZ=");
  Serial.println(Gz, 1);

  Serial.print("Roll  : ");
  Serial.print(roll, 1);
  Serial.println(" deg");

  Serial.print("Pitch : ");
  Serial.print(pitch, 1);
  Serial.println(" deg");

  float maxAxis = max(max(fabs(Ax), fabs(Ay)), fabs(Az));

  Serial.print("UP Axis : ");

  if (maxAxis == fabs(Ax))
    Serial.println(Ax > 0 ? "+X" : "-X");
  else if (maxAxis == fabs(Ay))
    Serial.println(Ay > 0 ? "+Y" : "-Y");
  else
    Serial.println(Az > 0 ? "+Z" : "-Z");

  delay(250);
}