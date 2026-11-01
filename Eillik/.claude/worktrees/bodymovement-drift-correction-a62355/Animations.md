This is the complete animation script for all 38 expressions based on the expression sheet.

To ensure the robot feels alive, fluid, and non-stagnant, apply these **Global Animation Rules** to all expressions:

* **The "Breathing" Effect:** No expression should ever be perfectly still. Add a subtle, slow vertical sine-wave bob (1-2 pixels) to simulate breathing.
* **Squash and Stretch:** When the eyes blink or dart, they should stretch slightly in the direction of movement and squash when they stop or close.
* **Rounded Vectors:** Ensure all shapes are drawn with rounded caps and bezier curves. Avoid sharp 90-degree corners.
* **Organic Blinking:** Randomize blinks every 3 to 7 seconds. A blink should take about 3-4 frames (close fast, open slightly slower with a tiny bounce at the top).

---

### Row 1: Joy & Excitement

**1. Standard Smile**

* **Resting:** Two soft, glowing semi-circles curving upwards.
* **Animation:** The semi-circles gently throb with a soft glow. Occasionally, they stretch slightly wider for 1 second, then relax back.

**2. Giddy**

* **Resting:** High-arced smiling eyes with a slight inward tilt.
* **Animation:** The eyes rapidly bounce up and down in tandem. As they hit the top of the bounce, they squash slightly, conveying uncontrollable excitement.

**3. Ecstatic**

* **Resting:** Wide, thick inverted U-shapes.
* **Animation:** The eyes expand and contract in a rhythmic "heartbeat" pulse. Tiny, soft glowing particles occasionally emit from the outer edges and fade out smoothly.

**4. Charmed**

* **Resting:** Wide, circular eyes with soft pink gradients underneath (blush).
* **Animation:** The eyes slowly blink. When they open, they slide slightly to the left, then the right, as if admiring something. The pink blush pulses in opacity (50% to 100%).

**5. Sparkling Joy**

* **Resting:** Happy arches with two small, four-point rounded stars.
* **Animation:** The stars rotate slowly and fluidly, occasionally scaling up by 20% and back down. The eyes do a "happy squint" (flattening slightly) every few seconds.

**6. Playful Wink**

* **Resting:** One standard round eye, one squashed horizontal line (wink).
* **Animation:** The open eye darts playfully from side to side. The wink line wiggles slightly. Transition out of the wink smoothly, popping the eye open with a slight overshoot (stretching larger before settling).

**7. Starry Eyes**

* **Resting:** Two large, rounded glowing stars.
* **Animation:** The stars slowly morph into soft circles and back into stars. As they are stars, their points undulate smoothly like a jellyfish swimming, rather than rigid spinning.

**8. Cheerful Laugh**

* **Resting:** Deep, thick crescent moons facing upwards.
* **Animation:** A rapid, vibrating vertical shake (simulating a chuckle). The eyes scale down by 10% and back up on a short, 0.5-second loop.

**9. Eager**

* **Resting:** Slightly enlarged, perfectly round eyes placed slightly higher on the screen.
* **Animation:** The eyes dart left, then right, then center, followed by a double blink. The movement should have a sharp "ease-out" (stopping abruptly but smoothly).

**10. Anticipation**

* **Resting:** Wide eyes trembling slightly, placed low on the screen.
* **Animation:** The eyes slowly slide upwards on the Y-axis as if looking at something rising, then reset down quickly with a soft bounce.

---

### Row 2: Sadness & Anger

**11. Pout**

* **Resting:** Two circles flattened on the top, tilting slightly outward.
* **Animation:** The eyes slowly slide away from each other, then slide back. A subtle downward drift simulates feeling heavy.

**12. Teary Eyes**

* **Resting:** Round eyes with a watery, glowing wave effect at the bottom.
* **Animation:** The water level at the bottom of the eyes softly sloshes left and right. The eyes blink slowly and sluggishly.

**13. Crying Softly**

* **Resting:** Squashed, downturned arcs with teardrops.
* **Animation:** The teardrops detach from the bottom of the eyes, stretch as they fall, and fade out before hitting the bottom of the screen. New tears seamlessly generate.

**14. Disappointed**

* **Resting:** Half-circles flat on the bottom, placed low.
* **Animation:** A long, slow blink that stays closed for a full second. When they open, they look away (shift to the bottom left corner) to avoid "eye contact."

**15. Heartbroken**

* **Resting:** A single large, cracked heart or two cracked eye shapes.
* **Animation:** The crack organically pulses with a darker color. The two halves of the eyes slightly drift apart and together, as if struggling to stay whole.

**16. Grumpy/Frustrated**

* **Resting:** Circles cut off by a steep, downward-angled line on top.
* **Animation:** The eyes aggressively dart from side to side. The angled "brow" line dips lower, squashing the circle underneath it to show building pressure.

**17. Furious**

* **Resting:** Severely angled, glowing red/orange eyes.
* **Animation:** The eyes tremble violently. The outer edges of the eyes flare up organically like soft flames swaying in the wind.

**18. Fuming**

* **Resting:** Squinted, sharp (but corner-rounded) slits.
* **Animation:** Soft, cloudy puffs of "steam" (rounded circles fading in and out) emerge from the sides of the screen. The eyes pulse in a deep, angry red.

**19. Annoyed**

* **Resting:** One eye normal, one eye half-closed (flat top).
* **Animation:** The half-closed eye twitches slightly every 2 seconds. The robot rolls its eyes, making a slow, deliberate semi-circle along the top of the display.

**20. Flushed**

* **Resting:** Wide eyes, glowing red/orange all over.
* **Animation:** Rapid, erratic blinking. The eyes expand as if they are about to pop, then quickly shrink, simulating an overheated system trying to cool down.

---

### Row 3: Curiosity, Playful & Affection

**21. Questioning**

* **Resting:** One eye normal, one eye raised high and arched.
* **Animation:** The raised eye slowly bounces, while the normal eye squints slightly. The whole face tilts 15 degrees to the right smoothly.

**22. Investigating**

* **Resting:** One small eye, one massive eye (simulating a magnifying glass).
* **Animation:** The large eye scans back and forth smoothly across the screen, while the small eye stays locked in place. The large eye subtly distorts (fisheye effect) as it moves.

**23. Thoughtful/Puzzled**

* **Resting:** Eyes looking up and to the right, slightly squashed.
* **Animation:** The eyes drift slowly in a tiny figure-eight pattern in the top right corner. A very slow blink indicates deep processing.

**24. Confused Spiral**

* **Resting:** Two soft, rounded spiral shapes.
* **Animation:** The spirals rotate continuously in opposite directions. The rotation uses a sine wave for speed—speeding up dizzily, then slowing down to a near stop, then repeating.

**25. Wide-eyed Wonder**

* **Resting:** Massive, perfectly round eyes taking up most of the screen.
* **Animation:** The pupils (or glowing centers) slowly expand and contract. Tiny, subtle light reflections (small white circles) drift across the surface of the eyes.

**26. Playful**

* **Resting:** Arced eyes bounding upwards.
* **Animation:** The eyes perform a "wave"—the left eye bounces up, followed immediately by the right eye, creating a fluid, rolling motion across the face.

**27. Mischievous**

* **Resting:** Half-closed eyes with a distinct upward curve at the outer edges.
* **Animation:** The eyes slide slowly and smoothly to the extreme left edge of the screen, pause, blink once, and quickly snap back to the center.

**28. Cheeky Tongue**

* **Resting:** Happy squinting eyes with a soft pink U-shape (tongue) at the bottom.
* **Animation:** The tongue wiggles side to side like a jelly bean. The eyes do a quick, playful double-blink.

**29. Silly/Derp**

* **Resting:** One eye looking up, one looking down; differing sizes.
* **Animation:** The eyes move completely independently of each other. They occasionally cross in the middle before drifting away again. Highly fluid, zero sharp movements.

**30. Loving/Hearts**

* **Resting:** Two soft, rounded heart shapes.
* **Animation:** The hearts beat in a classic "ba-bum" rhythm. Upon beating, they scale up by 30% and emit a soft, blurred outer glow that instantly fades.

---

### Row 4: Fatigue & Reactive

**31. Tired**

* **Resting:** Heavy, half-closed eyelids (flat top, curved bottom).
* **Animation:** The eyes slowly droop downwards until they are almost closed, then snap back open suddenly (catching itself falling asleep), followed by a slow droop again.

**32. Sleeping/Zzz**

* **Resting:** Two flat horizontal lines with curved ends.
* **Animation:** The lines compress and expand slightly with a deep, rhythmic breathing pattern. Small, rounded "Z"s float up from the right side, fading out as they rise.

**33. Yawning**

* **Resting:** Squinted eyes with a large, soft oval (mouth) in the center.
* **Animation:** The oval expands significantly, trembling slightly at maximum size. The eyes squash down tight. Then, everything smoothly shrinks back to normal.

**34. Sleepy**

* **Resting:** Glazed, unfocused, wobbly circles.
* **Animation:** The eyes slowly drift downwards and lose opacity (fade to 50%), then slowly regain opacity as they slide back up.

**35. Scared/Shivering**

* **Resting:** Small, trembling circles placed high and close together.
* **Animation:** A rapid, continuous X and Y axis jitter (1-2 pixels). The eyes occasionally dart rapidly to the corners of the screen as if looking for danger.

**36. Bored/Meh**

* **Resting:** Two perfectly horizontal, pill-shaped lines.
* **Animation:** Absolute minimal movement. The eyes do one very slow, lethargic blink every 8-10 seconds. Occasionally, they sigh (dropping on the Y-axis slightly and returning).

**37. Embarrassed/Blush**

* **Resting:** Wide eyes looking down, large soft pink gradients on the sides.
* **Animation:** The eyes dart away from the center. The pink gradient pulses warmly and slowly spreads inwards, momentarily covering the bottom half of the eyes.

**38. Hungry/Food Icon**

* **Resting:** Wide, pleading eyes with a subtle graphic (like a rounded battery or a fish, as in the image).
* **Animation:** The eyes track the food graphic. The food graphic floats gently like it's in water. The eyes do a "puppy dog" stretch, elongating vertically to look extra cute and desperate.