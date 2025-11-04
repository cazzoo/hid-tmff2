## PR #175 (Kimplul/hid-tmff2) — Comprehensive Feedback Analysis and Action Plan

Scope
- Source PR: https://github.com/Kimplul/hid-tmff2/pull/175 (T500RS USB interrupt-transfer driver)
- Includes all review comments from Pull Request Review #3432151994 (CHANGES_REQUESTED) and all subsequent comments (review comments, further reviews, and issue comments) posted after that review.
- Data points (with links):
  - Review #3432151994 (OWNER, CHANGES_REQUESTED): https://github.com/Kimplul/hid-tmff2/pull/175#pullrequestreview-3432151994
  - Review #3433013848 (CONTRIBUTOR, CHANGES_REQUESTED): https://github.com/Kimplul/hid-tmff2/pull/175#pullrequestreview-3433013848
  - Review #3434140085 (OWNER, COMMENTED): https://github.com/Kimplul/hid-tmff2/pull/175#pullrequestreview-3434140085
  - Review #3434197753 (OWNER, COMMENTED): https://github.com/Kimplul/hid-tmff2/pull/175#pullrequestreview-3434197753
  - Issue comment (OWNER): https://github.com/Kimplul/hid-tmff2/pull/175#issuecomment-3482005499
  - Issue comment (author): https://github.com/Kimplul/hid-tmff2/pull/175#issuecomment-3499579701

Last updated: 2025-11-07

---

### 1) Chronological feedback (from Review #3432151994 onward)

A. Udev rules (udev/99-thrustmaster.rules)
- Remove init-mode rules (shared across wheels, not useful)
  - Comment: “The init mode is shared… Remove these rules, they’re not T500-specific and not really useful.”
  - Link: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2502046211
- Move sysfs attribute permission rules to Oversteer; simplify overall rule set
  - Comment: Oversteer is the better place for sysfs rules; many MODE/TAG rules likely unnecessary.
  - Link: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2502050778
- Simplify PC-mode rules; verify jscal “magic numbers” and deadzone behavior
  - Comment: Other wheels only use the two RUN rules; check if jscal rule actually works (use jstest-gtk); uaccess generally set via SteamDeck rule on hidraw.
  - Link: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2502071562
- Remove shell artifacts
  - Comment: “Also, unnecessary sleep 0.5, 2>/dev/null and || true.”
  - Link (reply in same thread): https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2502091864

B. Build helper script (tools/build-reload.sh)
- Move to its own PR; reduce sudo; remove unnecessary sysfs cleanup; remove random sleeps or replace with proper readiness checks
  - Comment: “If you'd like to add some kind of helper script … open a separate PR… having to run the whole thing as sudo … sysfs cleanup is not necessary … random sleeping …”
  - Link: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2502099655

C. Header/API (src/hid-tmff2.h)
- Remove init-mode Product ID (b65d) definition (not T500-specific; not useful for this driver)
  - Link: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2502108296
- Remove unused function pointers (set_spring_level, set_damper_level, set_friction_level); rely on global spring/damper/friction levels managed by hid-tmff2.c
  - Link: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2502123548

D. Force-feedback implementation details (src/tmt500rs/hid-tmt500rs-usb.c)
- Replay length/delay not accounted for (except ramp length)
  - Comment (CONTRIBUTOR): “Replay length and delay is never taken into account”
  - Link: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2503631302
- Owner’s follow-ups in later reviews:
  - Acknowledge correctness of a code-level catch and that it applies to other uploads (COMMENTED review)
    - Link: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2503717885
  - Ramp likely emulated via long sawtooth; docs should highlight this relationship
    - Link: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2503764279

E. Review body texts to incorporate
- Review #3432151994 (OWNER): “Quite a lot of comments … check how you're handling effect updates.”
- Review #3433013848 (CONTRIBUTOR): Summary points (do not duplicate every inline):
  - Many instances using effect ID = 0 instead of effect->id
  - Envelopes missing/static in many places
  - “99% static packets with not explained values” — document why/what; prefer structs over raw buf[n]
  - Replay length/delay not used (except ramp length)
  - Document packet formats and ordering (see docs/FFBEFFECTS.md for T300)

F. Issue comments
- Owner (2025-11-03):
  - Fix work handler critical section; rebase on commit 5095d47; use usb_interrupt_msg() directly (drop second workqueue); curiosity about hid_hw_request with T500
  - Avoid AI-generated commits with mismatched messages or noisy changes; keep history clean
  - Link: https://github.com/Kimplul/hid-tmff2/pull/175#issuecomment-3482005499
- Author (2025-11-06):
  - Notes: adjusted gain/autocenter to avoid kernel crash; added per-driver log-level; added driver version/build; udev rules are complex; build-reload.sh helpful
  - Link: https://github.com/Kimplul/hid-tmff2/pull/175#issuecomment-3499579701

---

### 2) Classified categories and consolidated feedback

1. Udev Rules
- Remove init-mode (044f:b65d) rules
- Keep only minimal PC-mode rules (RUN evdev-joystick and jscal) if truly needed; otherwise consider trimming
- Consider moving sysfs attribute permissions to Oversteer
- Validate jscal parameters with jstest-gtk; ensure deadzone expectations
- Drop shell artifacts: sleeps, redirections, “|| true”

2. Build/Dev Tooling
- Move tools/build-reload.sh to separate PR
- Minimize sudo; avoid unnecessary sysfs cleanup; replace sleeps with condition checks if required

3. Public API/Header Hygiene
- Remove TMT500RS_INIT_ID (0xb65d)
- Remove unused function pointers in tmff2_device_entry; rely on global spring/damper/friction

4. FFB Implementation Correctness
- Handle replay length and delay properly across effects
- Use correct effect IDs (avoid hardcoded 0) where the device expects per-effect addressing
- Implement/propagate envelopes (attack/fade) consistently
- Review periodic magnitude/sign handling; constant force sign/direction; period=0 default behavior
- Document packet formats and byte meanings; reduce unexplained magic
- Consider struct-based packet layouts for clarity and safety
- Ramp likely emulated via sawtooth — reflect in code comments and docs

5. Architecture/Concurrency
- Rebase on 5095d47; refactor work handler: keep critical section minimal, perform USB after it
- Prefer usb_interrupt_msg() approach; remove extra workqueue

6. Code Quality/Process
- Clean commit history; accurate messages; remove AI/noise artifacts
- Add/adjust logging thoughtfully (discuss making log-level global later)
- Version string/build number: either generalize repo-wide or drop; avoid per-device-only infra unless agreed

---

### 3) Detailed action plan (per feedback item)

Udev rules (HIGH)
- What: Remove init-mode rules; simplify PC-mode rules; migrate sysfs perms to Oversteer; drop shell artifacts; validate jscal.
- Why: Init-mode not T500-specific; simpler, consistent rule set; avoid brittle shell tricks; ensure correct deadzone behavior.
- How:
  1) Delete all rules for idProduct b65d.
  2) Keep only necessary PC-mode rules; consider only the two RUN lines (evdev-joystick, jscal) if validated; otherwise drop.
  3) Remove MODE/TAG rules if redundant with existing system rules (e.g., SteamDeck hidraw uaccess).
  4) Remove sleeps, 2>/dev/null, and “|| true”.
  5) Test jscal via jstest-gtk; adjust numbers as needed; document rationale.
- Priority: High
- Response strategy: Acknowledge and fix; ask for confirmation on minimal rule set and whether to move sysfs perms to Oversteer.

Build helper script (MEDIUM)
- What: Move tools/build-reload.sh to separate PR; reduce sudo; remove sysfs cleanup; avoid sleeps.
- Why: Keep PR focused; safer dev ergonomics; avoid brittle timing.
- How: Extract script to standalone PR; run non-root parts unprivileged; drop sysfs cleanup; if timing needed, wait for device nodes (udev settle) rather than sleep.
- Priority: Medium
- Response: Acknowledge and fix (separate PR); ask if maintainers want to keep such a tool in-tree.

Header/API (HIGH)
- What: Remove TMT500RS_INIT_ID; remove unused function pointers.
- Why: Not device-specific/useful; unused APIs mislead and add maintenance burden.
- How: Delete defines and struct fields; ensure callers use global levels from hid-tmff2.c; update build.
- Priority: High
- Response: Acknowledge and fix.

FFB: Replay timing (HIGH)
- What: Respect effect.replay.length and effect.replay.delay for all effect types.
- Why: Timing semantics expected by games; correctness.
- How: In upload/update paths, translate ms to device fields; in play/stop logic ensure delay applied before start and length stops effect as required (or program device timers). Where not supported natively, emulate with delayed start/stop commands.
- Priority: High
- Response: Acknowledge and fix.

FFB: Effect IDs and envelopes (HIGH)
- What: Use effect->id consistently (unless device specifically forces ID 0 for an init phase); implement non-static envelopes.
- Why: Correct addressing; expected attack/fade.
- How: Audit all upload paths; replace hardcoded 0 with effect->id where appropriate; plumb envelope params from ff_effect; document any exceptions with links to captures/specs.
- Priority: High
- Response: Acknowledge and fix; ask for device-specific exceptions confirmation where Windows-like sequences require ID 0.

FFB: Packet structure, magic bytes, and documentation (MEDIUM)
- What: Replace scattered buf[n] with packed structs; document packet formats/ordering; comment unexplained bytes; align with docs/FFBEFFECTS.md style.
- Why: Maintainability, reviewability, fewer mistakes.
- How: Define C structs with fixed-size fields; add comments for reserved/unknown bytes; centralize helpers for main/envelope/param packets; update docs with T500 specifics and ramp=sawtooth note.
- Priority: Medium
- Response: Acknowledge and fix; propose initial doc draft for review.

Architecture/Concurrency (HIGH)
- What: Rebase on 5095d47; refactor work handler to keep critical section minimal and move USB to post-section; then use usb_interrupt_msg() directly; remove secondary workqueue.
- Why: Avoid latent concurrency bugs; simpler logic; matches maintainer guidance.
- How: Rebase; adapt code to new handler pattern; drop extra queue; validate with smoke tests.
- Priority: High
- Response: Acknowledge and fix.

Code quality/process (MEDIUM)
- What: Clean up commit messages; remove AI/noise; discuss global log-level and versioning strategy before keeping per-device versions.
- Why: History clarity; consistent repo-wide conventions.
- How: Squash/reword; optionally move log-level/versioning to a dedicated follow-up PR or generalize across devices.
- Priority: Medium
- Response: Acknowledge and fix; ask preference on log-level/versioning (global vs drop).

---

### 4) Implementation checklist (trackable)

- [x] Udev: Remove 044f:b65d init-mode rules (implemented in feature/pr175-phase1-udev-header @ dbc65c2)
- [x] Udev: Trim PC-mode rules to minimal set (drop RUN hooks; add SteamDeck hidraw uaccess for b65e); validate RUN necessity via user testing
- [x] Udev: Remove sleeps, redirections, and “|| true” (implemented by removing sysfs chmod rule; commit dbc65c2)
- [x] Udev: Validation via jstest-gtk — OK; no default jscal needed
- [ ] Udev: Consider moving sysfs permissions to Oversteer (confirm with maintainer)
- [x] Script: Move tools/build-reload.sh to separate PR (branch chore/build-reload-refactor); removed from this branch
- [x] Script: Reduce sudo; drop sysfs cleanup; replace sleeps with proper checks (tools/build-reload.sh: add --build-only, remove sysfs cleanup, remove init modules, replace sleeps with check loop, gate sudo to module ops)
- [x] Header: Remove TMT500RS_INIT_ID (0xb65d) (commit dbc65c2)
- [x] Header: Remove unused set_*_level function pointers; use global levels (commit dbc65c2; T500RS updated to use global spring/damper/friction levels)
- [x] FFB: Implement replay length and delay handling (core scheduler enforces delay before START and triggers STOP after length; device-level timers TBD)
- [x] FFB: Audit and fix effect ID usage — T500RS requires EffectID=0 for Report 0x01 main uploads and 0x41 START/STOP; some secondary 0x01 tolerated with effect->id (commits 20218a9, 675485b)
- [x] FFB: Implement envelope handling (attack/fade) across effects — constant/periodic/ramp now populate Report 0x02 (0x1c) with attack_length/level and fade_length/level (commit 6c3978d)
- [x] FFB: Audit constant/periodic magnitude and sign handling; clamp scaling and default period=100ms when 0 (commit cb6dd7e)
- [x] FFB: Convert packet buffers to typed structs (0x01/0x02/0x03/0x04/0x41); comment unknown bytes in 0x01
- [x] Docs: Document T500RS packet formats and ordering; reference ramp as sawtooth emulation (docs/FFBEFFECTS.md)
- [X] Arch: Rebase on 5095d47; refactor work handler; use usb_interrupt_msg(); remove extra workqueue (already done by the past)
- [ ] Process: Clean commit history/messages
- [x] Process: Versioning strategy decided — global module version (TMFF2_DRIVER_VERSION via Makefile/Kbuild; MODULE_VERSION exported)
- [x] Process: Log-level scope decided — no repo-wide mechanism; removed per-driver log level and using standard kernel logs/dynamic debug


---

### Udev validation: jscal via jstest-gtk (T500RS)

Goal
- Confirm we do NOT need to ship a jscal calibration for T500RS PC mode (044f:b65e)
- If calibration is desired by a user, document how to generate and apply it locally without RUN hooks

Procedure
1) Ensure updated udev rules are active
   - Install repo rules as needed, then reload: sudo udevadm control --reload && sudo udevadm trigger
   - Unplug/replug the wheel
2) Inspect the joystick device
   - Run: jstest-gtk and select the Thrustmaster T500RS device (or run jstest /dev/input/jsX)
   - Expected: axes show full range (~-32767..32767), center near 0, no large hard deadzone enforced by system
3) Check in-game behavior (optional)
   - For Steam/Proton titles with multiple js devices, use event device: SDL_JOYSTICK_DEVICE=/dev/input/eventXX %command%
4) If you need calibration anyway
   - Use jstest-gtk to calibrate; it will show the corresponding jscal -s ... string
   - Prefer applying per-user at session start or via Oversteer; avoid shipping a global RUN jscal in udev

Expected outcome
- T500RS operates normally without a baked-in jscal string; keeping udev minimal avoids races and unwanted global calibration
- If your environment shows significant offset/deadzone, capture the jscal string from jstest-gtk and report it here so we can document it as an optional example (not default)

Result (user validation): jstest-gtk shows normal ranges/centers without jscal; leaving udev minimal (no RUN jscal).


---

### 5) Response strategy per thread (examples)

- r2502046211 (udev/init-mode): Acknowledge and fix — remove init-mode rules; keep T500-specific scope.
- r2502050778 (udev/sysfs perms): Acknowledge and fix — move sysfs perms to Oversteer; trim rules; ask for confirmation on minimal set.
- r2502071562 (udev/PC-mode+jscal): Acknowledge and fix — simplify rules; verify jscal via jstest-gtk and report findings.
- r2502091864 (udev/shell artifacts): Acknowledge and fix — remove sleeps/redirections/“|| true”.
- r2502099655 (build script): Acknowledge and fix — move to separate PR; reduce sudo; remove sysfs cleanup and sleeps.
- r2502108296 (header/init ID): Acknowledge and fix — remove 0xb65d define.
- r2502123548 (header/unused function pointers): Acknowledge and fix — remove pointers; rely on global levels.
- r2503631302 (FFB/replay timing): Acknowledge and fix — implement replay length/delay handling across effects.
- r2503717885 (owner follow-up): Implemented/Will implement wherever applicable — audit similar upload paths; add tests.
- r2503764279 (ramp ~ sawtooth, docs): Acknowledge and fix — update docs; comment code accordingly.
- Review #3432151994 body: Acknowledge — will re-audit effect update handling explicitly.
- Review #3433013848 body: Acknowledge — will address effect IDs, envelopes, doc/structs, replay timing.
- Issue #3482005499 (owner): Acknowledge — will rebase on 5095d47; refactor work handler; use usb_interrupt_msg(); avoid AI/noise commits.
- Issue #3499579701 (author notes): Provide clarifications — propose global log-level discussion; align versioning; simplify udev; extract build script.

---

Appendix: References
- Review #3432151994: https://github.com/Kimplul/hid-tmff2/pull/175#pullrequestreview-3432151994
- Review #3433013848: https://github.com/Kimplul/hid-tmff2/pull/175#pullrequestreview-3433013848
- Review #3434140085: https://github.com/Kimplul/hid-tmff2/pull/175#pullrequestreview-3434140085
- Review #3434197753: https://github.com/Kimplul/hid-tmff2/pull/175#pullrequestreview-3434197753
- Issue comment #3482005499: https://github.com/Kimplul/hid-tmff2/pull/175#issuecomment-3482005499
- Issue comment #3499579701: https://github.com/Kimplul/hid-tmff2/pull/175#issuecomment-3499579701
- Example threads:
  - r2502046211: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2502046211
  - r2502050778: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2502050778
  - r2502071562: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2502071562
  - r2502091864: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2502091864
  - r2502099655: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2502099655
  - r2502108296: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2502108296
  - r2502123548: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2502123548
  - r2503631302: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2503631302
  - r2503717885: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2503717885
  - r2503764279: https://github.com/Kimplul/hid-tmff2/pull/175#discussion_r2503764279

