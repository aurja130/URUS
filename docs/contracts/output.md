# Output contract · format version 2

The layout and units of the output: the HDF5 file `urus` writes for a run (Vision §1). It
holds the eigenvalue, the solution fields and the run record that says whether the result is
valid. Frozen at the start of sub-milestone 1.4 (milestone 1 plan §3). Later sub-milestones
add the nodal kernels' convergence data, the adjoint flux and the thermal-hydraulic fields as
additive changes (§7). Terms: `Planning/DBC/DEFINITIONS.md`.

The engine writes it (`src/nts/io/output.hpp`); nothing in the engine reads it yet.

## 1. Conventions

- **Units.** Every float64 dataset carries a string attribute `units` with exactly the value
  tabled below; a dimensionless quantity has `units` = `1`. The units are the engine's
  internal units (cm, s, W, J).
- **Types.** Floating-point datasets are IEEE 754 binary64 (`float64`); integer datasets and
  attributes are signed 64-bit (`int64`); strings, datasets and attributes, are
  variable-length UTF-8 (h5py's `str`).
- **Scalars.** A single number or string is a scalar dataset (rank 0).
- **Positions.** Arrays over positions have the shape of the reactor's composition map,
  `[nz][ny][nx]`, row-major with x fastest (problem input contract §1, §3). A position that
  holds no node (an excluded position) holds NaN in every field.
- **Groups.** Energy group index 0 is fast and 1 is thermal.

## 2. Command and exit status

```text
urus <problem input> -o <output>
```

Both arguments are required, in this form. `urus` reads and validates the problem input
(problem input contract §9), solves the steady state, writes the output and prints the
eigenvalue and the convergence state to standard output. Diagnostics go to standard error.

| Exit status | Meaning | Output written |
|---|---|---|
| 0 | Valid result: converged, no table clamp | yes |
| 1 | Usage error, or the output could not be written | no |
| 2 | Problem input or library rejected, or an input file could not be read (for its SHA-256), or the problem asks for a method this engine version does not provide (§6) | no |
| 3 | Not converged: the outer iteration limit was reached | yes |
| 4 | Converged with table clamps | yes |

When both 3 and 4 apply, the status is 3. The output is written to a temporary file in the
output's directory and renamed onto `<output>` when complete, so `<output>` is either the
whole previous file, the whole new one, or absent. With status 1 or 2 an existing `<output>`
is left untouched.

## 3. Root

| Member | Kind | Value |
|---|---|---|
| `format_version` | integer attribute | `2` |
| `eigenvalue` | group | §4 |
| `fields` | group | §5 |
| `run_record` | group | §6 |

## 4. Eigenvalue

`/eigenvalue/`:

| Dataset | Symbol (TM §2) | Shape | Units |
|---|---|---|---|
| `k_eff` | k_eff, the eigenvalue of the last outer iteration (TM Eq. (4.9)) | scalar | `1` |

## 5. Fields

`/fields/`, the solution fields of the steady state, scaled so that the fission power of the
core equals the thermal power of the initial state (TM §3.3):

| Dataset | Symbol (TM §2) | Shape | Units |
|---|---|---|---|
| `flux` | $\bar\phi_g$, node-average scalar flux | `[2][nz][ny][nx]` | `cm^-2 s^-1` |
| `power` | $P_f^m$, node fission power (TM Eq. (3.10)) | `[nz][ny][nx]` | `W` |
| `power_density` | $P_f^m / V^m$ | `[nz][ny][nx]` | `W cm^-3` |

With exit status 3 the fields are those of the last outer iteration, scaled the same way.

## 6. Run record

`/run_record/` carries the attributes

| Attribute | Type | Value |
|---|---|---|
| `valid` | integer | 1 for exit status 0, else 0 |
| `exit_status` | integer | the exit status of §2: 0, 3 or 4 |
| `reason` | string | `converged`; `not converged: outer iteration limit <n> reached`; or `converged with table clamps: <n> node-axis clamps` |

and the groups below.

**`convergence/`**, the convergence state of the eigenvalue solve (TM §4.4–§4.6):

| Dataset | Type | Units | Value |
|---|---|---|---|
| `converged` | integer | — | 1 when the tests of TM Eq. (4.10) held at some outer iteration within the limit, else 0 |
| `outer_iterations` | integer | — | outer iterations done |
| `inner_iterations` | integer | — | linear-solver iterations, summed over all outer iterations |
| `nodal_updates` | integer | — | nodal updates done; 0 for the finite-difference kernel |
| `eigenvalue_change` | float64 | `1` | δ_k of the last outer iteration |
| `fission_source_l2_change` | float64 | `1` | δ_L2 of the last outer iteration |
| `fission_source_linf_change` | float64 | `1` | δ_L∞ of the last outer iteration, over nodes with fission production |
| `residual` | float64 | `1` | ‖Mφ − λFφ‖₂ / ‖λFφ‖₂ at the final flux and eigenvalue (TM §4.6), M and F of TM Eq. (4.7) |
| `coupling_correction_change` | float64 | `1` | the largest change of a coupling correction D̂ (dimensionless, TM Eq. (4.1)) in the last nodal update; 0 when no nodal update was done |

**`table_clamps/`**:

| Dataset | Type | Value |
|---|---|---|
| `counts` | integer `[4][2]` | nodes clamped per state axis (rows in the library contract's vocabulary order: `fuel_temperature`, `moderator_density`, `moderator_temperature`, `boron`) and side (column 0 below the first point, 1 above the last) |

**`options/`**: every solver option as used, one scalar dataset per setting of problem input
contract §7 under the same name and type (`kernel` a string, `adjoint` and
`allow_table_clamps` integers 0 or 1), whether it was given or defaulted. Settings carry no
`units`.

**`methods/`**: scalar strings naming the methods used.

| Dataset | Value in format version 2 |
|---|---|
| `kernel` | `finite_difference` |
| `eigenvalue_iteration` | `Wielandt-shifted inverse iteration (TM Eqs. (4.8)-(4.9))` |
| `linear_solver` | `BiCGSTAB (Eigen)` |
| `preconditioner` | `incomplete LU with dual threshold (Eigen IncompleteLUT)` |

**`provenance/`**:

| Dataset | Type | Value |
|---|---|---|
| `problem_input` | string | the problem input's path, absolute |
| `problem_input_sha256` | string | SHA-256 of the problem input file, 64 lower-case hexadecimal digits |
| `document_files` | string `[4]` | the file holding each input document (`reactor`, `initial_state`, `control_signals`, `settings`), absolute; empty for an absent optional document |
| `document_sha256` | string `[4]` | SHA-256 of each of those files; empty for an absent document |
| `library` | string | the nodal library's path, absolute |
| `library_sha256` | string | SHA-256 of the nodal library file |
| `engine_version` | string | the engine's version (CMake project version) |
| `engine_commit` | string | the git commit the engine was built from, with `-dirty` appended when its working tree had changes to tracked files; `unknown` without git |
| `build_preset` | string | the CMake preset of the build; `none` when configured without one |
| `build_type` | string | the CMake build type; `unspecified` when no build type was set |
| `wall_time` | float64, `units` = `s` | wall time from the start of `urus` to the end of the solve |

**Methods this engine version provides.** Format version 2 is written by an engine with the
finite-difference kernel only. A problem whose settings ask for `kernel` other than
`finite_difference`, or for `adjoint` = 1, is rejected with exit status 2 and a diagnostic
naming the setting. So is one whose `wielandt_shift` is below 1e-6, a floor far above the
shift of about 1e-16·k at which k + δk rounds to k and the shifted source of TM Eq. (4.8)
vanishes. So is a problem in which no node has νΣ_f > 0 or no node has
κΣ_f > 0, since it has no eigenvalue or no power to scale to; the diagnostic names the problem
input. A diagnostic about a setting names the file that holds the settings document (the
problem input, or the linked settings file) and the setting's location in that file (problem
input contract §9).

## 7. Changes and versions

Additive changes keep the format version: a new group, dataset or attribute, a new value of a
string that names a method. Anything else (a changed unit, shape, layout or meaning, or a
removed member) needs a new format version.

| Date | Change |
|---|---|
| 2026-11-26 | Version 1, frozen at the start of sub-milestone 1.4. |
| 2026-11-27 | Version 2: `coupling_correction_change` units `cm` -> `1` (D̂ is dimensionless, TM Eqs. (4.1)-(4.2)); plus the clarifications of §2 and §6 (`unspecified` build type; unreadable input and `wielandt_shift` give exit status 2; diagnostics name the settings file and location). Version 1 was frozen on 2026-11-26 and never written. |
