# 02 — Thermal-hydraulics, primary loop, coupling and data library

Date: 2026-11-16. Baseline: 72b6313cbf06a383532062510e93bd844124d940
Scope: `Planning/THEORY_MANUAL.md` §14–§19, Appendix D, and the rows of §20–§21 on this slice, against Demazière §5.3–§5.5, Kolev, the BEAVRS specification and `external/BEAVRS_minimal`. No engine code exists. Line numbers refer to the manual.

## Conclusion

The conservation equations (15.1)–(15.6) and their discrete forms are sound. The defects are in the transient: a steady-state centreline formula feeds the Doppler temperature (T-1), the loop–core ring is never closed inside a coupled pass (T-2), heat is handed over as point values so energy is not conserved (T-3), and xenon lags under D3 (T-4). §19.3 misses unphysical library entries (T-5).

## Findings

### T-1 The centreline formula (14.9) adds a spurious power-proportional term to the Doppler temperature in a transient
1. **Component:** $T_{cl}=T_1+q'''_1s_1/(4k_1)$ (line 1686), used in (14.10).
2. **Claim vs computation:** claimed centreline temperature; computed is the steady parabolic offset for the instantaneous source, true only when ring 1 is in balance.
3. **Counterexample (Analytic, confirmed by a throwaway implicit solve of (14.4)):** uniform rod, power stepped to 5 × 18.3 kW m⁻¹, $k=4$ W m⁻¹ K⁻¹. At $t=0^+$ the true $T_{cl}-T_1$ is zero. (14.9) gives $q'/(8\pi kN_f)$ = 91 K for $N_f=10$ and 182 K for $N_f=5$; the true rise after 10 ms is 6.1 K. With $\omega_D=0.3$ (Appendix D) $T_D$ jumps 27 K instantly and depends on the ring count.
4. **Root cause:** 1.
5. **Inherited by:** Doppler reactivity in rod-ejection transients; NEACRP comparisons. §19.1 item 5 tested steady state only.
6. **Fixes:** (a) $T_{cl}=T_1+K_{3/2}(T_1-T_2)\,s_1/(4k_1A_1)$, the net heat leaving ring 1: identical at steady state, zero for a flat profile; physical, free. (b) A node at $r=0$: changes the mesh. Recommend (a).
7. **Severity:** wrong physics.

### T-2 The loop–core ring is not closed inside step 5.1
1. **Component:** §17.8, "solved in flow order starting from the core outlet" (line 2271); §18.3, "a function of the power estimate, not an accumulation over the passes" (line 2362).
2. **Claim vs computation:** the sweep needs the core outlet at $t_n$, which exists only after the channels have the inlet at $t_n$. The outlet is therefore taken from pass $p-1$ (an accumulation) or from $t_{n-1}$ (the lag D1 claims to remove, losing $W(h_\text{out}^n-h_\text{out}^{n-1})\Delta t$ at the upper plenum). $T_\text{sec}$ in (17.7) depends on every $T_c^n$ and cannot be marched either.
3. **Counterexample (Analytic):** fixed power, 20 volumes of 0.75 s transit (generic 15 s loop). The pass-lagged sweep contracts by $\prod_j(1+\tau_j/\Delta t)^{-1}$: $10^{-5}$ at 1 s, 0.78 at 60 s, 0.996 at 3600 s (3300 passes for $10^{-6}$). The change-based tests of step 5.5 then stop with an error 240 times the tolerance.
4. **Root cause:** 2, 6.
5. **Inherited by:** every loop transient with a step near or above the loop transit time; the D1 rationale.
6. **Fixes:** (a) close the ring inside 5.1: with flows frozen the outlet enthalpy and boron are affine in the inlet values, so two sweeps solve the cyclic system exactly, with $T_\text{sec}$ as a bordered unknown; physical. (b) Inner Picard with a residual test: simple, slow at long steps. Recommend (a).
7. **Severity:** wrong physics.

### T-3 Heat reaches the implicit-Euler thermal-hydraulics as an end-of-step value; energy is not conserved
1. **Component:** step 5.4 (line 2355); $Q_k^n$ in (15.11); the θ option of §14.5; (12.3).
2. **Claim vs computation:** (12.1) integrates to $\int q_T\,dt=\int q_f\,dt-\sum_i(D_i^n-D_i^{n-1})$. Rods and coolant receive $\Delta t\,q_T^n$. A rod with $\theta<1$ releases $\Delta t[\theta q_w'^{\,n}+(1-\theta)q_w'^{\,n-1}]$ while the channel takes $\Delta t\,q_w'^{\,n}$.
3. **Counterexample (Analytic):** scram, $q_f=0$, one group, $x=\zeta\Delta t$. Delivered over stored energy is $xe^{-x}/(1-e^{-x})$: 0.951 at $x=0.1$, 0.771 at 0.5, 0.313 at 2. The §19.2 check "energy released after shutdown equals the stored $\sum_iD_i$" (line 2435) fails at any finite step.
4. **Root cause:** 4, 2.
5. **Inherited by:** decay-heat removal, stored fuel energy, the §17.9 energy check (which also omits $V\,\Delta p_\text{sys}$).
6. **Fixes:** (a) pass the step-mean power $[\int q_f\,dt-\sum_i\Delta D_i]/\Delta t$ and the θ-weighted wall heat; implicit Euler stays stable and the balance closes; physical. (b) Document the first-order loss. Recommend (a).
7. **Severity:** wrong physics.

### T-4 Under D3 the cross sections at $t_n$ use xenon from $t_{n-1}$
1. **Component:** step 2, "when (10.4) is used" (line 2346); step 6, "with D3 the xenon and samarium densities" (line 2360).
2. **Claim vs computation:** D3 is justified by steps of hours and VISION §2.3 asks for converged feedback; xenon is outside the coupled iteration and lags one step.
3. **Counterexample (Analytic, from (10.1) and (10.3)):** halving the power from equilibrium gives $dN_\text{Xe}/dt\approx(a_\text{Xe}/2)N_\text{Xe}$. With the manual's "$2/(\lambda_\text{Xe}+a_\text{Xe})$, a few hours" taken as 4 h, that is about 21 % per hour: a one-hour step uses xenon absorption stale by a fifth.
4. **Root cause:** 2.
5. **Inherited by:** xenon and load-following transients; the D3 rationale.
6. **Fix:** evaluate (10.5)–(10.8) inside the iteration with the current flux and commit at step 6; closed form, negligible cost; physical.
7. **Severity:** wrong physics.

### T-5 §19.3 misdescribes the library: unphysical corner entries, and a noise figure dominated by non-fuel nodes
1. **Component:** "hold baffle and water data"; the noise figures (line 2468).
2. **Claim vs computation:** claimed usable reflector-like data and one noise level; the corners hold empty tallies and the noise figure mixes node kinds.
3. **Counterexample (Data-derived, h5py over all 72 states):** $D_1$ equals 1.797×10³⁰⁸ or is negative (minimum −169) in 46 entries and $D_2$ in 79; $\Sigma_{a1}=0$ in 41, $\Sigma_{a2}=0$ in 77, $\Sigma_{12}=0$ in 220 (56 states). All lie at the outer corner positions; none in the 3281 fuel nodes. Mirror differences ($x\leftrightarrow y$, state [900 K, 585 K, rods out]) over fuel positions only: $\nu\Sigma_{f2}$ 2.8 %, $\Sigma_{a2}$ 2.3 %, $D_1$ 3.7 %, $D_2$ 3.2 %, $\Sigma_{12}$ 6.3 %, $\Sigma_{a1}$ 5.1 %. The manual's 5.2, 5.6, 4.1, 11 and 11 % appear only with the 32 non-fuel positions included.
4. **Root cause:** 4 (empty tallies), 3.
5. **Inherited by:** any run that "exercises the code path".
6. **Fix:** add a library row for empty-tally nodes and reject non-finite or non-positive $D$ on reading; quote fuel-node noise separately.
7. **Severity:** misleading.

### T-6 §16.4: the default drift flux is negative at PWR conditions, and the "not in Resources" flags are misplaced
1. **Component:** (16.5) (line 2073); the flag on (16.4); Appendix D (line 2640).
2. **Claim vs computation:** claimed a default closure for a PWR and two items absent from `Resources/`; neither holds.
3. **Counterexample (Analytic from (16.5)):** at 15.5 MPa the constant is 0.246 m s⁻¹ and the $G$ coefficient −1.54×10⁻⁴, negative above 13.3 MPa. $V_{gj}=-0.25$ m s⁻¹ at $G\approx3200$ kg m⁻² s⁻¹ (BEAVRS Table 1 flow, 5 % bypass, 193 × 261 cm²); the sign changes near 1600. No validity range is stated, though §14.7 demands one. **Reference-cited:** (16.4) is Kolev Eq. (3.64), given for $x_e<0$; Saha–Zuber is Kolev (3.56)–(3.57); both are in `Resources/`. Kolev (3.73) uses the heated diameter; (16.1) uses $D_h$ silently.
4. **Root cause:** 1, 8.
5. **Inherited by:** stage-5 void fraction; Appendix D.
6. **Fix:** take the range from Maier and Coddington (not in Resources), or default to Dem Eq. (5.192) with $V_{gj}\ge0$; correct the flags.
7. **Severity:** unsupported.

### T-7 The pump relation (17.6) does not cover the end of a coastdown
1. **Component:** "defined for forward flow and forward rotation. A stopped pump is a loss coefficient" (line 2196).
2. **Claim vs computation:** claimed to serve the pump-speed knob; the table covers only the start of a coastdown.
3. **Counterexample (Analytic):** in a coastdown $N\to0$ at finite natural-circulation flow, so $\nu\to\infty$. Continuity with the stopped-pump loss requires $f_H\propto-\nu^2$, which a table over finite ν cannot hold. The hand-over is undefined, so $\Delta p_\text{pump}$ jumps in (17.5); likewise the torque.
4. **Root cause:** 4.
5. **Inherited by:** the pump-coastdown transient of VISION §3; O3's recommended option.
6. **Fix:** a second homologous branch in $h/\nu^2$ against $1/\nu$ whose limit is the locked-rotor coefficient (standard practice, not in Resources; verify).
7. **Severity:** unsupported.

## Sound components

- (15.1)–(15.6) agree with Dem Eqs. (5.162)–(5.175): $Gh^+$ and $G^2/\rho^+$ equal the mixture-plus-drift terms; pressure and cross-flow signs are right. Dropping $J_m\cdot\nabla p$ with the dissipation is consistent only if $Q_\text{pump}$ is the shaft power; say so.
- (15.8)–(15.12), (15.14): mass and energy telescope exactly, direct heating included. The §15.6 bias is right in size (1.0 K average, 17 nodes); (15.13) needs the donor-side neighbour on reversal.
- (14.3)–(14.8) and the $\omega_D=\tfrac12$ statement; Fink, Kelly and the misprint argument reproduce (58.5, 16.5, 19.0 W m⁻¹ K⁻¹ at 600 K).
- (16.1)–(16.3) transcribe Kolev correctly.
- (17.1)–(17.5): ring closure, gravity signs, no double-counted inertia; the coastdown solution is exact. (17.7) sums to $Q_\text{SG}$, but a load step below about half (Analytic, illustrative end differences 48 and 15 K) leaves outlet cells colder than $T_\text{sec}$, which then heats them.
- §19.3 mesh, states, statistics, units, 32 corner positions. O1's direction is sound; unstated costs are surface-flux tallies for discontinuity factors and the missing leakage spectrum [INFERENCE].
