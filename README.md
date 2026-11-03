# BinaryTree

A header-only C++20 binary tree library where node references can never
dangle, never alias, and never cross tree boundaries — by design, not by
discipline.

- Single header: `include/binary_tree/binary_tree.hpp`
- Namespace: `binary_tree`
- Requires: C++20, no external dependencies

## Table of contents

1. [Why this library exists](#why-this-library-exists)
2. [Quick start](#quick-start)
3. [Commands reference](#commands-reference)
4. [Architecture](#architecture)
5. [Contracts and requirements](#contracts-and-requirements)
6. [Pitfalls](#pitfalls)
7. [Performance](#performance)
8. [Documentation](#documentation)
9. [Usage examples](#usage-examples)

## Why this library exists

Raw-pointer binary trees fail in ways that are silent until they crash.
This library removes each failure mode structurally — the API makes the
bug impossible to express, rather than asking you to remember not to
write it.

### The core idea in one picture

```
   Raw-pointer tree (fragile)              This library (safe)
   =========================               ===================

   Node* root ──► Node                     Handle root = { 0, 1, 7 }
                    ├── left  ──► Node                      │
                    └── right ──► Node                      │  index + generation + tree id
                                                                │
                                                                ▼
   Problem: pointers die when              Resolve(index) ──► m_Slots[0]
   the tree moves or reallocates.                          m_Generations[0] == 1?  yes
                                                            m_TreeId == 7?          yes
                                                            → valid, return value

                                           Problem solved: no pointer to die.
```

### What changes for each classic bug

| Classic bug | Root cause | This library |
|---|---|---|
| Dangling pointer after growth | Pointer into a moving container | Handles address slots by **index**; growth relocates nodes, handles keep resolving |
| Use-after-free / stale handle | Recycled slot aliases new data | Every slot carries a **generation counter**; reuse bumps it, old handles fail validation |
| Cross-tree mixup | Pointer from tree A used on tree B | Every handle embeds its **tree id**; mismatches are rejected with an error |
| Out-of-bounds access | No bounds check on raw node pointers | Every resolve **bounds-checks** the index before touching storage |
| Silently ignored failed insert | `bool` return you forgot to check | Every mutation returns a **status variant** carrying a specific `TreeError` |
| Double free / accidental copy | Implicit copy semantics | Copy is **deleted**; moves are explicit and `noexcept` |
| Dangling `Value()` pointer | Pointer invalidated by reallocation | Handles are the always-safe path; `Reserve()` removes reallocation entirely |

All of the above is covered by unit tests (storage contiguity audit,
10,000-node growth audit, move-only reallocation sweeps) running under
AddressSanitizer and UBSan.

## Quick start

```cpp
#include "binary_tree/binary_tree.hpp"
#include <iostream>
#include <variant>

int main()
{
    binary_tree::BinaryTree<int> tree;

    if (auto st = tree.EmplaceRoot(10);
        !std::holds_alternative<std::monostate>(st))
        return 1;

    auto root = tree.Root();
    if (!root)
        return 1;

    tree.EmplaceLeft(root, 5);
    tree.EmplaceRight(root, 15);

    tree.InOrder([](const int &v) { std::cout << v << ' '; });
    // prints: 5 10 15
}
```

## Commands reference

This project uses `make` as a thin wrapper over CMake presets. All
commands assume a POSIX shell (Linux, macOS, WSL).

### Make targets

| Command | What it does |
|---|---|
| `make setup` | Install dependencies (vcpkg), build Debug |
| `make debug` | Configure and build Debug |
| `make release` | Configure and build Release |
| `make test` | Build Debug and run tests (ASan + UBSan) |
| `make benchmark` | Full benchmark suite, for publishing numbers |
| `make benchmark-quick` | Filtered benchmark, for iteration |
| `make docs` | Generate Doxygen HTML docs |
| `make clean` | Remove build artifacts, keep vcpkg_installed |
| `make distclean` | Remove entire `build/` tree |

### First-time setup

```bash
export VCPKG_ROOT="$HOME/.local/share/vcpkg"
make setup
make test
```

`make setup` bootstraps vcpkg if needed, installs `doctest`, `benchmark`,
and `doxygen`, then configures and builds the Debug target. After that,
the other targets are incremental.

### Direct CMake (if you don't want to use make)

```bash
# Debug + tests (ASan + UBSan)
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

# Release
cmake --preset release
cmake --build --preset release

# Benchmarks (-O3 -march=native, benchmark target only)
cmake --preset release-bench
cmake --build --preset release-bench
./build/release-bench/binary_tree_benchmarks

# Docs
cmake --build --preset release --target docs
xdg-open build/release/docs/html/index.html
```

### Notes on the two benchmark targets

`make benchmark-quick` runs 4 benchmarks (the ones quoted in the
performance table), 3 repetitions, 0.05 s each. Use it while developing.

`make benchmark` runs the full suite, 10 repetitions, 0.2 s each. Use it
only when updating the numbers in the `binary_tree::BinaryTree` class
docblock.

The benchmark target uses `-O3 -march=native` in Release only, applied
to the benchmark binary alone — never to the library (which is
`INTERFACE` and has no compile step), and never to the tests (which
must match what users build with).

### Platform note

The `Makefile` targets a POSIX shell. Windows users should use the CMake
commands directly, or run under WSL.

## Architecture

### The three arrays at a glance

```
   ┌──────────────────────────────────────────────────────────────┐
   │  BinaryTree<int>                                             │
   │                                                              │
   │  m_Slots    (contiguous Node<int> storage)                   │
   │   ┌──────┬──────┬──────┬──────┬──────┐                       │
   │   │ root │ left │ right│ empty│ empty│                       │
   │   │  10  │  5   │  15  │      │      │                       │
   │   │ L=1  │ L=—  │ L=—  │      │      │                       │
   │   │ R=2  │ R=—  │ R=—  │      │      │                       │
   │   └──────┴──────┴──────┴──────┴──────┘                       │
   │     [0]    [1]    [2]    [3]    [4]    ← slot index          │
   │                                                              │
   │  m_Generations  (one u64 per slot, used for validation)      │
   │   ┌──────┬──────┬──────┬──────┬──────┐                       │
   │   │  1   │  1   │  1   │  0   │  0   │                       │
   │   └──────┴──────┴──────┴──────┴──────┘                       │
   │                                                              │
   │  m_FreeSlots    (recycled indices, future deletion support)  │
   │   [ 3, 4 ]                                                   │
   │                                                              │
   │  m_Root = 0                                                  │
   │  m_TreeId = 7                                                │
   └──────────────────────────────────────────────────────────────┘

   NodeHandle = { u32 index, u64 generation, u64 tree_id }
   Example:    { 1, 1, 7 }  →  slot 1, generation 1, tree 7
```

### Anatomy of a handle

```
   ┌─────────────────────────────────────────────┐
   │  NodeHandle                                 │
   │  ┌──────────┬──────────────┬─────────────┐  │
   │  │  index   │  generation  │   tree_id   │  │
   │  │  u32     │   u64        │    u64      │  │
   │  │  slot #  │  which life  │   which     │  │
   │  │          │  of the slot │   tree      │  │
   │  └──────────┴──────────────┴─────────────┘  │
   │        4 bytes   8 bytes       8 bytes      │
   │                                    = 20 B   │
   └─────────────────────────────────────────────┘
```

### How each memory bug is removed by design

#### 1. Dangling pointer after growth

A `std::vector<Node<T>*>` would leave every pointer dangling when the
vector reallocates. Here, handles store a **32-bit index**. `m_Slots`
can grow, shrink, or move — the index still resolves to slot N.

```
   BEFORE GROWTH
   ─────────────
   m_Slots.data() ──► [ 0 | 1 | 2 | 3 ]
                        ▲   ▲   ▲   ▲
                        │   │   │   │
                        │   │   │   └──── handle {3, 1, 7}
                        │   │   └──────── handle {2, 1, 7}
                        │   └──────────── handle {1, 1, 7}
                        └──────────────── handle {0, 1, 7}

   AFTER GROWTH (vector reallocated, memory moved)
   ───────────────────────────────────────────────
   m_Slots.data() ──► [ 0 | 1 | 2 | 3 | _ | _ | _ | _ ]
                        ▲   ▲   ▲   ▲
                        │   │   │   │     (new buffer, old freed)
                        │   │   │   └──── same handle {3, 1, 7}
                        │   │   └──────── same handle {2, 1, 7}
                        │   └──────────── same handle {1, 1, 7}
                        └──────────────── same handle {0, 1, 7}

   Handles: UNCHANGED.   Raw pointers: ALL DANGLING.
```

#### 2. Stale handle after slot reuse

When a slot is freed and later reused, its **generation** is bumped. An
old handle carrying generation N fails validation against slot
generation N+1 — detected, not dereferenced.

```
   TIME 1: slot 2 holds value 15, generation = 1
   ─────────────────────────────────────────────
   handle_a = { 2, 1, 7 }   ────► IsValid: index OK, gen 1 == 1 ✓, tree 7 == 7 ✓
                                  Value():   returns pointer to 15     ✓

   TIME 2: slot 2 is freed (ReleaseSlot), generation bumps to 2
   ────────────────────────────────────────────────────────────
   m_Slots[2].reset()       (slot now empty)
   m_Generations[2] = 2     (old handles now invalid)

   TIME 3: slot 2 is reused for value 99
   ─────────────────────────────────────
   m_Slots[2] = 99

   handle_a = { 2, 1, 7 }   ────► IsValid: index OK, gen 1 != 2 ✗
                                  Value():   returns nullptr          ✓
                                  (STALE, rejected — no aliasing)
```

#### 3. Cross-tree mixup

Every handle embeds the tree id of its owner. Validation compares it
against the tree's current id. A handle from tree A is rejected by tree
B with `InvalidParent` (or `nullptr` from `Value()`), never
dereferenced.

```
   Tree A (id = 7)          Tree B (id = 8)
   ────────────────         ────────────────
   handle_A = { 0, 1, 7 }   handle_B = { 0, 1, 8 }

   tree_B.IsValid(handle_A):
       handle non-null?           ✓
       handle.tree_id (7) == m_TreeId (8)?   ✗
       → return false (never touches slot 0 of tree B)
```

#### 4. Out-of-bounds access

`Resolve(index)` bounds-checks `index < m_Slots.size()` before touching
the array. A malicious or corrupted handle cannot read outside the slot
pool.

```
   handle = { 999, 1, 7 }     (m_Slots.size() == 5)

   Resolve(999):
       if (999 >= 5) return nullptr;   ← stops here
       // never reaches m_Slots[999]
```

#### 5. Silently ignored insert failure

`EmplaceRoot`, `EmplaceLeft`, and `EmplaceRight` return
`TreeStatus = std::variant<std::monostate, TreeError>`. The caller must
explicitly branch.

```
   auto status = tree.EmplaceLeft(root, 5);

   if (std::holds_alternative<std::monostate>(status)) {
       // success
   } else {
       switch (std::get<TreeError>(status)) {
       case TreeError::RootAlreadyExists:   ...
       case TreeError::InvalidParent:       ...
       case TreeError::ChildAlreadyExists:  ...
       }
   }
```

#### 6. Double free / accidental copy

Copy constructor and copy assignment are **deleted**. Moves are
`noexcept`, and the moved-from tree is left in a valid empty state with
a **fresh tree id** (so its old handles die). Self-move-assignment is a
safe no-op.

```
   BinaryTree<int> a;
   // BinaryTree<int> b = a;             ← compile error (copy deleted)
   BinaryTree<int> c = std::move(a);     ← OK, a is now empty

   auto old_root = a.Root();             ← nullptr (a was moved from)
   c.IsValid(handle_from_a_before)       ← true (tree id traveled with data)
```

#### 7. Dangling `Value()` pointer

`Value()` returns a `T*` that follows `std::vector` reallocation rules —
any insertion that grows storage invalidates it.

```
   int *p = tree.Value(handle);           ← pointer to live data
   tree.EmplaceRight(other, 42);          ← may reallocate → p is DANGLING
   int x = *p;                            ← undefined behavior

   Fix: use Reserve() up front
   ─────────────────────────────
   tree.Reserve(1000);                    ← no reallocation for 1000 inserts
   int *p = tree.Value(handle);           ← stable for the whole window
```

### Why this is fast

- **Locality.** Nodes are inline in one contiguous array. Traversal
  streams pure node data — no pointer chase to separately allocated
  nodes.
- **No per-node allocation.** One vector growth, not N `new` calls.
- **Generation array stays out of the hot path.** Touched only by
  validation; traversal and `Value()` touch `m_Slots` only.
- **Handle resolution is index arithmetic**, one bounds-check, one
  `optional` check — no indirection through a node table.

## Contracts and requirements

- **Type requirements.** `T` must be move- or copy-insertable. Move-only
  types such as `std::unique_ptr<T>` work. No default-construction
  required.
- **Pointer lifetime.** Pointers from `Value()` die when an insertion
  grows storage; handles never die from insertion. `Reserve(count)`
  freezes growth for known sizes.
- **Threading.** Concurrent const usage (traversals, `Value()`,
  `IsValid()`) is safe. Any mutation needs exclusive external access to
  the tree.
- **Errors.** No exceptions from library logic; failures arrive as
  return values. Only `T` construction and allocation
  (`std::bad_alloc`) can throw — and if they do, no link is written and
  `Size()` does not move.

## Pitfalls

Bad handles never cause undefined behavior here, but they fail calls,
and failed calls do nothing. Watch for these traps:

- **Always check handles before use.** `Root()`, `Left()`, and `Right()`
  return a null handle when the tree is empty, the child slot is empty,
  or the parent handle is stale or foreign.
- **Always check insertion results.** Branch on the status variant.
- **Never keep a `Value()` pointer across insertions.** Re-fetch after
  mutations, or call `Reserve()` up front. Handles are never invalidated
  by insertion — prefer them as the long-lived reference.
- **Never mutate the tree while other threads read it.** There is no
  internal locking.
- **Never persist or send handles.** Tree ids come from an in-process
  counter — meaningful only inside the issuing process.
- **A handle from another tree is rejected, not fatal.** Receiving one
  means a caller bug.
- **`Reserve()` never shrinks.** Pointers fetched before `Reserve()` may
  die if `Reserve()` itself reallocates — re-fetch once after it.
- **Exceptions leave the tree intact.** If `T`'s constructor or
  allocation throws, no link is written and `Size()` does not move.

## Performance

Measured on an Intel Core i3 (Fedora 44, Clang 22, Google Benchmark,
10 repetitions, median):

| Operation | Cost |
|---|---|
| `Value()` (validated deref) | ~4 ns |
| `IsValid()` (full check) | ~4.5 ns |
| Full walk (2 checks per node) | ~5 ns/node |
| Insertion (<=4096 nodes) | ~16 ns/node |
| Insertion (>4096 nodes, L2) | ~30 ns/node |

Insertion throughput drops about 2x above ~4096 nodes on this CPU
(256 KB L2 boundary for `m_Slots + m_Generations`). Call `Reserve()` up
front to hide the realloc cost on top of it. For repeated reads without
intervening insertion, cache `Value()` pointers after `Reserve()` —
re-calling `Value()` per iteration re-validates the handle each time.

Full numbers are in the class docblock (`binary_tree::BinaryTree`,
`@par Performance`). To reproduce:

```bash
make benchmark
```

## Documentation

```bash
make docs
xdg-open build/release/docs/html/index.html
```

The generated API docs live in `build/release/docs/html/`. The
`BinaryTree` class page contains the measured performance table, the
safety model, the requirements, and the usage rules.

## Usage examples

The examples below progress from basic to advanced. Each is self-contained
and compilable.

### Example 1 — Build a binary search tree and search it

```cpp
#include "binary_tree/binary_tree.hpp"
#include <iostream>
#include <variant>

using binary_tree::BinaryTree;
using binary_tree::TreeError;
using NodeHandle = BinaryTree<int>::NodeHandle;

bool Insert(BinaryTree<int> &tree, NodeHandle parent, int value)
{
    // Compare with the parent's value to decide left or right.
    int *pv = tree.Value(parent);
    if (!pv) return false;

    auto &child = (value < *pv) ? parent /*placeholder*/ : parent;

    // We pick the slot based on comparison; only one of these succeeds.
    auto status = (value < *pv)
        ? tree.EmplaceLeft(parent, value)
        : tree.EmplaceRight(parent, value);

    return std::holds_alternative<std::monostate>(status);
}

int main()
{
    BinaryTree<int> tree;
    tree.EmplaceRoot(50);

    auto root = tree.Root();
    Insert(tree, root, 30);
    Insert(tree, root, 70);

    // In-order of a BST is sorted:
    tree.InOrder([](const int &v) { std::cout << v << ' '; });
    // prints: 30 50 70
}
```

### Example 2 — Move-only payloads (`std::unique_ptr`)

```cpp
#include "binary_tree/binary_tree.hpp"
#include <memory>
#include <variant>

int main()
{
    binary_tree::BinaryTree<std::unique_ptr<int>> tree;

    auto status = tree.EmplaceRoot(std::make_unique<int>(42));
    if (!std::holds_alternative<std::monostate>(status))
        return 1;

    auto root = tree.Root();
    if (!root) return 1;

    std::unique_ptr<int> *pv = tree.Value(root);
    if (!pv || !*pv) return 1;

    // **pv is the unique_ptr; ***pv is the int.
    return (**pv == 42) ? 0 : 1;
}
```

### Example 3 — Survive a move of the whole tree

```cpp
#include "binary_tree/binary_tree.hpp"
#include <utility>

int main()
{
    binary_tree::BinaryTree<int> source;
    source.EmplaceRoot(10);
    source.EmplaceLeft(source.Root(), 5);

    auto old_root = source.Root();

    // Move the whole tree.
    binary_tree::BinaryTree<int> dest = std::move(source);

    // The handle still works — but now inside `dest`.
    if (!dest.IsValid(old_root)) return 1;
    if (source.IsValid(old_root)) return 2;   // should be false
    if (!source.Empty()) return 3;
    return 0;
}
```

### Example 4 — Safe iteration after a large build

```cpp
#include "binary_tree/binary_tree.hpp"
#include <iostream>
#include <variant>
#include <vector>

int main()
{
    binary_tree::BinaryTree<int> tree;

    // Reserve so no reallocation happens during insertion.
    // This makes cached Value() pointers safe for the whole build.
    constexpr int N = 1000;
    tree.Reserve(N);

    tree.EmplaceRoot(0);
    auto parent = tree.Root();

    for (int i = 1; i < N; ++i) {
        tree.EmplaceRight(parent, i);
        parent = tree.Right(parent);
    }

    // Cache one pointer — it stays valid because we reserved.
    int *first = tree.Value(tree.Root());
    if (!first) return 1;
    std::cout << "first = " << *first << '\n';   // 0

    // Walk the tree; handles never die from insertion.
    std::size_t count = 0;
    tree.InOrder([&](const int &) { ++count; });
    std::cout << "count = " << count << '\n';    // 1000
}
```

### Example 5 — Handle a failed insertion explicitly

```cpp
#include "binary_tree/binary_tree.hpp"
#include <iostream>
#include <variant>

using binary_tree::TreeError;
using binary_tree::TreeStatus;

void Report(TreeStatus status)
{
    if (std::holds_alternative<std::monostate>(status)) {
        std::cout << "ok\n";
        return;
    }
    switch (std::get<TreeError>(status)) {
    case TreeError::RootAlreadyExists:
        std::cout << "root already exists\n";       break;
    case TreeError::InvalidParent:
        std::cout << "parent handle is stale/foreign\n"; break;
    case TreeError::ChildAlreadyExists:
        std::cout << "child slot is already occupied\n"; break;
    }
}

int main()
{
    binary_tree::BinaryTree<int> tree;
    Report(tree.EmplaceRoot(10));     // ok

    binary_tree::BinaryTree<int>::NodeHandle empty;
    Report(tree.EmplaceLeft(empty, 5)); // parent handle is stale/foreign

    Report(tree.EmplaceRoot(20));     // root already exists
}
```

### Example 6 — Reject a stale handle after deletion (conceptual)

`ReleaseSlot` is internal today (prepared for future node deletion), but
the generation mechanism it uses is what you'll observe once deletion is
exposed. When it is:

```cpp
auto h = tree.Root();
tree.ReleaseSlot(h.index);          // bumps generation
// tree.IsValid(h) == false          // old handle is rejected, not dereferenced
```

### Example 7 — Cross-tree safety

```cpp
#include "binary_tree/binary_tree.hpp"

int main()
{
    binary_tree::BinaryTree<int> a;
    binary_tree::BinaryTree<int> b;

    a.EmplaceRoot(10);
    b.EmplaceRoot(20);

    auto ha = a.Root();
    auto hb = b.Root();

    // Handles are tree-scoped — a handle from `a` is rejected by `b`.
    if (b.IsValid(ha)) return 1;         // should be false
    if (b.Value(ha) != nullptr) return 2; // should be nullptr

    // Using it is an error, never undefined behavior.
    auto status = b.EmplaceLeft(ha, 5);
    return std::holds_alternative<binary_tree::TreeError>(status) ? 0 : 3;
}
```
