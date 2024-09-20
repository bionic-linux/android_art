Trading off Garbage Collector Speed vs Memory Use
-------------------------------------------------

Garbage collection inherently involves a space vs. time trade-off: The less frequently we collect,
the more memory we will use. This is true both for average heap memory use, and also maximum heap
memory use, measured just before GC completion. (We're assuming that the application's behavior
doesn't change depending on GC frequency, which isn't always true, but ideally should be.)

Android provides primarily one knob to control this trade-off: the `HeapTargetUtilization` option.
This is a fraction between 0 and 1, normally between 0.1 and 0.8 or so. The collector aims to
ensure that just before a collection completes and memory is reclaimed, the ratio of live data in
the heap to the allocated size of the heap is HeapTargetUtilization.  With a non-concurrent GC, we
would trigger a GC when the heap size reaches the estimated amount of live data in the heap
divided by `HeapTargetUtilization`. In the concurrent GC case, this is a bit more complicated, but
the basic idea remains the same.

This value is implicitly adjusted to delay collections beyond this point for applications
essential to the responsiveness of the device, such as the current foreground application. Such
applications thus get to use more memory in order to reduce GC time.

Assume that the GC cost is proportional to the amount of live data in the heap. If we collect
garbage whenever a fixed fraction of the allowable heap size is in use, then GC cost per byte
allocated remains fixed, independent of the app's memory use.  If the live data size increases by
a factor of, say, 2, the cost of each GC increases by a factor of 2 as well, but we collect half
as often.  I.e. we get to allocate twice as much memory for the cost of a GC which became twice as
expensive. `HeapTargetUtilization` thus effectively sets the per-allocated-byte GC cost, at the
expense of memory use.

(It's not clear whether we should try to keep per-byte GC cost constant on a system running
many ART instances, and people have made reasonable arguments to the contrary. But it's the
traditional property maintained by garbage collectors, and a reasonably defensible one.)

In fact GCs have some constant cost independent of heap size. With very small heaps, the above
rule would constantly trigger GCs, and this constant cost would dominate. With a zero-sized heap,
we would GC on every allocation. This would be slow, so a minimimum number of allocations between
GCs, a.k.a. `HeapMinFree`, makes sense. Ideally it should be computed based on the
heap-size-invariant GC cost, which is basically the cost of scanning GC roots, including the cost
of "suspending" threads to capture references on thread stacks. Currently we approximate that with
a constant, which is suboptimal. It would be better to have the GC compute this based on actual
data, like the number of threads, and the current size of the remaining root set.

`HeapMinFree` should really reflect the caracteristics of the Java implementation, given data
about the application known to ART. It is currently configurable. But we would like to move away
from that, and have ART use more sophisticated heuristics in setting it.

There used to be some argument that once application heaps got too large, we may want to limit
their heap growth even at the expense of additional GC cost. That could be used to justify
`HeapMaxFree`, which limits the amount of allocation between collections in applications with a
very large heap.  Given that in most cases device memory has grown faster than our support for
huge Java heaps, it is unclear this still has a purpose at all. We recommend it be set to
something like the maximum heap size, so that it no longer has an effect. We may remove it, or at
least make it ineffective, in the future. However, we still need to confirm that this is
acceptable for very low memory devices.
