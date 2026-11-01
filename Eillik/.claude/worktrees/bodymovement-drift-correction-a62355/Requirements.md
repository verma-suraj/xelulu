# Eillik Final code requirements

# New changes in circuits

| MPU6050 | ESP32 |
| --- | --- |
| SDA | 21 |
| SCL | 22 |

---

# Changes required in code

| Emotion Sr. No. | Emotion name | Change sound to |
| --- | --- | --- |
| 21 | silly derp | oi-oi-1x-minion-slowed.wav |
| 25 | giddy | risa-minion_loujUe9.wav |
| 29 | cheeky tongue | bahhh.wav |

- To find the sound refer to the "WAV files" folder

---

# Changes in hardware
- **Head** servo motor is not installed and the head is not fixed so no head movement will be there 
- Only 3 servo motors are installed arm left , arm right , torso and the pin number are same as decided just the head servo pin will be empty
- The MPU6050 is mounted in the random direction . For the proper orientation guide refer to mpu6050_orientation.md
- For body touch threshold for single finger is 1350 and for strong touch 1050 and for the head is below 1000.
- Be cautious with the 21 and 22 as it have 2 i2c devices are connected . 

---

# Requirement for the final code
- Update all the changes I have mentioned above 
- Refer to Eilik_Emotion_Movement_Datasheet.md for all the decided body movement and the guesture . 
- Make sure the whole thing will be able to run on the ESP32 devmodule . Keep the ram and flash size in mind while writing the code . The movement should not be laggy .
- As the final deliverable I want a final code that I can upload in the ESP32 and start testing the complete hardware.
- The code must contain serial debugging like the current emotion along with the serial number and also it should tell which body movement it is showing right now so that i can verify the physical movement with the serial text .

---

# Embedded Systems Rules

**When writing firmware:**
- Respect RAM limits

- Respect Flash limits

- Avoid blocking delays unless necessary
- Prefer state machines
- Avoid unnecessary heap allocation
- Avoid String fragmentation
- Use constexpr where appropriate
- Reuse buffers
- Minimize ISR work
- Protect shared resources

---

# Hardware Rules

**Never assume:**

- Pin numbers
- Voltage
- Current
- Pull-ups
- Pull-downs
- I2C addresses
- SPI modes
- UART baud
- Servo limits
- PWM frequency
- Clock speed
- Unless explicitly provided.

---

# Documentation Rules

**Every implementation must include:**
- Summary
- Requirements addressed
- Files modified
- Important decisions
- Known limitations
- Testing procedure
- Future improvements

# Hallucination Prevention

**Before answering internally verify:**

- Did I invent anything?
- Did I assume something?
- Did I create files that don't exist?
- Did I reference unknown functions?
- Did I use undocumented APIs?
- If YES
- Remove them.

# Final Deliverable Checklist

**Every completed task must satisfy:**

- All user requirements implemented
- No assumptions
- No hallucinations
- No placeholder code
- Production quality
- Buildable
- Consistent style
- Memory conscious
- Efficient
- Fully explained
- Tested logically