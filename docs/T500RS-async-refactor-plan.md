# T500RS USB driver: async workqueue rationale and long‑term refactor plan

This note records why the T500RS USB backend uses an async work handler today, the small footprint change we made to reduce resources, and a concrete plan to remove the per‑device async layer in the future by refactoring the core scheduler.

## Current architecture and constraints

- The core scheduler (tmff2_work_handler in src/hid-tmff2.c) iterates over effects while holding tmff2->lock (spinlock/atomic context) and calls wheel callbacks: upload_effect, update_effect, play_effect, stop_effect.
- The T500RS USB backend transmits using usb_interrupt_msg(), which may sleep. Sleeping while a spinlock is held is illegal and will WARN/deadlock.
- To bridge these constraints, the driver enqueues operations into a per‑device queue and processes them in process context via a work handler (t500rs_io_work_handler). This ensures all USB I/O happens outside atomic context.
- The driver does not stream forces continuously; it only serializes discrete commands (upload/update/play/stop and parameter ops such as gain/range).

## Minimal change implemented now

- Instead of allocating a dedicated per‑device workqueue, the driver now uses the global system_highpri_wq for its io_work. This:
  - Removes a per‑device workqueue allocation and teardown
  - Keeps ordering and single‑worker semantics per device (one work_struct per device)
  - Preserves correctness under the existing core scheduler design

Code changes summary:
- queue_work(t500rs->usb_wq, &io_work) -> queue_work(system_highpri_wq, &io_work)
- Removed alloc_workqueue/destroy_workqueue; INIT_WORK still used; destruction now flush_work(&io_work)

## Long‑term proposal: remove the async layer entirely

Goal: Call wheel callbacks from tmff2_work_handler outside of tmff2->lock so they can perform synchronous, possibly sleeping I/O safely. Then remove the T500RS op queue and worker.

### Proposed design

1) Split scheduling into two phases per timer tick:
   - Phase A (under tmff2->lock, atomic):
     - Evaluate each effect’s state and compute required operations (UPLOAD, UPDATE, START, STOP) and any param changes. Do not perform I/O.
     - Build a small vector/list of operations (struct tmff2_op { type, effect snapshot, params }) for this tick.
     - Clear or adjust the state flags optimistically, or mark them as "pending" to be finalized after Phase B.
   - Phase B (process context, no spinlock held):
     - Iterate the prepared operation list and invoke tmff2->upload_effect/update/play/stop directly. These calls may sleep.
     - Record success/failure per op.
   - Phase C (brief lock reacquire):
     - Reacquire tmff2->lock and finalize flags based on outcomes (re‑set flags on failure if retry semantics desired; clear flags on success).

2) Concurrency and idempotency:
   - The op list for a tick is immutable once built; subsequent ticks build a fresh list.
   - Make device callbacks tolerant of duplicate calls (e.g., STOP when already stopped). This is already broadly true.

3) hid_hw_wait pacing:
   - Today the core calls hid_hw_wait(hdev) between effects while still in the atomic loop. After the split, pacing can happen between I/O calls in Phase B, where sleeping is permissible.

4) Apply to all wheels:
   - The change is in src/hid-tmff2.c and benefits all backends. Backends that already do synchronous I/O can remain unchanged; backends with private workers (like T500RS) can be simplified.

### Step‑by‑step implementation plan

- Step 1: Introduce a tmff2_ops scratch buffer per tmff2 (allocated once), and refactor tmff2_work_handler into the three phases described above, maintaining current behavior and logs.
- Step 2: Convert T500RS backend to wire tmff2->upload_effect/update/play/stop directly to the blocking t500rs_* implementations (not the queue_* wrappers). Keep the queue code temporarily compiled but unused.
- Step 3: After validation across devices, remove the T500RS op queue, work_struct, and any remaining async wrappers and comments.
- Step 4: Validate pacing and throughput (no USB queue overrun) across:
  - Constant force bursts (0x03), periodic, spring/damper/friction uploads and starts/stops
  - High‑frequency update scenarios (e.g., AMS2 constant force updates)
  - Gain/range changes and autocenter transitions

### Testing strategy

- Unit/functional tests at the driver layer are limited; rely on:
  - Tracing/logging at log level 2/3 (USB TX dump, unknown‑only mode)
  - Manual validation on hardware (T500RS) while running titles known to exercise paths: LFS, AMS2, AC
  - Ensure no kernel WARNs about sleeping in atomic context
  - Confirm no lost or out‑of‑order commands under effect spam

### Risks and mitigations

- Risk: Flag/state races if finalize step is incorrect. Mitigation: Keep a clear contract for flag transitions; only clear flags after successful I/O; re‑queue on error.
- Risk: Throughput reduction if I/O becomes serialized improperly. Mitigation: Preserve ordering guarantees; rely on hid_hw_wait pacing similar to today.
- Risk: Behavior changes for other wheels. Mitigation: Guarded roll‑out; test T300/TX/TS‑XW; be prepared to re‑enable per‑device async only for affected backends.

### Rollback plan

- The change is localized to tmff2_work_handler and backend wiring. If regressions occur, revert to the per‑device async pattern for the affected backend(s) and/or revert the core split.

### Expected benefits

- Simpler per‑device code (remove queue and worker)
- Fewer kernel workqueue objects and less overhead
- Clearer control flow and easier debugging (I/O happens where it’s scheduled)

---

Status: minimal change (global high‑pri workqueue) implemented; long‑term core split remains TODO.

