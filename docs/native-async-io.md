# Native asynchronous I/O design

## Why this is needed

The current `native/fs_plugin.c` returns Promise objects, but performs the
filesystem operation before returning from the native function. The Promise
is therefore already settled (or rejected) when JavaScript receives it.
This is Promise-shaped synchronous I/O, not asynchronous I/O. Large reads,
slow flash storage, recursive directory walks, and sync writes can still block
the Falcon/QuickJS owner thread.

The same restriction applies to the current `Readex.openBook`,
`Book.getChapter`, `Book.search`, and `Readex.paginate` bindings: they call
the core synchronously. They must not be described as non-blocking merely
because callers use `await`.

## Required execution model

1. The QuickJS owner thread validates arguments and copies input into native
   task-owned memory. It must not perform file I/O, PBKDF2, encryption,
   decompression, parsing, or full-text search.
2. A bounded native worker queue executes the operation. Workers may use POSIX
   and core C/C++ code, but must never call QuickJS APIs or retain
   `JSContext*`, `JSRuntime*`, `JSValue`, or JS-owned string pointers.
3. Workers publish plain C/C++ result records to a mutex-protected completion
   queue.
4. Only the QuickJS owner thread converts completed records to JS values and
   resolves/rejects Promises.
5. Completion delivery must use a host-supported owner-thread dispatcher or
   wakeup primitive. If the host exposes no such primitive, a low-frequency
   JS timer may poll a nonblocking native completion function. A worker must
   never call `JS_EnqueueJob`, `JS_Call`, or Promise resolvers directly.
6. Queue capacity and worker count must be bounded. Overload returns a
   deterministic error rather than allocating unbounded tasks.
7. Runtime shutdown must stop submissions, drain or safely abandon work, join
   workers, and release all copied secrets/results before destroying runtime
   state. Detached workers that can outlive module/runtime state are forbidden.

## Proposed API shape

Keep existing synchronous methods for compatibility and introduce explicit
async variants so existing callers do not silently change semantics:

- `fs.readFileAsync(path) -> Promise<string>`
- `fs.writeFileAsync(path, data) -> Promise<void>`
- `fs.readdirAsync(path, options?) -> Promise<Entry[]>`
- `Readex.openBookAsync(path) -> Promise<Book>`
- `Book.getChapterAsync(index) -> Promise<Chapter>`
- `Book.searchAsync(query, options?) -> Promise<Result[]>`
- `Readex.paginateAsync(content, config) -> Promise<PageInfo[]>`

The async API must be backed by the host's actual event-loop integration, not
by wrapping the current synchronous call in `Promise.resolve` or an already
settled Promise.

## Compatibility and validation

- Preserve the existing `custom_init_jsapis` / `registerCModuleLoader`
  module ABI and current module names.
- Validate the exact Falcon host dispatcher/teardown contract before wiring
  completion delivery. The vendored QuickJS header alone does not define a
  thread-safe host wakeup API.
- Add host tests for worker completion, queue saturation, I/O failures,
  runtime shutdown with pending work, and proof that JS callbacks execute on
  the owner thread.
- Device acceptance: keep touch/scroll and unrelated JS timers responsive
  during a deliberately slow read; measure worst-case callback latency and
  worker shutdown behavior on the X5.

## Current status

This document records the audit and implementation contract. The existing
Promise-returning `fs_plugin.c` functions remain synchronous until a
host-supported completion dispatcher and lifecycle hook are confirmed.
