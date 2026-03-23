# Code Review — Mar 19
`hid_host_example.c` — FreeRTOS best practices

---

## What you're doing well

- **Queue-based ISR handoff** (`gpio_isr_cb`) is correct. You're using `xQueueSendFromISR` with a proper `xTaskWoken` and `portYIELD_FROM_ISR()`. This is exactly the right pattern.
- **Stream buffer trigger level of 1** is correct for char-by-char reading in `scan_reader_task`.
- **Queue depth of 1 for `scannedID_queue`** is a clean way to enforce "one request in flight" without needing a semaphore or flag.
- **Task notification** to synchronize `usb_lib_task` startup (`xTaskNotifyGive` / `ulTaskNotifyTake`) is the right primitive for a one-shot "ready" signal.
- **Structs as queue items** rather than raw pointers — this is correct. FreeRTOS queues copy by value, so passing pointers to stack variables would be a bug. You're doing the right thing.

---

## Issues and observations

### 1. `portMAX_DELAY` in `xStreamBufferSend` — called from interrupt context (line 177)

`hid_keyboard_print_char` calls `xStreamBufferSend(..., portMAX_DELAY)`. This function is called from `key_event_callback` → `hid_host_keyboard_report_callback` → `hid_host_interface_callback`, which fires in the USB interrupt/callback context. **You must never block in interrupt context.** If the stream buffer is full, this will hang the USB stack.

Use a timeout of `0` here, or better yet `pdMS_TO_TICKS(0)`. Dropped bytes are much safer than a hung interrupt.

### 2. `ascii_stream_buff` size is 10 bytes (line 595)

A card scan is 8 characters + `\r`. That's 9 bytes minimum. With a buffer of 10 you have exactly 1 byte of slack — if any two keystrokes arrive before `scan_reader_task` drains them, you overflow. Increase this to at least 32 or 64.

### 3. Admin scan sends to **both** queues (lines 453–454)

```c
if (is_admin(...)) {
    xQueueSend(access_state_queue, &command, portMAX_DELAY);
    xQueueSend(scannedID_queue, &cur_msg, portMAX_DELAY);  // also here?
}
```

Admin scans are sent to both queues. Was this intentional? If the intent is "admin = local LED grant, no HTTP request needed," the second send to `scannedID_queue` looks like a leftover. If it's intentional (admin also triggers HTTP), the comment on the else branch ("// TODO: send buffer through msg queue") is now misleading.

### 4. `scan_read_created` is never checked (line 596)

`task_created` for `usb_lib_task` is asserted (line 594), but `scan_read_created` is never checked. A failed task creation returns `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY` and the task silently doesn't exist. Add `assert(scan_read_created == pdTRUE)`.

### 5. `scannedID_Queue` local variable shadows the global (line 554)

```c
BaseType_t scannedID_Queue;  // local, never used
```

This local variable is declared but never used, and its name closely shadows the global `scannedID_queue` (different case). This is a readability hazard and a compiler warning waiting to happen. Remove it.

### 6. `access_state_queue` created before stream buffer, tasks created before queue (lines 557–605)

Initialization order in `app_main` is scattered:
- `access_state_queue` and `scannedID_queue` created at top (lines 557–558) ✓
- `ascii_stream_buff` created at line 595, after `usb_lib_task` is already running
- `app_event_queue` created at line 627, **after** `hid_host_install` at line 624

`hid_host_install` registers the device callback, which calls `xQueueSend(app_event_queue, ...)`. If a device connects between lines 624 and 627, `app_event_queue` is still NULL and the send is silently dropped. Create all queues and buffers **before** installing any drivers or creating any tasks that depend on them.

### 7. `update_led_task` and `receive_ID_task` use `xTaskCreate`, others use `xTaskCreatePinnedToCore` (lines 603–605)

`xTaskCreate` lets FreeRTOS schedule the task on either core. For tasks that call `gpio_set_level` or `printf`, this is generally fine, but it's worth being deliberate. On ESP32-S3/S2, core 0 runs the USB/WiFi stack — keeping your application tasks on core 1 avoids contention. Consider pinning application tasks to core 1 explicitly.

### 8. `static` locals in `scan_reader_task` (lines 439–440)

```c
static scan_buffer_received cur_msg;
static char msg_buff[64] = {0};
```

Making these `static` means they persist across calls and live in BSS/data rather than the stack. For a task that never returns, this works fine — but it also means if you ever created two instances of this task, they'd share the same buffers (a bug). For a single-instance task it's fine, but be aware of this constraint. The `pos` variable on the stack (line 441) is correct by contrast.

---

## Summary table

| Issue | Severity | Location |
|---|---|---|
| `portMAX_DELAY` in interrupt-context send | **High** | line 177 |
| Stream buffer too small | **Medium** | line 595 |
| Admin path sends to both queues | Medium (logic bug?) | lines 453–454 |
| `scan_read_created` unchecked | Medium | line 596 |
| `app_event_queue` created after `hid_host_install` | Medium | lines 624–627 |
| Unused `scannedID_Queue` shadows global | Low | line 554 |
| Unpinned tasks | Low | lines 603–605 |
| `static` locals limit to single instance | Low/informational | lines 439–440 |
