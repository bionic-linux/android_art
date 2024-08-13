Thread Priorities in Android
----------------------------

[ Some of the following is currently more aspirational than fact. Implementation is in progress. ]

ART initially assumed that Java threads always used `SCHED_OTHER` Linux thread scheduling. Under
this discipline all threads get a chance to run, even when cores are overcommitted. However the
share of cpu time allocated to a particular thread varies greatly, depending on its "niceness",
as set by `setpriority()` (or the swiss-army-knife `sched_setattr()`). Java priorities are mapped,
via the platform supplied `libartpalette` to corresponding niceness values.

The above is still generally true, but there are two important exceptions:

1. ART occasionally uses niceness values obtained by interpolating between Java priorities.
   These niceness values do not map perfectly back onto Java priorities. Thread.getPriority()
   rounds them to the nearest Java priority.

2. Frameworks application management code may temporarily promote certain threads to `SCHED_FIFO`
   scheduling with an associated strict priority. Such threads cannot be preempted by
   `SCHED_OTHER` threads. There are currently no clear rules about which threads may run under
   `SCHED_FIFO` scheduling. In order to prevent deadlocks, all ART, libcore, and frameworks code
   should be written so that it can safely run under `SCHED_FIFO`. This means that wait loops that
   either spin consuming CPU time, or attempt to release it by calling `sched_yield()` or
   `Thread.yield()` are *incorrect*. At a minimum they should eventually sleep for short periods.
   Preferably, they should call an OS waiting primitive, as mutex acquisition, or Java's
   `Object.wait()` does.

How to set priorities
---------------------

Java code is arguably allowed to expect that Thread.getPriority() returns the last value passed to
Thread.getPriority, at least unless the application itself calls Android-specif code or native
code documented to alter thread priorities. This argues that Thread.getPriority() should cache the
current priority. Historically, this has also been done for performance reasons. We continue to do
so.

In order to keep this cache accurate, applications should try to set priorities via
`Thread.setPriority`. `android.os.Process.setThreadPriority(int)` may be used to set any niceness
value, and interacts correctly with the `Thread.getPriority()`.

All other mechanisms, including `android.os.Process.setThreadPriority(int, int)` (with a thread id
parameter) bypass this cache. This has two effects:

1. The change of priority is invisible to `Thread.getPriority()`.

2. If the effect is to set a positive (> 0, i.e. low priority) niceness value, that effect may be
   temporary, since ART, or the Java appliction may undo the effect.

Avoiding races in setting priorities
------------------------------------

Priorities may be set either from within the process itself, or by external frameworks components.
If multiple threads or external components try to set the priority of a thread at the same time,
this is prone to races. For example:

Consider a thread A that temporarily wants to raise its own priority from *x* to *y* and then
restore it to the initial priority *x*. If another thread happens to also raise its priority to
*y* while it is already temporarily raised to *y*, resulting in no immediate change, A will have
no way to tell that it should not lower the priority back to *x*. The update by the second thread
can thus be lost.

In order to minimize such issues, all components should avoid racing priority updates and play by
the following rules:

1. The application is entitled to adjust its own thread priorities. Everything else should
   interfere with that as little as possible. The application should adjust priorities using
   either `Thread.setPriority(int)` or `android.os.Process.setThreadPriority(int)`. JNI code that
   updates the thread priority only for the duration of the call, may be able to adjust its
   own priorities using OS calls, using something like the ART protocol outlined below,
   so long as the JNI code does not call back into Java code sensitive to thread priorities.

2. ART may temporarily change the priority of an application thread with positive niceness to a
   lower non-negative niceness value (usually zero), and then restore it it to the cached Java
   priority.  This happens only from within that thread itself.  This is necessary, since threads
   with large positive niceness may become esentially unresponsive under heavy load, and may thus
   block essential runtime operations.

3. Priority updates using means other than `android.os.Process.setThreadPriority(int)` or
   `Thread.setPriority(int)` should set a positive niceness value only when it is OK for ART (or
   the application) to overwrite that with the cached Java priority at any point. ART will not
   overwite negative niceness values, unless the negative value is installed exactly between ART's
   priority check and priority update. (There is no atomic priority update in Linux.) Hence,
   ideally:

4. In order to minimize interference with applications directly using OS primitive to
   adjust priorities, external processes should avoid updating thread priorities of threads
   that may currently be actively running. Ignoring this advice may result in lost priority
   updates, but should not otherwise break anything. That is good, because it is unclear
   that such timing constraints are always fully enforceable.

ART may use the following to temporarily increase priority by lowering niceness to zero:

```
  current_niceness = getpriority(<me>);
  if (current_niceness <= 0 || current_niceness != <cached niceness>)
    <do not change priority>;
  setpriority(<me>, 0);
  <do high priority stuff>
  if (<we changed priority> && getpriority(<me>) == 0) {
    // Either there were no external intervening priority changes, or an external process
    // restored our 0 niceness in the interim. Thread.setPriority(0) may have been called
    // in the interim. In any of those cases, it is safe to restore priority with:
    setpriority(<me>, <cached Java priority>);
    // setpriority(<me>, current_niceness) is incorrect with an intervening
    // Thread.setPriority(0).
  }
```

The above requires sufficient concurrency control to ensure that no ```Thread.setPriority```
calls overlap with the priority restoration at the end.

Java priority to niceness mapping
---------------------------------

The priority mapping is mostly determined by libartpalette, which is a platform component
outside ART. Traditionally that provided `PaletteGetSchedPriority' and `PaletteSetSchedPriority`,
which returned and took Java priority arguments, thus attempting to entirely hide the mapping.
This turned out to be impractical, because some ART components, e.g. ART's system daemons,
were tuned to use niceness values that did not correspond to Java priorities, and thus used
Posix/Linux `setpriority` calls directly. This scheme also prevents us from easily caching
priorities set by `android.os.Process.setThreadPriority(int)`, which traffics in Linux niceness
values.

Thus libartpalette now also provides `PaletteGetPriorityMapping()`, which is used in connection
with Linux `setpriority(), and is preferred when available.
