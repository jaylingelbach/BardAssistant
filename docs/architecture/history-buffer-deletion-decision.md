# Why History Buffer Deletion Reconstructs the Circular Buffer

## Decision

When an insult is deleted, `removeFromHistory()` will **reconstruct the history circular buffer** rather than shifting entries in place around the circular buffer.

The history storage remains a fixed-size array:

```cpp
RTC_DATA_ATTR uint32_t history[HISTORY_CAP] = {0};
```

We are **not** changing history to a `std::vector`.

## Why

The history buffer is a circular buffer because its normal job is to maintain a small, bounded navigation history across deep sleep/wake.

Normal history operations are simple and efficient:

- append a new insult ID
- discard the oldest entry when the buffer is full
- navigate Prev/Next
- persist the buffer in RTC memory

Deletion is different. Deleting an arbitrary ID from the middle of a circular buffer requires reasoning about both:

- **logical position** — where an entry appears in the user's history
- **physical position** — where the entry happens to live in `history[]`

Because the buffer can wrap around the end of the array, an in-place deletion would require carefully handling:

- physical index wrapping
- `historyHead`
- `historySize`
- `historyPosition`
- shifting entries across the physical end of the array
- preserving logical order

That complexity creates more opportunities for subtle off-by-one and state-consistency bugs.

## The Alternative: In-Place Shifting

A normal array deletion looks like:

```text
[A][B][C][D][E]

Delete C

[A][B][D][E][E]
```

The elements after the deleted entry are shifted left and the logical size is reduced.

That algorithm works naturally when physical order and logical order are the same.

Our history buffer does not guarantee that.

For example, the physical array might contain:

```text
[F][B][C][D][E]
```

while the logical history is:

```text
B → C → D → E → F
```

An in-place deletion therefore has to account for the circular wrap.

## The Chosen Approach: Reconstruct

Instead, `removeFromHistory()` will work primarily in **logical history order**.

Conceptually:

```text
Current logical history:

A → B → C → D → E

Delete C

Remaining logical history:

A → B → D → E
```

The function can then rebuild the fixed circular-buffer representation from the remaining IDs.

The rest of the application does not need to know how the circular buffer is physically arranged.

## Why This Is Acceptable

History is intentionally bounded by `HISTORY_CAP`.

Even if the history capacity were 50 or 100 entries, reconstructing it means copying at most that many `uint32_t` IDs.

For example:

```text
100 IDs × 4 bytes = 400 bytes
```

Deletion is also an infrequent operation compared with normal operations such as Random, Next, and Prev.

Therefore, we prefer:

1. Correctness
2. Clear reasoning
3. Maintainability
4. Performance

For this use case, reconstruction is a better tradeoff than complicated in-place circular-buffer manipulation.

## Why We Did Not Change History to a Vector

The fact that arbitrary deletion is easier with a vector does not mean a vector is the better underlying structure.

The circular buffer still provides useful properties for the normal history workload:

- fixed memory usage
- bounded history
- efficient appending
- automatic expiration of old history
- predictable embedded-memory behavior
- straightforward RTC persistence

The awkwardness comes from supporting **one less-common operation (arbitrary deletion)**, not from the circular buffer being a poor fit for history itself.

So the decision is:

```text
Keep:
    fixed RTC_DATA_ATTR array
    circular-buffer behavior

Add:
    history-specific removal operation

Use:
    reconstruction when removing an arbitrary ID
```

## API Boundary

The rest of the application should not manipulate the circular-buffer internals directly.

Instead, history should expose operations such as:

```cpp
appendToHistory(uint32_t id);
historyGetAtLogical(size_t logicalPos, uint32_t &outId);
removeFromHistory(uint32_t id);
```

`deleteInsult()` should be able to say:

```text
remove this ID from history
```

without needing to know about:

- `historyHead`
- physical array indexes
- wrapping
- how the circular buffer is reconstructed

This keeps the circular-buffer implementation encapsulated inside the history logic.

## Future Reconsideration

This decision can be revisited if the requirements change.

Consider a different data structure if:

- `HISTORY_CAP` becomes very large
- arbitrary history mutations become frequent
- history needs operations that are consistently awkward with a circular buffer
- the device needs substantially more complex history querying/manipulation

For the current Bard's Assistant requirements, none of those justify changing the underlying structure.

## Short Version

**We reconstruct the history buffer on deletion because deletion is rare, history is small and bounded, and reconstruction keeps the circular-buffer logic understandable and reliable.**

We are deliberately optimizing for **correctness and maintainability over micro-optimization**.

The circular buffer is still the right structure for normal history behavior; reconstruction is simply the safest way to handle an unusual mutation of that structure.
