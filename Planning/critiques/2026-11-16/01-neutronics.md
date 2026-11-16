# 01 — Neutronics (THEORY_MANUAL §3–§13, Appendices A–C)

Date: 2026-11-16 Baseline: 72b6313cbf06a383532062510e93bd844124d940

Scope: `Planning/THEORY_MANUAL.md` §3–§13, App. A–C against the PARCS theory manual and Marguet; throwaway numpy checks only; no engine, no OpenMC, no library read.

## Conclusion

Strongest: the adjoint (9.1) and the inner product of (9.2)–(9.4) belong to different matrices (N-1), and the rod weights have no rule between nodal updates (N-2). The kernels, the kinetics discretization, Xe/Sm, decay heat and the pin-power basis re-derive cleanly; Appendix C items 2 and 3 are real, item 1 is half misquoted.

## Findings

### N-1 The adjoint of (9.1) is not the adjoint of the volume-weighted product used in (9.2)–(9.4)
1. **Component:** "$\mathbf M^T\boldsymbol\phi_0^*=\lambda_0\mathbf F^T\boldsymbol\phi_0^*$" (§9.1, line 1050); "volume-weighted inner product" (§9.2, line 1060). $\mathbf M$ is the per-unit-volume balance (4.6)–(4.7).
2. **Claims vs computes:** claims $\rho_\text{NULL}=0$ for the exact adjoint. Transposing per-unit-volume rows gives $x^m=V^m\phi^{*m}$; weighting again with $V^m$ uses $(V^m)^2$.
3. **Counterexample (Analytic, numerical one-off):** 1D column, 20 nodes alternating 30/10 cm, two groups, $\hat D=0$, vacuum top; $\Sigma_{a2}+0.010$ cm⁻¹ in the top five nodes; $\boldsymbol\phi$ the perturbed eigenvector. Literal (9.1)+(9.2): $\rho=-36.8$ pcm (exact for any weight), $\rho_\text{CR}=-28.6$, $\rho_\text{NULL}=-8.2$ pcm. With $\phi^*=x/V$: $\rho_\text{CR}=-36.8$, $\rho_\text{NULL}=3\times10^{-11}$ pcm. Identical on a uniform mesh, so the present 21.515 cm mesh hides it.
4. **Root cause:** 5 (composition).
5. **Inherited by:** reactivity components (9.3), $\Lambda$, $\beta^\text{eff}_k$ (9.4), the $\rho_\text{NULL}$ and point-kinetics tests of §19.2.
6. **Fixes:** (a) define the adjoint on the volume-integrated system, $(\mathbf V\mathbf M)^T\boldsymbol\phi^*=\lambda_0(\mathbf V\mathbf F)^T\boldsymbol\phi^*$; physical, free. (b) keep (9.1) and drop the volume weight; then the edited "adjoint flux" is $V\phi^*$. Recommend (a).
7. **Severity:** wrong physics.

### N-2 Rod weights $w_{a,g}$ are undefined between nodal updates; the literal reading freezes the rod
1. **Component:** (8.2)–(8.3); "The three-node problem is solved at every nodal update" (line 1028); "A moving rod tip changes the removal cross section of its node at every step and so keeps the flag set" (§11.7, line 1402); §18.3 step 5.2.
2. **Claims vs computes:** step 1 of §11.7 needs $\Sigma_r$ of P at the new $\zeta$, which needs $w(\zeta^n)$, which only the nodal update that step 1 decides can produce.
3. **Counterexample (Source-derived, Analytic):** tip moves by $\Delta\zeta$ inside P, no nodal update yet. With the stored $w$: $\Sigma^P(t_n)=\Sigma^P(t_{n-1})$, $\Delta\Sigma_\text{max}=0$, the flag is never set by P, and the rod has no effect until the tip enters the next node, which has no weight at all. PARCS Eq. (9.4), $\Sigma_{UR}+\alpha\xi\Delta\Sigma_c$, avoids this: $\xi$ is current, $\alpha$ lagged. Even then the flag is set only when the accumulated change exceeds $\varepsilon_\text{Xsec}$, not at every step.
4. **Root cause:** 2 (ordering), 4 (state not covered).
5. **Inherited by:** every rod transient, bank worth, the cusping test.
6. **Fixes:** (a) store $\alpha_{a,g}=w_{a,g}/\zeta_a$ and use $w=\alpha\zeta^n$ until the next three-node solve, $\alpha=1$ for a newly entered node (PARCS; physical, first-order lag). (b) always solve the three-node problems of moving tips, independent of the flag (physical, small cost). Recommend (b), with (a) as predictor.
7. **Severity:** wrong physics (contradicts its own claim).

### N-3 §9.3 states the wrong sign of the missing delayed-neutron effectiveness
1. **Component:** "the higher worth of delayed neutrons, which comes from their lower emission energy, is absent" (line 1105).
2. **Claims vs computes:** $\beta^\text{eff}=\beta$ for uniform $\beta$ is correct algebra. The stated direction of the neglected effect is not.
3. **Counterexample (Reference-cited):** Marguet §17.10: importances "with a weight of 0.97 in PWR, suggest that βeff is smaller than β" (delayed neutrons, about 400 keV, miss the fast-fission bonus; valid for large low-leakage LWRs, reversed in small leaky cores). With the manual's $\beta=0.00740$ this is about −22 pcm on the prompt-critical threshold [my arithmetic].
4. **Root cause:** 3 (physical $\beta_k$ in a one-emission-group model).
5. **Inherited by:** O5, L9, every result in dollars, rod-ejection margins.
6. **Fixes:** correct the sentence. For O5: (a) tally adjoint-weighted $\beta_k$ (iterated fission probability) into the library, physical; (b) an input importance factor, which only moves the comparison unless tallied. Recommend (a).
7. **Severity:** wrong physics (statement); the model limit itself is documented.

### N-4 The three-node finite-difference current does not reduce to the kernel current; $N_\text{fine}$ has no value or convergence requirement
1. **Component:** §8.3 (8.4), "which give $\hat D$ on those faces through (4.3) in place of the two-node result"; §19.2 "smooth".
2. **Claims vs computes:** at $\zeta\to0$ or 1 the interface switches between ANM and fine-mesh finite difference; $\hat D$ jumps by the truncation error.
3. **Counterexample (Analytic, numerical one-off):** 1D column, 21.515 cm nodes, six unrodded below three rodded ($\Sigma_{r2}$ 0.085→0.110 cm⁻¹, smaller fast-group changes), radial buckling in $\mathbf A$, reference 0.05 cm mesh. P homogeneous unrodded, R rodded, exact U and R averages imposed. Thermal current at P/R against the reference (which ANM reproduces in 1D): −6.1 % at $N_\text{fine}=10$ (PARCS's Figure 9.2 value), −1.7 % at 20, −0.45 % at 40; $\hat D_2$ shifts by 8.5 % of $\tilde D_2$ at 10. Second order, not converged at 10.
4. **Root cause:** 6.
5. **Inherited by:** $k_\text{eff}$ against rod position at every node crossing; the cusping test.
6. **Fixes:** (a) split P at material boundaries and solve the U, P-segments, R problem with the §5 kernels and the same average constraints: exact in 1D, continuous with the two-node result; physical. (b) keep (8.4), state $N_\text{fine}\ge40$ and test convergence. Recommend (a).
7. **Severity:** unsupported.

### N-5 $\bar S_g$ in (13.2) is undefined; read as $S_g$ of (11.2) the claimed node balance fails
1. **Component:** "$\Sigma_{ps,g}=(\bar L_{g,z}-\bar S_g)/\bar\phi_g$ … By construction the node balance with $\mathbf A'$ holds" (§13.2).
2. **Claims vs computes:** with $\mathbf A$ of (5.2), the balance holds only if $\bar S_g=-(\bar\ell^\text{eff}_g-\bar\ell_g)$ of (11.10). PARCS Eq. (5.1) defines its $s$ that way, through the balance.
3. **Counterexample (Analytic):** $\theta=\tfrac12$, $\Delta t=1$ ms, $v_2\approx4\times10^5$ cm s⁻¹ [INFERENCE, typical]: taking $\bar S_2=S_2^n$ leaves the residual $\bar\phi_2/(\theta v_2\Delta t)=5\times10^{-3}\,\bar\phi_2$ cm⁻¹, about 6 % of thermal absorption, so the reconstructed node average no longer equals the nodal one.
4. **Root cause:** 5.
5. **Inherited by:** transient pin powers and detector responses.
6. **Fixes:** define $\bar{\mathbf S}\equiv\bar{\boldsymbol\ell}-\bar{\boldsymbol\ell}^\text{eff}$ in §13.2 (physical, free).
7. **Severity:** misleading.

### N-6 Appendix C misquotes PARCS Eq. (3.4) and omits the (9.4)/(9.5) inconsistency
1. **Component:** App. C item 1; "PARCS's flux-weighting factor is $\alpha_g=w_{a,g}/\zeta_a$" (line 990).
2. **Claims vs computes (Reference-cited, PARCS p. 12, p. 93):** printed $a^m=(\tilde D^{m+}+\tilde D^{m-}-\hat D^{m+}+\hat D^{m-})/h$, identical to the diagonal of (4.6); only $a^{m+}=-(\tilde D^{m+}-\hat D^{m+})/h$ is misprinted. Eq. (9.5) prints $\alpha_g$ as the rodded share of the flux integral, which is $w$, not $w/\zeta$.
3. **Counterexample (Analytic):** flat flux, $\zeta=0.5$: (9.5) gives 0.5, (9.4) then gives $\Sigma_{UR}+0.25\,\Delta\Sigma_c$; volume weighting requires 0.5. (8.2)–(8.3) are right.
4. **Root cause:** 5 (citation).
5. **Inherited by:** anyone cross-checking code against PARCS.
6. **Fixes:** correct item 1; add an item for (9.4)/(9.5).
7. **Severity:** misleading.

## Sound components

- Signs of (4.1), (4.5), (4.6), (5.23)–(5.26) are mutually consistent; (4.4) follows from the half-node albedo relation.
- (5.5)–(5.6) re-derived for unequal widths; (5.10)–(5.14) and App. A re-derived; (5.16)–(5.19) re-derived.
- (11.2) equals PARCS Eq. (2.22). (11.5) equals PARCS Eq. (2.24) as printed to $10^{-12}$ for four $(\lambda,\Delta t,\gamma)$ sets including $\gamma=100$. App. B recurrences and series are correct.
- (11.10) re-derived; App. C item 2 (sign, and $L_1$ in the group-2 line) and item 3 are real misprints. §11.7 matches PARCS §10.2.1.
- (9.4) is the correct reduction of PARCS Eqs. (9.68)–(9.73) for emission in group 1.
- (10.1)–(10.8) and (12.1)–(12.3) match PARCS §9.5–9.6; the explicit-Euler bound is right; the decay-heat balance closes.
- (13.3): all eight functions satisfy the modal equation; block sizes 3, 2, 2, 1 confirmed; (13.4) is PARCS Eq. (5.24) times −1, equivalent.
- §8.3: with per-group flux weights the three-node average of P equals the CMFD value at convergence (Analytic); (8.2)–(8.3) preserve reaction rates.
- D2 is described truthfully in §20.2 (the last paragraph of §7.2 names only the quadratic term); interpolating $1/(3D)$ is defensible.
