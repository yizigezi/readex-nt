# Native asynchronous I/O audit and implementation contract

## Audit finding

The current `native/fs_plugin.c` Promise-returning methods perform filesystem work before returning the Promise. They are Promise-shaped synchronous calls: awaiting them does not prevent the QuickJS/Falcon owner thread from blocking.

This matters for large book files, slow flash storage, directory scans, and writes. The same audit must be applied to book-opening, chapter-loading, search, and pagination bindings before describing those operations as non-blocking.

## Required execution model

1. The QuickJS owner thread performs only lightweight argument validation and copies inputs into task-owned native memory.
2. A bounded native worker queue performs filesystem I/O and other expensive native work.
3. Workers must never access `JSRuntime*`, `JSContext*`, `JSValue`, JS-owned strings, or any QuickJS API.
4. Workers publish plain native result records to a synchronized completion queue.
5. Only the QuickJS owner thread converts results to JS values and resolves/rejects Promises.
6. Completion delivery must use a Falcon-provided owner-thread dispatcher/wakeup mechanism. If none exists, use a low-frequency JS-side poll of a nonblocking native completion function as a fallback; do not call QuickJS from a worker.
7. Bound both worker count and pending queue size. Reject overload deterministically.
8. Runtime teardown must stop submissions, prevent callbacks into a destroyed context, join workers, and securely release pending buffers. Detached workers that outlive runtime/module state are forbidden.

## Compatibility strategy

Keep existing synchronous API behavior unchanged for callers that depend on it. Add explicit asynchronous methods rather than silently changing semantics:

- `fs.readFileAsync(path)`
- `fs.writeFileAsync(path, data)`
- `fs.readdirAsync(path, options?)`
- `Readex.openBookAsync(path)`
- `Book.getChapterAsync(index)`
- `Book.searchAsync(query, options?)`
- `Readex.paginateAsync(content, config)`

Each method must return a Promise whose work is actually performed by a worker. Do not use `Promise.resolve(syncOperation())`, an already-settled Promise, or a timer around a synchronous operation.

## Required host integration investigation

Before implementing completion delivery, identify the actual Falcon event-loop integration and native module lifecycle hooks used by this repository's `custom_init_jsapis()` and `registerCModuleLoader()` path.

The vendored QuickJS API alone does not establish a thread-safe way to wake the owning event loop. Do not assume `JS_EnqueueJob` is safe from a worker thread. If the host exposes no dispatcher, implement and test a main-thread polling fallback.

## Tests and device acceptance

- Worker success and error propagation.
- Queue saturation and deterministic overload errors.
- Large-file read while unrelated JS timers and touch interaction remain responsive.
- Runtime shutdown while tasks are pending; no use-after-free or callback into a destroyed context.
- Verify all JS callbacks run on the owner thread.
- Measure X5 latency and memory use with representative book sizes.
- Confirm old synchronous APIs remain compatible.

## Scope of this audit

This document records the issue and the required implementation contract. It does not claim that existing filesystem or book APIs have already been converted to true asynchronous operations.
