# AI Prompt: ESP32 Eilik Robot Sound Effects Generator

**Instructions for the AI Assistant:** Act as an expert Embedded Systems & C++ developer. Your task is to generate a comprehensive, non-blocking Arduino/C++ library for an ESP32 that synthesizes 38 distinct "bleep and bloop" sound effects for an interactive desktop robot (similar to Eilik, Wall-E, or BB-8). 

## Hardware & Architecture Constraints
1. **Microcontroller:** ESP32
2. **Audio Output:** A Class D amplifier connected to **GPIO 25** (using standard `tone()` / `ledcWriteTone()` or DAC synthesis).
3. **CRITICAL:** The robot is running a high-framerate SPI TFT display. **You are strictly forbidden from using `delay()`**. All audio generation must be non-blocking, utilizing `millis()` state machines, hardware timers, or FreeRTOS tasks pinned to a specific core (Core 0 preferred for audio).
4. **Style:** The sounds must be generated mathematically (sine/square wave sweeps, arpeggios, frequency modulation) rather than playing WAV files from an SD card.

## Required Sound Effect Functions
Generate a C++ class or a series of state-machine functions for the following 38 emotions. Adhere to the provided pitch, tempo, and design rules.

### Category 1: Joy & Excitement (Major keys, ascending pitch, fast bounce)
1. **Standard Smile:** Mid-to-high, quick pleasant upward-sliding chirp.
2. **Giddy:** Very high, rapid series of bubbly staccato bleeps bouncing up and down.
3. **Ecstatic:** High, very fast trilling sequence ascending in a scale, ending in a bright pop.
4. **Charmed:** Mid-high, slow and smooth melodic two-tone chime.
5. **Sparkling Joy:** High, rapid bursts of twinkling bloops (arpeggio).
6. **Playful Wink:** High, snappy "bloop-bleep!" with a sharp pop at the end.
7. **Starry Eyes:** High, sustained shimmering vibrato tone.
8. **Cheerful Laugh:** Mid-high, rhythmic staccato bouncy bloops ("ha-ha-ha").
9. **Eager:** High, quick two ascending chirps.
10. **Anticipation:** Escalating tempo bleeps that gradually speed up and rise slightly.

### Category 2: Sadness & Anger (Descending pitches, dissonant or harsh tones)
11. **Pout:** Mid-to-low, slow descending digital sigh.
12. **Teary Eyes:** Mid, slow vibrato, wobbly descending tone.
13. **Crying Softly:** Mid-high, slow and sparse falling bleeps (water drops).
14. **Disappointed:** Low, flat, slightly atonal "womp".
15. **Heartbroken:** High bleep that abruptly cracks into a dissonant, lower buzz.
16. **Grumpy/Frustrated:** Low, staccato, buzzy guttural bleeps.
17. **Furious:** High and harsh, loud, erratic bleeps with distortion.
18. **Fuming:** Low rhythmic pulse (ticking bomb) ending in sharp static.
19. **Annoyed:** Mid-low, abrupt, flat monotone bleep.
20. **Flushed:** Rising pitch, chaotic fast escalating glitch sounds.

### Category 3: Curiosity, Playful & Affection (Dynamic pitch bends, vocal inflections)
21. **Questioning:** Mid-to-high pitch bend sliding sharply upward at the end.
22. **Investigating:** Mid, steady and rhythmic sonar pings.
23. **Thoughtful:** Mid, slow calculating alternating two-tone bleeps.
24. **Confused Spiral:** Fluctuating pitch, steady tempo looping up and down smoothly.
25. **Wide-eyed Wonder:** Low to high smooth slide, holding softly at the top.
26. **Playful:** High, bouncy syncopated pattern with varied velocities.
27. **Mischievous:** Quiet to loud, sneaky ascending scale ending in a sharp chirp.
28. **Cheeky Tongue:** High, elastic springy "boing" with a rapid downward slide.
29. **Silly/Derp:** Varied pitches, irregular tempo, out-of-tune bleeps.
30. **Loving/Hearts:** Mid-high, rhythmic melodic two-tone digital heartbeat.

### Category 4: Fatigue & Reactive (Low energy, fading out, or sudden bursts)
31. **Tired:** Mid-to-low, slow descending bloop losing volume/pitch.
32. **Sleeping/Zzz:** Low, slow rhythmic hums that fade in and out.
33. **Yawning:** Mid, long slow ascending-then-descending wobbly bleep.
34. **Sleepy:** Low, dragging tempo, sluggish detuned bleeps.
35. **Scared/Shivering:** Very high, rapid stuttering micro-bleeps (jitter).
36. **Bored/Meh:** Mid, sluggish, flat unenthusiastic tone dropping off quickly.
37. **Embarrassed:** High, fast ascending trill that cuts off abruptly.
38. **Hungry/Food:** High, persistent repeating two-tone chirp (high-low, high-low).

## Expected Output format:
1. Provide a well-structured header file (`.h`) declaring the sound manager class.
2. Provide the implementation file (`.cpp`) with non-blocking frequency arrays or timer logic.
3. Include an example `.ino` sketch demonstrating how to trigger the sounds while running a dummy display loop (to prove non-blocking capability).
