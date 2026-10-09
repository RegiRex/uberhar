# Scoped ARM64 vertex-output independence

<!-- CodexAstraLocal: Document the consumer proof behind the 0.1.36 candidate,
including what future backend or observer changes must revalidate. -->

The selected-output contract lets Native and Combo distribute more CPU vertex
work without changing selected vertex bytes or primitive order. It is a separate
contract from the original full arithmetic-read analysis. Both use one bounded,
acyclic control-flow graph; the original remains the default API and the route
for callers that cannot establish the narrower consumer boundary.

A local no-GS `ShaderUnit` lives only inside `PicaCore::LoadVertices`. Its output
conversion copies selected register values into `OutputVertex`; FIFO storage,
primitive tails and renderer submission consume those copied values. Temporary,
address and condition storage does not escape that draw. EMIT/SETEMIT, geometry
shaders, debug/timing observers and unknown instruction effects remain excluded.
The new proof requires every lane of every selected output register to be
independent of carried state, even if the current output mapping uses fewer lanes.

<!-- CodexAstraLocal: State the induction across arbitrary prior invocations;
finite synthetic examples alone cannot authorize a general shader. -->

The analyzer first finds every possibly written mutable lane in the reachable
control-flow graph. Those lanes start tainted, representing arbitrary values left
by earlier invocations. Never-written lanes retain the common initial draw value.
At joins, selected-output analysis unions predecessor taints. Arithmetic propagates
true component dependencies, reading every source before any aliased destination
write. Reductions and scalar operations use their actual ARM64 result components.
Any tainted branch condition or relative address refuses admission. Every END must
have clean selected outputs.

This proves the next invocation's selected values are independent of any preceding
mutable state. Reapplying that statement covers any length of serial history and
any grain boundary. Original arithmetic still executes; no numerical approximation,
live output substitution or draw omission follows from this certificate. Unused
final temporary bytes and incidental host status can differ, and the tests report
those differences explicitly. They are not claimed to match standalone shader state.

<!-- CodexAstraLocal: Host FP status is distinct from guest CPU status and FP
controls. The actual constructed backends, not settings, identify the proof scope. -->

`ARM_Dynarmic` opts into host-status isolation only on ARM64. Before returning host
callbacks, its A32 backend spills active guest FPSR. Callback-only memory uses the
call-preparation path; page-table/fastmem emitters spill before direct access or
wrapped fallback. Later guest FP clears the inactive collector before collecting
new flags. VMRS and context save/load consume stored guest state. Exception
callbacks also use call preparation. The current interpreter fallback is fatal;
it is not a successful bridge to DynCom.

This does not assert generic exception-unwind safety or host-status equivalence.
The separate DynCom backend and other host backends default to false. The inspected
ARM64 shader emitter has no external call in the admitted instruction domain and
no status-dependent result; its EX2/LG2 helpers are emitted arithmetic. A future
FP-status observer, external helper, CPU backend or emitter requires renewed proof.
FPCR controls remain preserved, and enabled traps still require serial execution.

`System::IsHostFpStatusIsolated` queries the constructed live core safely.
`ShaderEngine::SupportsObservableVertexContract` independently identifies the
constructed ARM64 JIT. Both must opt in after the existing no-GS, observer, input,
trap and concurrent-memory guards. A settings change cannot authorize a different
engine. Missing capabilities retain the original proof. Standalone shader Run,
geometry state and guest save formats are unchanged.

<!-- CodexAstraLocal: Identify cache identity, ordered consumers and evidence
that must remain coupled when maintaining this route. -->

The cache key includes contract, entry, Boolean uniforms and output mask alongside
exact source snapshots and source revision binding. A same-revision contract switch
must select a separate cached proof. Bounded FIFO planning, worker lifetime, copied
FP controls, immutable input references and original owner assembly remain the
existing batch implementation. Small actual-miss populations remain owner-only.

The actual A64 guest-status gate executes synthetic SVC and memory callbacks with
seeded status, all rounding/FZ/DN combinations, immediate VMRS, fresh FP and block
exit/reentry. Three altered backend implementations must fail. Narrow memory loads
in that fixture explicitly produce clean byte/half return carriers; the inherited
JIT's separate narrow-return ABI assumption is not qualified by this status test.

The actual A64 observable gate uses synthetic shaders, independent admission
expectations, rotated raw special values and different serial/grain histories.
It compares selected bytes and composes real input loading, FIFO boundaries,
output conversion and persistent primitive assembly. Nine broken certificate rules
and a wrong-order implementation must produce an actual output mismatch. Existing
full-arithmetic, cache-invalidation and runtime-ownership gates remain required.
These finite host/QEMU proofs do not establish Thor speed, universal shader support
or unobserved game graphics; the release still needs its device qualification.
