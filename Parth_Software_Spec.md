# Parth — Software Team Spec

Parth is the flagship adult companion (ages 20–45): **AI chief-of-staff, mentor, expert council**, with a 3.5" touch glance face. This document lets the **software team build the AI brain, integrations and app in parallel with the hardware**, by reusing the Xelulu gateway contract and layering Parth-specific features on top.

The hardware spec is deliberately thin here — it is covered by `Parth_BOM_ESP32S3_v4.xlsx` + the firmware. **This document is about AI features and what the software team must deliver.**

---

## 1. How Parth differs from Xelulu (one page)

| Thing | Xelulu | Parth |
|---|---|---|
| Audience | Kids 1–12 | Adults 20–45 (flagship) |
| Form | Mobile egg with wheels + arms + visor | Static desk orb with touch face |
| Face | 2" pixel-LED visor, no touch | **3.5" resistive-touch HMI** (UART smart-screen) |
| Primary input | Voice + touch zones + motion | Voice + touch screen + optional TTP223 taps |
| On-device perception | Wake word, cry detection | Wake word only |
| Movement | Yes (2 wheels, ToF) | **None** (static) |
| Camera | None | **None** (ESP32-S3 build; SBC build later for webcam) |
| Phone calls | — | **Yes**, via external BT-Classic module (HFP) |
| Smart home | — | **Matter over Wi-Fi** |
| Battery | 2S | 1S2P |

**Reuses from Xelulu, zero changes:** the gateway contract (§3 of the Xelulu spec), auth model, cloud infrastructure, voice pipeline (STT→LLM→TTS), app accounts/households, OTA pipeline.

**New in Parth (software team work):** everything in §4 — the AI feature catalogue (chief-of-staff, meetings, research, drafting, mentor, council, smart home), the HMI display GUI, the BT-call bridge, Matter integration, and the connections/permissions model.

---

## 2. Scope & parallel delivery

**The same rule as Xelulu: freeze the device gateway API first, build to it.** Parth adds a small delta on top of the shared gateway (§3 of this doc), not a new protocol. Everything above the gateway is identical in shape — new feature services, new UI, new integrations — but the transport, auth, device identity and message envelopes are the same.

**Software team owns (this doc):**
- **Chief-of-staff services** — calendar / mail / tasks integrations, drafting, meeting prep, follow-up tracking, personal admin.
- **Meeting companion** — recording consent, transcription, speaker separation, summary + action-item extraction.
- **Research agent** — multi-step web research with source attribution.
- **Expert Council + Mentor + Rehearsal coach** — multi-persona LLM orchestration, goals / weekly reviews / decision journal.
- **Smart home** — Matter-over-Wi-Fi controller service.
- **Phone-call bridge control** — commanding the external BT module via firmware.
- **HMI GUI pages** — designed in the SmartElex HMI editor, stored on the display's own micro-SD; the firmware only sends page IDs + variable values over UART.
- **Parth module in the shared app** — connections, rules, approvals, council memos, meeting transcripts.
- **Device simulator (Parth profile)** — a feature flag on the Xelulu simulator (§9 of that spec) that emits Parth events (touch screen, BT call state) and accepts Parth commands.

**Hardware team owns:** the ESP32-S3 gateway firmware, HMI display wiring, BT module wiring + AT-command firmware, Matter radio stack on ESP32-S3, I2S audio pipeline, power, enclosure.

---

## 3. Gateway deltas vs. Xelulu

Reuse Xelulu §3.1 (transport, auth, versioning, heartbeat, QoS) and §3.2 (topic shape; `xelulu/` becomes `parth/{device_id}/...`). Only the following message types change.

### 3.1 New / changed events (device → cloud)

| Event `type` | `data` | Replaces / adds |
|---|---|---|
| `screen_touch` | `{ page: "home", target: "card_next_meeting", gesture: "tap"|"long"|"swipe_left"|"swipe_right" }` | New — the HMI reports tap targets by name, not pixel coords |
| `tap_zone` | `{ zone: "head_top"|"head_left"|"head_right"|"base_front"|"base_rear"|"back_of_head", action: "tap"|"double_tap"|"hold" }` | New — matches the physical touch map (TTP223 or capacitive) |
| `bt_call` | `{ state: "incoming"|"active"|"ended"|"muted", caller: "name_or_number" }` | New — BT module reports via firmware |
| `focus` | `{ action: "start"|"end"|"interrupted", reason: "walkup"|"vip"|"user", seconds: 1800 }` | New — Bird's Eye mode state |
| `wake` | same shape as Xelulu | Reused |
| `motion` | **drop** `pickup/shake/upside_down` (static device); keep nothing | Removed |
| `touch` | **drop** (zones replaced by `tap_zone` / `screen_touch`) | Removed |

### 3.2 New / changed commands (cloud → device)

| Command `action` | `data` | Purpose |
|---|---|---|
| `screen` | `{ page: "home"|"meeting_card"|"brief"|"focus"|"call"|"council_memo"|"custom", vars: { ... } }` | Switch the HMI to a named page and push variable values (text, countdown, status). The HMI renders its own GUI. |
| `screen_toast` | `{ text: "Meeting in 10", kind: "info"|"warn"|"urgent", seconds: 5 }` | Transient banner on top of the current page |
| `bt_call` | `{ cmd: "answer"|"hangup"|"mute"|"unmute"|"dial", number?: "..." }` | Drive the external BT module |
| `focus` | `{ cmd: "start"|"end", minutes: 25, rules: {...} }` | Enter / leave Bird's Eye focus |
| `matter` | `{ cmd: "scene"|"on"|"off"|"set", target: "living_room_lights", value: 80 }` | Drive a Matter-paired device |
| `say` / `animate` / `play` / `set_mode` / `config` / `ota` | same as Xelulu | Reused (animate is lightweight — face expression swap on the HMI) |
| `gesture` / `move` | **drop** (no arms, no wheels) | Removed |

### 3.3 Status

Same as Xelulu, plus two fields:

```json
{ "..." : "...", "bt_module": "ready"|"not_paired"|"in_call", "matter_hub": "ready"|"commissioning"|"error" }
```

### 3.4 Freeze process

Same as Xelulu — both leads sign off this §3 before anyone writes feature code. Changes bump `proto_ver`.

---

## 4. AI feature catalogue — the software work

Every row maps a catalogue feature (PA-* IDs from the Product Feature Catalogue §6) to what the software team must build. Priorities come from the catalogue; **P0 = must have at launch**.

### 4.1 Chief of staff (catalogue §6.1)

| ID | Feature | What software must build | Priority |
|---|---|---|---|
| PA-C01 | **Glance screen** | HMI pages: status strip (time/weather/temp always on), rotating main cards (next meeting w/ countdown, agenda, top tasks, commute time, focus timer). Cloud pushes `screen` cmds with vars every 15 s or on event. | P0 |
| PA-C02 | **Morning / evening brief** | Scheduled briefing service: pulls user data (calendar, mail priority, weather, news if opted in, tasks) → LLM composes brief → TTS plays voice + `screen` cmd shows cards. User sets time + content categories. | P0 |
| PA-C03 | **Calendar command** | Integrations: Google Calendar + Microsoft 365 (OAuth). LLM function-calling tools: `list_events`, `create_event`, `move_event`, `cancel_event`, `find_free_slot`, `propose_times`. **Mandatory confirmation step** before any action that touches other people. | P0 |
| PA-C04 | **Mail triage** | Integrations: Gmail + Outlook. Services: priority classifier (user rules + VIP list), summarizer, draft composer. `send` requires explicit "Yes send it" voice or screen confirmation. | P0 |
| PA-C05 | **Tasks + follow-ups** | Integrations: Todoist, Notion, Google Tasks, Microsoft To-Do (configurable). Follow-up tracker: watches sent mail → cron check "if no reply by Thursday" → remind on glance screen. | P0 |
| PA-C06 | **Meeting prep packs** | 15 min before each meeting: pull attendees → cross-reference last-interaction mails + notes → fetch linked docs → LLM composes a 1-pane prep card → pushed to glance screen + app. | P0 |
| PA-C07 | **Research agent** | Multi-step web research tool: LLM agent with `search`, `open_url`, `extract`, `cite`. Produces a markdown report with source links to the app or user's mail. Rate-limit, cost-cap per request. | P0 |
| PA-C08 | **Drafting (user's style)** | Style-sample ingestion flow (user approves which samples go in). Prompt-engineered or fine-tuned writer per user. Outputs to app or connected doc tools (Google Docs, Notion). | P0 |
| PA-C09 | **Personal admin** | Mail parsers for bills, renewals, travel itineraries, package tracking; voice-captured expense log; reminders surfaced on glance screen. | P1 |
| PA-C10 | **Phone bridge (notifications)** | Companion phone app (Android first): read notifications w/ user permission → forward summary to cloud → voice readout; voice-dictated reply sent back via phone. iOS limited to calendar/mail/reminders. | P1 |
| PA-C11 | **BT speakerphone** | Pair user's phone to the BT module (one-time via app). Call audio routes through the module's own mic + Parth's speaker. Firmware controls answer/hangup via AT commands over UART; cloud shows the call-screen page. | P1 |

### 4.2 Meeting companion (§6.2)

| ID | Feature | What software must build | Priority |
|---|---|---|---|
| PA-M01 | **In-room notes** | Voice intent → cloud opens a long-form audio session; **cloud emits a `screen` cmd setting the face to "Recording" with a red ring + spoken announcement**; stream mic audio up; store as transcription-only (audio deleted after processing by default). | P0 |
| PA-M02 | **Speaker tracking** | Mic-array direction estimation only (no camera on this build). Face page animates a "looking toward" indicator; actual head turn is a Parth-SBC-later feature. Diarization in the transcription service. | P0* |
| PA-M03 | **Summary + action items** | Within 2 min of meeting end: LLM extracts decisions, action items with owners + due dates, open questions. User approves before any resulting mail/task/calendar action is executed. | P0 |
| PA-M04 | **Live assist** | While recording, user whispers "Parth, what did we agree last time?" → quiet retrieval over past meeting index → shows card on the face (not spoken aloud mid-meeting). Also time-keeping toasts ("10 min left"). | P1 |
| PA-M05 | **Consent controls** | Any room participant can say "Parth, stop recording" — wake-word recogniser must handle this phrase from any voice, not just the owner. Retention default: delete raw audio after transcription; user-configurable. | P0 |

\* `PA-M02` is reduced to mic-array direction only on the ESP32-S3 build (no camera). Full speaker tracking returns in an SBC Parth variant.

### 4.3 Webcam & video calls (§6.3) — DEFERRED

ESP32-S3 Parth has **no camera**. The following are **dropped from P0 and move to a future SBC Parth build**:

- PA-W01 USB webcam
- PA-W02 USB speakerphone (standalone — Parth works as a Bluetooth speakerphone instead, see PA-C11)
- PA-W03 Auto-framing / speaker tracking by camera
- PA-W04 Camera privacy indicators (nothing to protect)

**Kept, voice-only variant:** PA-W05 Family calls (voice-only to another Parth / Sarthi / Xelulu via the household intercom in PA-H04).

Call this out in the app: the ESP32-S3 Parth is marked "voice edition"; webcam features are shown as "coming to Parth Pro".

### 4.4 Focus & wellbeing (§6.4)

| ID | Feature | What software must build | Priority |
|---|---|---|---|
| PA-F01 | **Bird's Eye focus** | Focus session state machine (start/pause/end) → `focus` cmd + `screen` to focus-page + optional phone DND via companion app. VIP/urgent-rule bypass evaluates each incoming notification before it is relayed. | P0 |
| PA-F02 | **Walk-up handling** | **Dropped** on ESP32-S3 Parth — no ToF, no camera. Replace with "tap head to leave a note" via TTP223 or the touch screen. | P1 → simplified |
| PA-F03 | **Healthy work habits** | Timers for hydration, 20-20-20 eye rest, stand-and-stretch. Posture nudge (camera-based) **dropped** here; keep hydration/eye/stretch. | P1 (reduced) |
| PA-F04 | **Wind-down** | End-of-day ritual: LLM asks pre-set reflection prompts → captures open loops → sets tomorrow's first task → good-night face. | P1 |

### 4.5 Mentor & expert council (§6.5)

| ID | Feature | What software must build | Priority |
|---|---|---|---|
| PA-E01 | **Expert Council** | Multi-agent LLM orchestration: up to 6 named lenses (CFO, lawyer, engineer, marketer, investor, customer, or user-defined). Each gets the user's plan + its own system prompt; outputs independent views; Parth synthesises a memo. **Memo delivered to the app, not read aloud in full.** Must display "AI perspectives, not professional advice." | P0 |
| PA-E02 | **Mentor mode** | Goals service (quarterly + weekly); weekly review flow; decision journal (user dictates decisions → stored with context → surfaced later for reflection); kind commitment tracker (never shaming). | P0 |
| PA-E03 | **Rehearsal coach** | Role-play LLM mode (interviewer / investor / counterpart); feedback on structure, pace (words/min from mic), filler words. **Eye-contact feedback dropped** (no camera). | P0 (reduced) |
| PA-E04 | **Skill plans** | LLM-generated learning plans + resource curation + spaced-practice scheduler. | P1 |
| PA-E05 | **Student chief of staff** | Semester planner, assignment deadline tracker, lecture/paper summariser (user pastes/uploads), thesis coach prompts, placement-prep practice sets, internship tracker. Shares infra with PA-C* and PA-E02. | P0 |

### 4.6 Home & life (§6.6)

| ID | Feature | What software must build | Priority |
|---|---|---|---|
| PA-H01 | **Smart home (Matter)** | Cloud Matter controller service **or** on-device Matter commissioner (ESP32-S3 supports Matter over Wi-Fi — Thread deferred, no Thread radio in BOM). Scenes, routines, status cards on the glance screen. | P0 (Wi-Fi only) |
| PA-H02 | **Music & media** | Integrations: Spotify, YouTube Music, podcasts. Audio streamed via cloud to the device's I2S speaker (or BT module for higher quality music). | P1 |
| PA-H03 | **Timers, alarms, reminders, lists** | Core assistant primitives; must work offline (firmware timers + cached alarms). | P0 |
| PA-H04 | **Household intercom** | Voice-only call to another household Companion device (Xelulu / Sathi / Sarthi / another Parth). Reuses the cloud gateway — one device publishes an audio turn, the other subscribes. Permission-gated. | P1 |
| PA-H05 | **Everyday knowledge** | General LLM Q&A, translation (10 Indian + major world languages per catalogue §3), cooking help, unit/currency conversions, news explained. | P0 |

### 4.7 Offline mode (§6.7)

Must work with no internet (firmware + cached content):
- Wake word, touch screen, TTP223 taps.
- Timers, alarms, reminders (previously-synced only; new ones queued).
- Glance face shows a cached "last known" morning brief + offline indicator.
- BT calls (the module itself works offline over the paired phone).
- Smart home: Matter commands can work locally if the Matter stack is on the device (no cloud needed for local scenes).
- Offline line: *"I can't reach the brain right now — your timers, calls and lights still work."*

**Re-sync on reconnect:** flush queued events (focus sessions, touch events, voice captures that fell through), fetch missed briefs, apply pending config / OTA.

---

## 5. HMI display GUI contract

The SmartElex 3.5" HMI is a **smart display** — it renders its own GUI pages from assets on its own micro-SD card, driven by UART commands from the ESP32-S3. **Software team designs these pages in the SmartElex HMI editor and ships the resulting SD-card image as part of the OTA bundle.**

### 5.1 Required pages (P0)

| Page name | Elements | Pushed vars from cloud |
|---|---|---|
| `home` | Status strip (time, weather, temp), rotating main card | `card_type`, `card_title`, `card_body`, `countdown_sec` |
| `brief` | Morning / evening brief as swipeable cards | `cards[]` (title, body) |
| `meeting_card` | Next meeting focus card | `title`, `time_in`, `attendees`, `location` |
| `focus` | Full-screen focus timer, minimal | `minutes_remaining`, `label` |
| `call` | Incoming / active call UI | `caller`, `state`, `duration` |
| `council_memo` | Scrollable AI memo view | `memo_md` |
| `recording` | Red ring + "Recording" + stop button | `elapsed`, `can_stop` |
| `offline` | Offline notice + what still works | — |
| `pairing` | QR code + pairing status | `code`, `step` |

### 5.2 Interaction

- Each tappable region on each page has a **stable target name**; firmware reports it as `screen_touch` → cloud (§3.1).
- No pixel coordinates cross the gateway — only logical targets.
- Face expressions (`animate`) are a small enum of face frames stored on the SD-card and switched by UART.

### 5.3 OTA

GUI assets ship as a single versioned bundle (`hmi_assets_v{n}.bin`). OTA to the device includes both firmware and (optionally) a new HMI bundle; the firmware writes the bundle to the HMI's SD-card over its UART file-transfer protocol.

---

## 6. Connections, OAuth & the permissions model

Parth is only useful once it has access to the user's mail, calendar, tasks, docs, smart home, and (optionally) phone. The software team must build a **first-class connections flow** in the app.

### 6.1 Supported connections (P0)

| Connection | Purpose | Scopes requested |
|---|---|---|
| Google (Calendar, Gmail, Tasks, Drive) | Chief of staff, drafts | per-service, incremental |
| Microsoft 365 (Outlook, Calendar, To-Do, OneDrive) | Chief of staff, drafts | per-service, incremental |
| Notion / Todoist | Tasks + notes | read + write (optional) |
| Spotify / YouTube Music | Music | playback |
| Matter smart-home | Lights, scenes | commissioner |
| Phone companion app | Notifications bridge | OS-level |

### 6.2 Permissions model

Per-connection, the user grants one of three levels:
- **Read** — Parth may see this data (brief, prep packs, triage).
- **Draft** — Parth may compose, but must ask before sending.
- **Act** — Parth may act without asking (irreversible changes still always ask).

Default is **Draft**. Nothing irreversible (send, delete, pay, buy) ever skips confirmation, regardless of level.

### 6.3 Rules engine

User-editable rules shown in plain language in the app:
- **VIPs** — people whose mail/calls always break focus.
- **Urgent rules** — keywords, labels, sources that bypass DND.
- **Focus rules** — what Parth may interrupt me for.
- **Working hours** — when `act` level is allowed.

---

## 7. Data models (deltas from Xelulu §6)

```
UserProfile      { user_id, display_name, voice_sample_id, writing_style_id,
                   working_hours{}, vip_list[], urgent_rules[], tz }
Connection       { connection_id, user_id, provider, scopes[], permission_level,
                   oauth_token (encrypted), status, last_sync }
Meeting          { meeting_id, user_id, calendar_event_id, started_at, ended_at,
                   recording_consent, transcript_id, summary, action_items[] }
Transcript       { transcript_id, meeting_id, segments[], speakers[],
                   retention: {audio: "deleted"|"kept_30d", text: "kept"} }
ActionItem       { item_id, meeting_id, owner, text, due, status, applied_to:
                   calendar|mail|tasks|none }
Goal             { goal_id, user_id, period: "q"|"w", title, checkins[] }
DecisionEntry    { entry_id, user_id, decided_at, context, choice, revisit_at? }
CouncilMemo      { memo_id, user_id, question, lenses[], views[], synthesis }
MatterDevice     { device_id, user_id, matter_node, type, room, capabilities[] }
Rule             { rule_id, user_id, kind: vip|urgent|focus|hours, body }
```

`Device` + `Household` + `ChildProfile` (treat Parth owner as a `User` not a `ChildProfile`), `Session`, `Event`, `Consent` — reused from Xelulu §6.

---

## 8. Voice & conversation pipeline

Same as Xelulu §7, with Parth-specific additions:

1. **Wake word** on-device (same as Xelulu).
2. **Barge-in** — user can interrupt Parth mid-sentence; audio WS supports duplex.
3. **Tool-use loop** — the LLM has function-calling tools for every integration (`calendar.create_event`, `mail.send_draft`, `research.search`, `matter.scene`, `focus.start`, etc.). Each tool invocation that *writes* goes through the confirmation layer (§6.2).
4. **Multi-turn planning** — for research / drafting / council, the agent may take minutes. During long runs the glance screen shows "working on X" with progress.
5. **Latency budget** — same ≤1.2 s wake→first audio.

**Multiple personas** (Expert Council, Rehearsal Coach) are the same STT→LLM→TTS pipeline but with different system prompts / TTS voices. No new transport.

---

## 9. Device simulator (Parth profile)

Extend the Xelulu simulator (§9 of that spec) with a Parth profile toggle:

- Emits `screen_touch`, `tap_zone`, `bt_call`, `focus` events via a web UI.
- Accepts `screen`, `screen_toast`, `bt_call`, `focus`, `matter` commands and renders a mock HMI (a small HTML pane showing the current page + vars).
- Simulates BT-call state transitions (ringing → active → ended).
- Simulates Matter device acks (lights on/off, scenes).
- Offline toggle for queue-and-resync tests.

Deliverable in the same repo; CI uses it for feature end-to-end tests.

---

## 10. Safety, privacy & compliance (adult)

Different from Xelulu — this is an adult product with sensitive data (mail, calendar, meetings, finances).

- **Explicit consent for every connection.** OAuth scopes shown in plain language; revocable per-connection anytime.
- **Confirmation before irreversible actions.** Send, delete, pay, buy, book, share — always ask aloud + on the screen; voice-only confirmation requires a verified wake-phrase ("Parth, confirm").
- **Meeting recording consent.** Recording never starts silently (PA-M05). Any voice in the room can say "Parth, stop recording." Default retention: audio deleted after transcription.
- **Memory is user-visible.** Everything Parth "remembers" (preferences, decisions, approved style samples) is listed in the app and deletable per-item.
- **Household-locked voice samples** (writing style, voice-clone if used).
- **No open internet to the user** that doesn't go through the research agent's citation layer — Parth answers with sources, not opinions presented as facts.
- **Crisis protocol** (PA-E, personal topics) — calm response, encourages talking to a trusted person, shares helplines (India: Tele-MANAS 14416). No alerts to anyone else without explicit user choice.
- **Expert Council disclaimer** — every memo is clearly labelled "AI perspectives, not professional advice."
- **Encryption** — OAuth tokens, transcripts, memory encrypted at rest; access scoped by user.

---

## 11. App module — Parth

Adds to the shared household app:

| Area | Functions |
|---|---|
| Today | Agenda, tasks, follow-ups, meeting summaries, focus stats |
| Meetings | Transcripts, summaries, decisions, action items, approve-and-send |
| Council & mentor | Council memos, goals, weekly reviews, decision journal |
| Research & drafts | Reports, drafts, sources, export to connected tools |
| Connections | Per-service OAuth, permission levels, scopes, revoke |
| Rules | VIPs, urgent rules, focus rules, working hours, act-without-asking defaults (default: nothing irreversible) |
| Privacy | Memory list + delete, retention settings, recording policy, data export, account deletion |
| Device | Mic status, Matter hub status, BT pairing, updates, diagnostics |

Role: single-user owner (unlike Xelulu's parent/caregiver/grandma split). Household members may each own their own Parth; devices intercom via PA-H04.

---

## 12. Milestones & parallel delivery plan

Software workstreams that proceed with **zero hardware**, then integration gates.

| Phase | Software (no hardware needed) | Gate |
|---|---|---|
| **M0 — Contract delta freeze** | Sign off §3 of this doc (events + commands + HMI pages list) | §3 frozen, `proto_ver` 2 |
| **M1 — Simulator + gateway (Parth profile)** | Extend Xelulu simulator with Parth events/commands; mock HMI pane | Sim connects, screen/screen_touch round-trip |
| **M2 — Voice pipeline + tool-use loop** | Wake→STT→LLM(+tools)→TTS; confirmation layer; barge-in | Live tool-calling conversation via sim |
| **M3 — Chief of staff core** | Google + Microsoft connections, PA-C01/02/03/04/05/06 end-to-end against test accounts | User can get a brief + triage mail + create a calendar event via sim |
| **M4 — Meeting companion** | PA-M01/03/05: recording session, transcription, summary + action-item extraction, consent flow | Mock meeting → summary + action items reviewed in app |
| **M5 — Mentor + Council + Research + Drafting** | PA-C07/08, PA-E01/02/03/05 | Council memo generated end-to-end; research report with sources |
| **M6 — HMI GUI bundle v1** | Pages 5.1 designed in HMI editor; OTA bundle format; the simulator renders the pages | Any page + vars reproduce on the mock HMI faithfully |
| **M7 — Smart home + BT calls** | PA-H01 Matter-over-Wi-Fi; PA-C11 BT bridge via AT-command firmware stub | Toggle a Matter light; answer a mock BT call via screen |
| **M8 — Parent app Parth module** | §11 screens | User can drive the simulator end-to-end from the app |
| **M9 — Safety & privacy audit** | §10 full suite — connection revoke, retention, meeting consent, confirmations | Safety test suite green |
| **M10 — Integration** | Swap simulator for real device (hardware reaches its own M-gate) | Real device: brief + meeting + Matter scene + BT call |
| **M11 — Pilot** | OTA pipeline, telemetry, field fixes | Stable multi-day runtime |

Integration (M10) is low-risk **because** everything above the gateway was built and tested against the simulator — the only new variable is the physical device honoring the extended contract.

---

## 13. What is explicitly NOT in this version (software team: do not build)

- Any camera-dependent feature (PA-W01–W04, posture nudge, eye-contact feedback, walk-up detection by camera, speaker tracking by camera). Deferred to a future SBC Parth variant.
- Thread radio (Matter-over-Thread). Only Matter-over-Wi-Fi.
- Local LLM / on-device NLP beyond wake word. Everything else is cloud.
- Multi-user profiles on a single Parth. One owner per device (household intercom still works across devices).

---

*This spec covers Parth (ESP32-S3 cloud-gateway build, ages 20–45). It layers on the Xelulu spec — the gateway, auth, cloud infra, voice pipeline, OTA and app skeleton are shared. Sathi/WithU and Sarthi will follow the same pattern (reuse + delta spec).*
