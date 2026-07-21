# LiteRTOS

A lightweight preemptive RTOS kernel implemented from scratch for STM32F407xx (Cortex-M4).  
Built as a deep-dive into embedded systems internals — no HAL abstractions, no black boxes.

---

## Platform

| Item | Detail |
|------|--------|
| MCU | STM32F407xx Cortex-M4|
| IDE | Keil MDK |
| Language | C |

---

## Architecture

```
┌─────────────────────────────────────────────┐
│                 Application                 │
├──────────┬──────────┬───────────┬───────────┤
│ Semaphore│  Mutex   │EventFlags │   Queue   │
├──────────┴──────────┴───────────┴───────────┤
│          Task Notify │ MemPool  │   Timer   │
├─────────────────────────────────────────────┤
│                  Scheduler                  │
├─────────────────────────────────────────────┤
│               Linked List                   │
└─────────────────────────────────────────────┘
```

---

## Features

### Scheduler
- **Priority-based preemptive scheduling** — 32 priority levels, lower number = higher priority
- **Round-robin time slicing** — 10ms time slice for same-priority tasks
- **Delay queue** — sorted linked list, O(1) tick processing
- **Stack overflow detection** — magic number guard + PSP range check on every SysTick
- **Idle task** with user-overridable `Idle_Hook`

### Synchronization
- **Semaphore** — counting semaphore, blocking / non-blocking / timeout
- **Mutex** — recursive lock with **priority inheritance**, prevents priority inversion
- **Event Flags** — 32-bit event word, `WAIT_ANY` / `WAIT_ALL` modes, auto-clear on wakeup
- **Task Notification** — zero-overhead per-task 32-bit notify value, ISR-safe

### Communication
- **Message Queue** — ring buffer + semaphore, blocking / timeout / peek / flush  
  Supports **DMA transfer** for large items (≥128 bytes, 4-byte aligned)

### Memory
- **Memory Pool** — fixed-size block allocator, O(1) alloc/free, no fragmentation,  
  wild-pointer guard via range + alignment check

### Timers
- **Software Timer** — one-shot / auto-reload, sorted insertion for O(1) tick processing,  
  callback via `Scheduler_TickHook` (no circular dependency)



---

## Module Summary

| Module | File | ISR-safe |
|--------|------|----------|
| Scheduler | `scheduler.c/h` | SysTick / PendSV |
| Linked List | `list.c/h` | — |
| Semaphore | `semaphore.c/h` | `Sem_Post` only |
| Mutex | `mutex.c/h` | ✗ |
| Event Flags | `event_groups.c/h` | `EventFlags_Set` only |
| Task Notify | `task_notify.c/h` | `Task_NotifySet` only |
| Message Queue | `queue.c/h` | ✗ |
| Memory Pool | `mempool.c/h` | ✅ |
| Software Timer | `timer.c/h` | — |

---

## Design Notes

**Priority inheritance** — When a high-priority task blocks on a mutex held by a lower-priority  
task, the owner's priority is temporarily elevated to prevent priority inversion.

**Dual list nodes** — Each TCB carries two list nodes: `wait_node` (for sync object queues)  
and `delay_node` (for the delay list). A `BLOCKED_TIMEOUT` task lives in both simultaneously.

**Self-pointer sentinel** — Task notification uses `waiting_on = tcb` (self-reference) to mark  
"waiting for notification" without adding a new TCB field or state.

**Zero circular dependency** — Timer module hooks into the scheduler via `__weak Scheduler_TickHook`  
rather than a direct include, keeping the dependency graph acyclic.

---

## Stack Sizing Guide

| Task type | Recommended stack |
|-----------|------------------|
| Simple LED blink | 64 words (256B) |
| UART / printf | 256 words (1KB) |
| Complex logic | 512+ words (2KB+) |

---

## License

MIT
