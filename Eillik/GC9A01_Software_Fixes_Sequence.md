# GC9A01 / SmartElex 1.28" Round TFT Startup Reliability -- Software Fixes

## Objective

The display occasionally fails to boot after a rapid OFF → ON power
cycle but boots correctly after remaining powered off for a longer
period.

The following fixes should be implemented **one at a time**, testing
after each change before proceeding to the next.

------------------------------------------------------------------------

# Step 1 -- Add an Explicit Hardware Reset

Do not rely only on `tft.init()`.

Before initializing the display:

-   Configure `TFT_RST` as an output.
-   Drive the reset pin LOW for approximately **50 ms**.
-   Drive the reset pin HIGH.
-   Wait approximately **200 ms**.
-   Call `tft.init()`.

This ensures the display controller always starts from a known reset
state.

------------------------------------------------------------------------

# Step 2 -- Add a Power-Up Delay

Before touching SPI or calling any display functions:

-   Wait **300 ms** after the ESP32 boots.

Reason:

The ESP32 typically boots faster than the GC9A01 display controller.

------------------------------------------------------------------------

# Step 3 -- Reduce SPI Clock Frequency

If currently running at 40 MHz, test progressively lower SPI
frequencies.

Recommended order:

1.  27 MHz
2.  20 MHz
3.  10 MHz

Test each frequency before moving to the next.

------------------------------------------------------------------------

# Step 4 -- Retry Display Initialization

If the first initialization attempt fails:

1.  Perform another hardware reset.
2.  Wait.
3.  Initialize again.

Allow up to **three attempts** before reporting failure.

------------------------------------------------------------------------

# Step 5 -- Reset Both Controllers

The module contains:

-   TFT Reset
-   Capacitive Touch Reset

During startup:

1.  Pull both reset pins LOW.
2.  Wait approximately 50 ms.
3.  Pull both reset pins HIGH.
4.  Wait approximately 200 ms.

Only then initialize the display.

------------------------------------------------------------------------

# Step 6 -- Initialize in the Correct Order

Recommended startup sequence:

1.  Power-up delay
2.  Reset TFT
3.  Initialize SPI
4.  Initialize TFT
5.  Draw a simple test screen
6.  Initialize touch controller
7.  Start the application UI

Avoid initializing the touch controller before the display.

------------------------------------------------------------------------

# Step 7 -- Ensure Exclusive SPI Access

During display initialization:

-   No Wi-Fi tasks using SPI
-   No SD card access
-   No other SPI peripherals
-   No concurrent FreeRTOS tasks using SPI

The display should have exclusive SPI access until initialization
completes.

------------------------------------------------------------------------

# Step 8 -- Add a Startup Health Check

After `tft.init()`:

-   Draw a simple pattern or splash screen.
-   If initialization appears unsuccessful:
    -   Reset the display.
    -   Reinitialize automatically.

------------------------------------------------------------------------

# Step 9 -- Implement a Display Recovery Function

Create a reusable recovery routine capable of:

-   Hardware reset
-   SPI restart
-   Display initialization
-   Rotation setup
-   Screen clear
-   UI redraw

This allows software recovery without rebooting the ESP32.

------------------------------------------------------------------------

# Step 10 -- Increase Reset Delays

Increase initialization timing:

-   Reset LOW: \~50 ms
-   Reset HIGH: \~200 ms
-   Delay before `tft.init()`: \~300 ms

Many display modules are more reliable with conservative timings.

------------------------------------------------------------------------

# Step 11 -- Prevent a Floating Reset Pin

Always configure the reset pin explicitly:

-   Set reset GPIO as OUTPUT.
-   Keep it HIGH except during reset.

Never leave the reset pin floating.

------------------------------------------------------------------------

# Step 12 -- Restart SPI Before Initialization

Before calling `tft.init()`:

1.  End the SPI bus.
2.  Wait briefly.
3.  Start SPI again.
4.  Initialize the display.

This clears any stale SPI state.

------------------------------------------------------------------------

# Step 13 -- Add Detailed Boot Logging

Log every stage of startup:

-   Boot
-   Reset
-   SPI start
-   TFT initialization
-   Rotation
-   First draw
-   Touch initialization
-   UI startup

This helps identify the exact stage at which failures occur.

------------------------------------------------------------------------

# Step 14 -- Block Other Tasks Until the Display Is Ready

Do not launch application tasks until the display has successfully
completed initialization.

This prevents timing conflicts during startup.

------------------------------------------------------------------------

# Testing Procedure

After implementing each step:

1.  Flash the firmware.
2.  Perform at least **20 rapid OFF → ON cycles**.
3.  Record whether the display initializes correctly.
4.  Proceed to the next step only if the issue persists.

------------------------------------------------------------------------

# Notes

If all software fixes fail, the root cause is likely hardware-related,
such as:

-   Incomplete discharge of the display power rail.
-   Incorrect power sequencing.
-   Slow regulator startup.
-   Power supply instability.
-   Display reset circuitry.
