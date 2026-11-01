"""
Bug Condition Exploration Test — Eilik Bot Logic Fix

This script performs static analysis of the Eilik_Bot firmware source files
to confirm that 9 logic bugs exist in the unfixed code.

**Validates: Requirements 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9**

EXPECTED OUTCOME: All 9 tests FAIL (this proves the bugs exist).
Each failure documents a counterexample showing incorrect vs. expected behavior.
"""

import os
import re
import sys

# Resolve source file paths relative to this script
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
BOT_DIR = os.path.join(PROJECT_ROOT, "Eilik_Bot")

PINS_H = os.path.join(BOT_DIR, "Pins.h")
EILIK_SERVOS_CPP = os.path.join(BOT_DIR, "EilikServos.cpp")
EILIK_MOTION_CPP = os.path.join(BOT_DIR, "EilikMotion.cpp")
EILIK_BOT_INO = os.path.join(BOT_DIR, "Eilik_Bot.ino")
EILIK_FACE_CPP = os.path.join(BOT_DIR, "EilikFace.cpp")


def read_file(path):
    """Read a source file and return its content."""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


class BugConditionResults:
    """Collects pass/fail results and counterexamples."""

    def __init__(self):
        self.results = []

    def record(self, bug_id, description, passed, counterexample=""):
        self.results.append({
            "bug_id": bug_id,
            "description": description,
            "passed": passed,
            "counterexample": counterexample,
        })

    def summary(self):
        total = len(self.results)
        passed = sum(1 for r in self.results if r["passed"])
        failed = total - passed
        return total, passed, failed

    def print_report(self):
        print("=" * 72)
        print("BUG CONDITION EXPLORATION TEST REPORT")
        print("=" * 72)
        for r in self.results:
            status = "PASS" if r["passed"] else "FAIL"
            print(f"\n[{status}] Bug {r['bug_id']}: {r['description']}")
            if r["counterexample"]:
                print(f"       Counterexample: {r['counterexample']}")
        total, passed, failed = self.summary()
        print("\n" + "=" * 72)
        print(f"TOTAL: {total} | PASSED: {passed} | FAILED: {failed}")
        if failed > 0:
            print(f"\n*** {failed} test(s) FAILED — this confirms the bugs exist ***")
        else:
            print("\n*** All tests PASSED — bugs appear to be fixed ***")
        print("=" * 72)


results = BugConditionResults()


# =============================================================================
# Bug 1.1: Body touch threshold is 1325 instead of 1350
# =============================================================================
def test_bug_1_1():
    """Body touch threshold must be 1350 per Requirements.md."""
    content = read_file(PINS_H)
    match = re.search(r"#define\s+TOUCH_BODY_THRESH\s+(\d+)", content)
    if not match:
        results.record("1.1", "Body touch threshold", False,
                       "TOUCH_BODY_THRESH not found in Pins.h")
        return

    threshold = int(match.group(1))
    expected = 1350
    passed = (threshold == expected)
    if not passed:
        counterexample = (
            f"TOUCH_BODY_THRESH == {threshold} (should be {expected}). "
            f"Readings between {threshold} and {expected} (e.g., 1340) are NOT detected as touches."
        )
    else:
        counterexample = ""
    results.record("1.1", "Body touch threshold == 1350", passed, counterexample)


# =============================================================================
# Bug 1.2: Head touch threshold is 1000 instead of 950
# =============================================================================
def test_bug_1_2():
    """Head touch threshold must be below 1000 (e.g., 950) per Requirements.md."""
    content = read_file(PINS_H)
    match = re.search(r"#define\s+TOUCH_HEAD_THRESH\s+(\d+)", content)
    if not match:
        results.record("1.2", "Head touch threshold", False,
                       "TOUCH_HEAD_THRESH not found in Pins.h")
        return

    threshold = int(match.group(1))
    # Requirements.md says "below 1000" — threshold must be < 1000 (e.g., 950)
    passed = (threshold < 1000)
    if not passed:
        counterexample = (
            f"TOUCH_HEAD_THRESH == {threshold} (should be < 1000, e.g., 950). "
            f"Threshold sits exactly at the boundary instead of safely below it. "
            f"Readings between 950 and {threshold} may be unreliably detected."
        )
    else:
        counterexample = ""
    results.record("1.2", "Head touch threshold < 1000 (e.g., 950)", passed, counterexample)


# =============================================================================
# Bug 1.3: gesturePlayFace() has no Serial.printf output
# =============================================================================
def test_bug_1_3():
    """gesturePlayFace() must produce Serial debug output for gesture name, steps, timing."""
    content = read_file(EILIK_SERVOS_CPP)

    # Find the gesturePlayFace function body
    func_match = re.search(
        r"void\s+gesturePlayFace\s*\([^)]*\)\s*\{(.*?)^\}",
        content, re.DOTALL | re.MULTILINE
    )
    if not func_match:
        results.record("1.3", "Gesture serial debug output", False,
                       "gesturePlayFace() function not found in EilikServos.cpp")
        return

    func_body = func_match.group(1)
    has_serial = bool(re.search(r"Serial\.(printf|println|print)\s*\(", func_body))

    # Also check gestureUpdate for step-level debug
    update_match = re.search(
        r"void\s+gestureUpdate\s*\([^)]*\)\s*\{(.*?)^\}",
        content, re.DOTALL | re.MULTILINE
    )
    update_has_serial = False
    if update_match:
        update_has_serial = bool(re.search(r"Serial\.(printf|println|print)\s*\(", update_match.group(1)))

    passed = has_serial and update_has_serial
    if not passed:
        missing = []
        if not has_serial:
            missing.append("gesturePlayFace() has NO Serial output (no gesture name/steps printed)")
        if not update_has_serial:
            missing.append("gestureUpdate() has NO Serial output (no step transitions printed)")
        counterexample = "; ".join(missing)
    else:
        counterexample = ""
    results.record("1.3", "Gesture serial debug output present", passed, counterexample)


# =============================================================================
# Bug 1.4: wakeIfAsleep() jumps directly to FACE_WONDER instead of wake chain
# =============================================================================
def test_bug_1_4():
    """Wake from sleep must follow chain: Sleeping → Yawning → Sleepy → Smile."""
    content = read_file(EILIK_BOT_INO)

    # Find wakeIfAsleep function
    func_match = re.search(
        r"bool\s+wakeIfAsleep\s*\([^)]*\)\s*\{(.*?)^\}",
        content, re.DOTALL | re.MULTILINE
    )
    if not func_match:
        results.record("1.4", "Wake chain from sleep", False,
                       "wakeIfAsleep() function not found in Eilik_Bot.ino")
        return

    func_body = func_match.group(1)

    # Bug: calls react(FACE_WONDER) directly — hard jump, no multi-stage chain
    has_wonder_jump = bool(re.search(r"react\s*\(\s*FACE_WONDER", func_body))
    # Expected: should have Yawning and Sleepy stages
    has_yawning = bool(re.search(r"FACE_YAWNING", func_body))
    has_sleepy = bool(re.search(r"FACE_SLEEPY", func_body))

    passed = has_yawning and has_sleepy and not has_wonder_jump
    if not passed:
        counterexample = (
            "wakeIfAsleep() calls react(FACE_WONDER, 2500) directly — "
            "immediate hard jump to WONDER instead of Yawning → Sleepy → Smile wake chain. "
            "Sleeping state + wake trigger → FACE_WONDER (wrong) instead of staged wakeup."
        )
    else:
        counterexample = ""
    results.record("1.4", "Wake chain: Sleeping → Yawning → Sleepy → Smile", passed, counterexample)


# =============================================================================
# Bug 1.5: Idle decay has no FACE_BORED stage in the deterministic chain
# =============================================================================
def test_bug_1_5():
    """Idle decay must follow: active → Bored → Tired → Yawning → Sleeping."""
    content = read_file(EILIK_BOT_INO)

    # Find the idle logic section — look for "idle > 90000" and "idle > 45000"
    # The bug: only 2 deterministic stages (yawn at 45s, sleep at 90s), no Bored or Tired in the chain
    has_idle_90 = bool(re.search(r"idle\s*>\s*90000", content))
    has_idle_45 = bool(re.search(r"idle\s*>\s*45000", content))

    # Check if there's a deterministic Bored state in the idle decay chain
    # (NOT in the random fidgets section, but as a guaranteed timed stage)
    # The idle section is after "// ---- idle life:" comment
    idle_section_match = re.search(
        r"//\s*-+\s*idle life.*?$(.+)",
        content, re.DOTALL | re.MULTILINE
    )

    has_bored_stage = False
    if idle_section_match:
        idle_section = idle_section_match.group(1)
        # Look for a deterministic FACE_BORED triggered by idle time threshold
        # (not inside a switch/random block)
        # A proper implementation would have something like "idle > 30000 ... FACE_BORED"
        bored_by_time = re.search(r"idle\s*>\s*\d+.*?FACE_BORED", idle_section, re.DOTALL)
        if bored_by_time:
            # Make sure it's not inside the random fidgets switch
            # Check if FACE_BORED appears outside of "switch (random"
            lines = idle_section.split("\n")
            in_switch = False
            for line in lines:
                if "switch (random" in line:
                    in_switch = True
                if in_switch and "}" in line and "case" not in line:
                    in_switch = False
                if not in_switch and "FACE_BORED" in line and "idle" in line:
                    has_bored_stage = True
                    break

    passed = has_bored_stage
    if not passed:
        counterexample = (
            "Idle decay chain has only 2 stages: yawn at 45s, sleep at 90s. "
            "FACE_BORED only appears as a random fidget (case 5 in switch(random(12))), "
            "NOT as a guaranteed deterministic stage in the decay chain. "
            "Expected: active → Bored (~30s) → Tired (~50s) → Yawning (~70s) → Sleeping (~90s)."
        )
    else:
        counterexample = ""
    results.record("1.5", "Idle decay includes deterministic BORED stage", passed, counterexample)


# =============================================================================
# Bug 1.6: Anger escalation order reversed — fuming before furious
# =============================================================================
def test_bug_1_6():
    """Anger chain must be: annoyed → grumpy → furious → fuming (fuming is peak)."""
    content = read_file(EILIK_BOT_INO)

    # Find the screen tap / poke handler section
    # Look for pokeCount == 6 and what it maps to
    poke6_match = re.search(r"pokeCount\s*==\s*6\).*?react\s*\(\s*(FACE_\w+)", content, re.DOTALL)
    poke7_match = re.search(r"pokeCount\s*(?:>=\s*7|==\s*7)\)?\s*.*?react\s*\(\s*(FACE_\w+)", content, re.DOTALL)

    if not poke6_match:
        # Try alternative: else if structure
        poke_section = re.search(
            r"pokeCount\s*==\s*6\)\s*react\s*\(\s*(FACE_\w+)",
            content
        )
        poke6_face = poke_section.group(1) if poke_section else None
    else:
        poke6_face = poke6_match.group(1)

    # For poke >= 7 (the else clause)
    if not poke7_match:
        # The "else" after pokeCount == 6
        poke7_section = re.search(
            r"pokeCount\s*==\s*6\).*?else\s+react\s*\(\s*(FACE_\w+)",
            content, re.DOTALL
        )
        poke7_face = poke7_section.group(1) if poke7_section else None
    else:
        poke7_face = poke7_match.group(1)

    # Expected: pokeCount 6 → FURIOUS, pokeCount 7+ → FUMING
    # Bug: pokeCount 6 → FUMING, pokeCount 7+ → FURIOUS (reversed)
    passed = (poke6_face == "FACE_FURIOUS" and poke7_face == "FACE_FUMING")
    if not passed:
        counterexample = (
            f"pokeCount == 6 → {poke6_face}, pokeCount >= 7 → {poke7_face}. "
            f"Expected: pokeCount == 6 → FACE_FURIOUS, pokeCount >= 7 → FACE_FUMING. "
            f"The anger escalation is REVERSED: fuming appears before furious, "
            f"but datasheet says furious → fuming (fuming is peak anger)."
        )
    else:
        counterexample = ""
    results.record("1.6", "Anger chain: grumpy → furious → fuming (correct order)", passed, counterexample)


# =============================================================================
# Bug 1.7: TOUCH_BODY always maps to FACE_CHEEKY instead of affection emotions
# =============================================================================
def test_bug_1_7():
    """Body touch must trigger affection emotions (giddy/charmed/loving), NOT CHEEKY."""
    content = read_file(EILIK_BOT_INO)

    # Find the TOUCH_BODY handler
    body_match = re.search(
        r"te\s*==\s*TOUCH_BODY\)\s*\{(.*?)^\s*\}",
        content, re.DOTALL | re.MULTILINE
    )
    if not body_match:
        # Try single-line form: } else if (te == TOUCH_BODY) { react(...); }
        body_match = re.search(
            r"te\s*==\s*TOUCH_BODY\)\s*\{([^}]+)\}",
            content
        )

    if not body_match:
        results.record("1.7", "Body touch affection mapping", False,
                       "TOUCH_BODY handler not found in Eilik_Bot.ino")
        return

    handler_body = body_match.group(1)

    # Check what face it triggers
    face_calls = re.findall(r"react\s*\(\s*(FACE_\w+)", handler_body)

    has_cheeky = "FACE_CHEEKY" in face_calls
    has_affection = any(f in face_calls for f in ["FACE_GIDDY", "FACE_CHARMED", "FACE_LOVING"])

    passed = has_affection and not has_cheeky
    if not passed:
        counterexample = (
            f"TOUCH_BODY handler calls react({', '.join(face_calls) if face_calls else 'unknown'}). "
            f"Body touch ALWAYS triggers FACE_CHEEKY instead of affection emotions "
            f"(giddy/charmed/loving). Datasheet says chest touch = affection, "
            f"CHEEKY should only be triggered by quick face/screen tap."
        )
    else:
        counterexample = ""
    results.record("1.7", "Body touch → affection emotions (not CHEEKY)", passed, counterexample)


# =============================================================================
# Bug 1.8: FACE_CONFUSED rot variable grows unbounded (no wrapping)
# =============================================================================
def test_bug_1_8():
    """FACE_CONFUSED rot accumulator must wrap at TWO_PI to prevent unbounded growth."""
    content = read_file(EILIK_FACE_CPP)

    # Find the FACE_CONFUSED case in the switch
    confused_match = re.search(
        r"case\s+FACE_CONFUSED\s*:\s*\{(.*?)\bbreak\b",
        content, re.DOTALL
    )
    if not confused_match:
        results.record("1.8", "Confused rot wrapping", False,
                       "FACE_CONFUSED case not found in EilikFace.cpp")
        return

    confused_body = confused_match.group(1)

    # Check for rot increment
    has_rot_increment = bool(re.search(r"rot\s*\+=", confused_body))

    # Check for wrapping: fmodf, modulo, or if (rot > TWO_PI) rot -= TWO_PI
    has_wrap = bool(re.search(
        r"(fmodf?\s*\(\s*rot|rot\s*-=\s*TWO_PI|rot\s*=\s*fmodf?\s*\(|"
        r"if\s*\(\s*rot\s*>\s*TWO_PI|rot\s*%|rot\s*=.*?TWO_PI)",
        confused_body
    ))

    passed = has_rot_increment and has_wrap
    if not passed:
        if has_rot_increment and not has_wrap:
            counterexample = (
                "FACE_CONFUSED case: 'rot += 0.02f + 0.11f * (...)' increments every frame "
                "but NEVER wraps (no fmodf, no 'if (rot > TWO_PI) rot -= TWO_PI'). "
                "After extended display, rot grows unbounded causing float precision loss "
                "and visual artifacts in the spiral animation."
            )
        else:
            counterexample = "FACE_CONFUSED rot logic not found as expected"
    else:
        counterexample = ""
    results.record("1.8", "FACE_CONFUSED rot wraps at TWO_PI", passed, counterexample)


# =============================================================================
# Bug 1.9: No I2C rate-limiting or sequencing between CST816S and MPU6050
# =============================================================================
def test_bug_1_9():
    """MPU6050 reads must be rate-limited/sequenced to avoid I2C bus contention with CST816S."""
    content = read_file(EILIK_MOTION_CPP)

    # Find motionUpdate's read interval
    interval_match = re.search(r"now\s*-\s*lastRead\s*<\s*(\d+)", content)
    if not interval_match:
        results.record("1.9", "I2C bus coordination", False,
                       "motionUpdate() read interval not found in EilikMotion.cpp")
        return

    interval_ms = int(interval_match.group(1))

    # Check for any bus coordination mechanisms in actual code (not comments):
    # - Mutex/semaphore usage
    # - Flag checking (e.g., i2cBusy, busLock)
    # - Explicit coordination with CST816S
    # Strip comments first to avoid false positives from documentation
    code_lines = []
    for line in content.split("\n"):
        stripped = re.sub(r"//.*$", "", line)  # remove single-line comments
        code_lines.append(stripped)
    code_only = "\n".join(code_lines)
    # Also remove block comments
    code_only = re.sub(r"/\*.*?\*/", "", code_only, flags=re.DOTALL)

    has_coordination = bool(re.search(
        r"(mutex|semaphore|i2c[Bb]us|busLock|busBusy|xSemaphore|"
        r"acquireBus|releaseBus|i2c_guard|busAcquire|wire.*lock)",
        code_only, re.IGNORECASE
    ))

    # The bug: 30ms interval with NO coordination between CST816S and MPU6050.
    # The spec requires BOTH a reasonable rate-limit AND bus sequencing/coordination.
    # A bare 30ms poll with no coordination logic is insufficient — the CST816S
    # can be mid-transaction at any point in the loop and the MPU read can collide.
    passed = has_coordination
    if not passed:
        counterexample = (
            f"motionUpdate() reads MPU6050 every {interval_ms}ms with NO bus coordination. "
            f"Both CST816S (touch) and MPU6050 (IMU) share I2C on GPIO 21/22. "
            f"No mutex, semaphore, or sequencing logic prevents overlapping transactions. "
            f"Expected: explicit rate-limiting or bus coordination to avoid I2C contention."
        )
    else:
        counterexample = ""
    results.record("1.9", "I2C rate-limiting or bus coordination", passed, counterexample)


# =============================================================================
# Run all tests
# =============================================================================
def main():
    print("\nRunning Bug Condition Exploration Tests...")
    print(f"Source directory: {BOT_DIR}\n")

    # Verify source files exist
    for path, name in [(PINS_H, "Pins.h"), (EILIK_SERVOS_CPP, "EilikServos.cpp"),
                       (EILIK_MOTION_CPP, "EilikMotion.cpp"), (EILIK_BOT_INO, "Eilik_Bot.ino"),
                       (EILIK_FACE_CPP, "EilikFace.cpp")]:
        if not os.path.exists(path):
            print(f"ERROR: Source file not found: {path}")
            sys.exit(1)

    test_bug_1_1()
    test_bug_1_2()
    test_bug_1_3()
    test_bug_1_4()
    test_bug_1_5()
    test_bug_1_6()
    test_bug_1_7()
    test_bug_1_8()
    test_bug_1_9()

    results.print_report()

    # Exit with failure if any test failed (expected for unfixed code)
    _, _, failed = results.summary()
    sys.exit(1 if failed > 0 else 0)


if __name__ == "__main__":
    main()
