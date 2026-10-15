# Actual SI PMU voltage-register and GPIO control

`driver/si_pmu_gpio.c` implements the current HND PMU voltage-register and GPIO
helpers reached by target PHY51 initialization. It composes the existing real
SI selector only where the actual register helper selects a core. It has no MMIO
backend, OS mapping/IRQ implementation, hardware operation or readiness result.
Exact selected static Evidence IDs are in
[SI_PMU_GPIO_EVIDENCE.json](../findings/SI_PMU_GPIO_EVIDENCE.json).

## PMU voltage-register path

The actual `si_pmu_vreg_control` tests **signed** ChipCommon revision greater
than34 and extended capability bit40. Only when both hold does it independently
find PMU core827/unit0 for each of two register calls. Otherwise each call uses
core index0. A missing PMU produces find-index33; the AI helper's index-above31
branch returns0 without I/O. The model bounds inspected catalogs to32 entries;
missing reached catalogs are dependencies, and reached bounds violations fail.

The address call uses offset658, maskffffffff and the supplied register index.
The data call uses offset65c and the supplied mask/value. Each actual modifying
AI register call performs R32/SY, ST/W32((old&~mask)|value), R32/SY, returning
that final independently observed sample. Values are not narrowed to the mask.
With mask|value equal0, it performs only the final R32/SY.

`si_pmu_corereg` has a separate busy-poll condition for other offsets. Neither658
nor65c matches that condition, so this voltage-register path adds no poll,
delay, constant return or successful callout. The target PHY caller's register0,
mask01f00000 and data derived from its actual PHY sample use this same real path.

## GPIO reservation and register path

The actual GPIO wrapper always accesses core index0, register6c. On bus0,
priority other than2 and nonzero mask|value reaches the real global GPIO
reservation word. Priority0 filters mask with its complement; other priorities
filter mask with the reservation itself. Value is then intersected with the
filtered mask. Priority2, nonzero buses and an original zero mask|value skip
that reservation access. Unknown reservation state is therefore a dependency
only at its reached guard; the model never assumes a global reservation of0.

The resulting mask/value run actual `si_corereg` interconnect dispatch. SB is an
explicit unresolved dependency; AI interconnects1/3/4 execute the recovered
body, and other interconnects return0. A filtered read-only request still
performs its actual R32/SY. The target GPIO maskffff/value0/priority0 requires
actual reservation metadata rather than claiming all pins were unreserved.

## Mapping, selection and IRQ state

The bus0 AI register path directly reads the requested core's cached mapping.
If absent, it requests actual OS MAP(physical,1000,0), committing even a
successful zero result to that cache. This path does **not** select the core,
map its wrappers, change the current SI index or invoke IRQ helpers. A zero
mapping stops before a reached MMIO dereference, retaining earlier effects.
This distinction matters when PHY initialization accesses PMU/ChipCommon while
D11 remains selected.

Bus1 ChipCommon fast windows use current-map+3000 for the observed newer bus
cores. The bus core's own fast window uses +2000; the older selected-bus-core
window follows the actual +1800/+1000 offset test. Other bus1 requests and
other bus types use the real current-core IRQ guard and SI selector. The
restoration index is sampled **after** any actual disable callback. Selection
restoration runs only when that saved index differs from the requested index.
The final restore-callback guard is independent and may run with token0 when
no disable callback ran. Read/write results never imply ownership or readiness.

IRQ operations carry actual callback/context identities as ARM32 labels and an
immutable current-state view. The backend must supply the actual post-call SI
state, even when unchanged. Missing state leaves freshness0 and stops as a
dependency; errors retain freshness0. Successful cancelling callbacks retain
reported state/token, while callback error wins simultaneous cancellation.
Resource catalogs, mapping/window lifetimes, callback bindings and serialization
remain caller-owned requirements. No native function pointer is invoked by the
portable model.

Begin snapshots its input before clearing the job, including own-context aliases.
Each step invokes at most one synchronous user callback. Child partial SI state
propagates before callbacks and after child steps. Terminal states are silent:
no automatic restore, IRQ cleanup, retry, unmap or rollback follows an error,
dependency or cancellation.

## Independent validation

`tests/test_si_pmu_gpio.c` supplies literal independent map/PCI/register/IRQ
traces and read replies. It covers target PMU and ChipCommon selection,
read-only and modifying operations, unmasked values, missing PMU, all GPIO
reservation priority guards, cache commits on cancellation and zero-map
failure, PCI fast/ordinary paths, explicit IRQ freshness, changed post-disable
index, independent restore with token0, aliases and interconnect gates.
Six private semantic mutants covering revision/filter/value/selection/restoration
contracts are rejected by the unchanged independent oracle. Every callback in
the target PMU and ordinary IRQ paths is exercised with
failure, cancellation and both. Steps are checked for one callback maximum and
silent terminals. ASan/UBSan host tests, combined-header compilation and
freestanding ARMv7 compilation pass. These do not execute stock instructions,
perform hardware voltage/GPIO changes or prove physical PHY initialization.
