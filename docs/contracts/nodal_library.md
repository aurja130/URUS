# Nodal library contract · format version 1

The layout, units and validity rules of the nodal library: the HDF5 file of nodal data the
engine reads (TM §3.4, §7, §19.3). Written in sub-milestone 1.2 (milestone 1 plan §3); it
freezes when 1.2 closes, because data milestone DM1 writes to it. After the freeze a change
needs a new format version unless it is additive (§8). Terms: `Planning/DBC/DEFINITIONS.md`.

The library writer in `external/BEAVRS_minimal` produces it; the engine reads and validates
it (`src/nts/io/nodal_library.hpp`).

## 1. Conventions

- **Units.** One accepted unit per quantity, written in its `units` attribute exactly as in
  the tables below. The engine rejects any other unit string. The units are the engine's
  internal units (cm, s, K, kg m⁻³, ppm, J), so the engine converts nothing.
- **Types.** Floating-point datasets are IEEE 754 binary64 (`float64`). Integer attributes
  are any HDF5 integer type. Strings, both datasets and attributes, are variable-length UTF-8
  (h5py's `str`); the engine also reads fixed-length strings.
- **Compression.** Datasets may be chunked and compressed with HDF5's built-in gzip and shuffle
  filters only.
- **Groups.** Energy group 1 (index 0) is fast, group 2 (index 1) is thermal, split at
  0.625 eV. Delayed groups are indexed 0–7. Exactly two energy groups and eight delayed groups
  (O2); a file stating any other count is rejected.
- **Order.** Multi-dimensional datasets are row-major (C order), the last index fastest.

## 2. Root attributes

| Attribute | Type | Value |
|---|---|---|
| `format_version` | integer | `1` |
| `energy_groups` | integer | `2` |
| `delayed_groups` | integer | `8` |
| `group_boundary_eV` | float64 | Boundary between groups 1 and 2, eV; finite and > 0 (`0.625`) |
| `generator` | string | Name of the program that wrote the file, non-empty |
| `generator_commit` | string | Git commit of that program, with `-dirty` appended when its working tree had changes, non-empty |

The root holds the group `composition_sets` and may hold the group `provenance` (§6). Other
root members are ignored.

## 3. Composition sets

`/composition_sets/<set>/` holds compositions that share one table of branch states. A file
has at least one set. Set names are unique HDF5 link names.

### 3.1 Axes

`/composition_sets/<set>/axes/<axis>`: one 1-D float64 dataset per axis of the set, drawn
from this vocabulary:

| Axis | Units | Points |
|---|---|---|
| `fuel_temperature` | `K` | > 0 |
| `moderator_density` | `kg m^-3` | > 0 |
| `moderator_temperature` | `K` | > 0 |
| `boron` | `ppm` | ≥ 0 |

- A set has zero to four axes; a name outside the vocabulary is rejected.
- The dimension order of every table in the set is the vocabulary order above, restricted to
  the axes present. The set's state count S is the product of the axis lengths (1 with no
  axes); a branch state of the set is one point of each axis.
- Each axis has length ≥ 1; its points are finite, within the bounds above and strictly
  ascending; its `units` attribute is the unit of the table.
- A length-1 axis is a fixed value: the interpolation (1.3, TM §7.2) clamps any other state
  on it.

### 3.2 Compositions

`/composition_sets/<set>/names`: 1-D string dataset of length N ≥ 1, the composition names,
each non-empty and unique within the set. Composition c of the set is `names[c]`.

### 3.3 Quantities

`/composition_sets/<set>/<quantity>/` is a group holding two float64 datasets, `value` and
`std_dev` (one standard deviation, same units), of identical shape `[N, axes…, extra…]`. The
group carries the `units` attribute. The `std_dev` dataset carries the string attribute
`method`: `tally` when it is the standard deviation openmc.mgxs computes for the quantity, or
`first-order propagation without correlations` when the writer propagated it (§7). The engine
does not interpret `method`.

| Quantity | Symbol (TM §2) | Units | Extra dimensions | Required | Entry rule |
|---|---|---|---|---|---|
| `D1`, `D2` | $D_1$, $D_2$ | `cm` | — | yes | > 0 |
| `Sigma_a1`, `Sigma_a2` | $\Sigma_{a1}$, $\Sigma_{a2}$ | `cm^-1` | — | yes | > 0 |
| `Sigma_s12` | $\Sigma_{12}$, the effective down-scatter of TM Eq. (3.5) | `cm^-1` | — | yes | > 0 |
| `Sigma_s21` | $\Sigma_{21}$, the tallied up-scatter, kept for traceability; the engine assumes none (TM §3.1, assumption 4) | `cm^-1` | — | yes | ≥ 0 |
| `nu_Sigma_f1`, `nu_Sigma_f2` | $\nu\Sigma_{f1}$, $\nu\Sigma_{f2}$ | `cm^-1` | — | yes | ≥ 0 |
| `kappa_Sigma_f1`, `kappa_Sigma_f2` | $\kappa\Sigma_{f1}$, $\kappa\Sigma_{f2}$ | `J cm^-1` | — | yes | ≥ 0 |
| `inv_v1`, `inv_v2` | $1/v_1$, $1/v_2$ | `s cm^-1` | — | yes | > 0 |
| `beta` | $\beta_k$, physical delayed fractions | `1` | `[8]` delayed group | yes | each ≥ 0; their sum < 1 |
| `lambda` | $\lambda_k$ | `s^-1` | `[8]` delayed group | yes | each ≥ 0, and > 0 where $\beta_k$ > 0 |
| `Sigma_f1`, `Sigma_f2` | $\Sigma_{f1}$, $\Sigma_{f2}$ (data item L3) | `cm^-1` | — | no | ≥ 0 |
| `discontinuity_factor` | $f_{g,u\pm}$ (TM §5.6, §5.9; data item L1) | `1` | `[6, 2]` face (x−, x+, y−, y+, z−, z+), energy group | no | > 0 |

Every value in an entry rule is also finite. A zero delayed abundance is allowed (P7). A
present optional quantity follows every rule of a required one; an absent one means the
engine's default (no $\Sigma_f$ data; discontinuity factors equal to one).

**Why $\Sigma_{12}$ > 0 and not ≥ 0.** Every material in a light-water core down-scatters; an
exact zero arises only from a tally that scored nothing (TM §19.3), and an effective value
≤ 0 means the up-scatter correction (3.5) overshoots on noisy tallies. Neither is usable.

### 3.4 Entries and the validity flag

An **entry** is one composition at one branch state: index `[c, i₁, …]` of the leading
dimensions. `/composition_sets/<set>/valid` is a uint8 dataset of shape `[N, axes…]`:

- `1`: the entry holds physical values. Every quantity present satisfies its entry rule in
  every component, and every standard deviation is finite and ≥ 0.
- `0`: the entry holds no physical value (data item L14). Its values are undefined and may be
  NaN, infinite or of any sign; nothing may compute with them.

The writer sets `0` exactly where some quantity breaks its entry rule or some standard
deviation is non-finite or negative.

## 4. What the engine checks when it reads a library

All failures are collected and reported together; each diagnostic names the file, the
dataset or attribute, the entry where there is one, and the rule. At most 20 entry
diagnostics are listed per dataset, followed by one diagnostic giving the number not listed.

1. The file opens as HDF5.
2. Root attributes of §2: present, of the stated type, with the stated value or bound.
3. `composition_sets` exists and holds at least one set.
4. Per set: the axis rules of §3.1; `names` per §3.2; every required quantity present; for
   every present quantity, both datasets float64 with the shape of §3.3 and the `units`
   attribute exactly as tabled; `valid` uint8 of shape `[N, axes…]` holding only 0 and 1.
5. Per set, every entry with `valid = 1` satisfies §3.4. A violation means the writer is
   wrong; the file is rejected, naming composition, quantity, component and branch state.

Members the engine does not know are ignored (§8), except that an unknown axis is rejected
because it changes every shape.

**Per composition in use.** A problem input (1.3) uses some compositions; the engine rejects
a used composition with any entry of `valid = 0`, naming the composition and the entry. An
unused composition may hold invalid entries.

## 5. Diagnostic naming

A diagnostic reads `<file>: <dataset or attribute>: <entry>: <message>`, the entry part empty
where none applies. An entry is written as
`composition <c> '<name>', <axis>[<i>] = <point> <units>, …` followed, for a quantity with
extra dimensions, by `delayed group <k>` or `face <face>, group <g>`.

## 6. Provenance

`/provenance` records where every entry came from. The engine does not read it.

Attributes: `openmc_version` (string), `nuclear_data` (string; declared by whoever runs the
writer, because a statepoint does not record its nuclear data library), `batches`,
`inactive_batches`, `particles` (integers; identical in every run, else the writer stops).

`/provenance/runs/`: parallel 1-D datasets over the R runs read:

| Dataset | Type | Meaning |
|---|---|---|
| `directory` | string | Name of the run's directory |
| `statepoint_sha256` | string | SHA-256 of the statepoint file, hexadecimal |
| `k_eff`, `k_eff_std_dev` | float64 | The run's combined k-effective and its standard deviation |
| `seed` | integer | The run's random-number seed |

`/provenance/composition_sets/<set>/run_index`: int32 `[N, axes…]`, the run each entry was
extracted from (row of `/provenance/runs`).

`/provenance/tally_mesh`: attributes `dimension` (3 integers, x, y, z), `lower_left` and
`upper_right` (3 float64, cm) of the mesh the tallies were scored on.

The writer may add further provenance (§7.4).

## 7. The library writer

`external/BEAVRS_minimal/library_writer.py` re-extracts every quantity from the statepoints
of a directory given as an argument; it runs no transport calculation. Extraction uses
openmc.mgxs (`nodal_extraction.py`, shared with `run_model.py`).

### 7.1 Per run

- Mean and standard deviation of $D_g$, $\Sigma_{ag}$, the ν-scatter matrix (giving
  $\Sigma_{12}$ and $\Sigma_{21}$), $\nu\Sigma_{fg}$, $\kappa\Sigma_{fg}$, $1/v_g$, the
  delayed ν-fission per delayed group, the decay rates, and the node flux $\bar\phi_g$.
- openmc.mgxs replaces NaN by 0 and ±∞ by ±1.797 693 134 862 315 7 × 10³⁰⁸ (`np.nan_to_num`).
  The writer turns ±that value back into ±∞ in every value and standard deviation, so an
  infinite diffusion coefficient from an empty tally breaks the finiteness rule instead of
  passing as a huge number. A 0 that was NaN stays 0 and breaks the > 0 rules.
- **Effective down-scatter**, TM Eq. (3.5):
  $\Sigma_{12}^\text{eff}=\Sigma_{12}-\Sigma_{21}\,\bar\phi_2/\bar\phi_1$ with the node's tallied
  flux. Standard deviation propagated to first order without correlations:
  $\sigma^2=\sigma_{\Sigma_{12}}^2+r^2\sigma_{\Sigma_{21}}^2+(\Sigma_{21}/\bar\phi_1)^2\sigma_{\phi_2}^2+(\Sigma_{21}r/\bar\phi_1)^2\sigma_{\phi_1}^2$,
  $r=\bar\phi_2/\bar\phi_1$. Where $\bar\phi_1 = 0$ the value is NaN and the entry invalid.
- **Delayed fractions**, as `run_model.py` defined them (TM §7.5): with
  $P_k=\sum_g\nu_{d,k}\Sigma_{fg}\bar\phi_g$ and $P=\sum_g\nu\Sigma_{fg}\bar\phi_g$,
  $\beta_k=P_k/P$ where $P>0$, else 0. Standard deviation to first order without
  correlations:
  $\sigma_{\beta_k}^2=\sum_g\big[(\bar\phi_g/P)^2\sigma_{\nu_{d,k}\Sigma_{fg}}^2+(\beta_k\bar\phi_g/P)^2\sigma_{\nu\Sigma_{fg}}^2+((\nu_{d,k}\Sigma_{fg}-\beta_k\nu\Sigma_{fg})/P)^2\sigma_{\phi_g}^2\big]$;
  0 where $P\le 0$.
- $\kappa\Sigma_{fg}$ is converted from eV cm⁻¹ to J cm⁻¹ with 1 eV = 1.602 176 634 × 10⁻¹⁹ J
  (exact, SI 2019), value and standard deviation.

### 7.2 Branch states of a run

Read from the run's `materials.xml`, never from its directory name: the fuel temperature is
the temperature of the materials whose name starts with `Fuel ` (all equal, else the writer stops); the
moderator temperature is that of `Borated Water`; boron is the mass fraction of boron in
`Borated Water` × 10⁶; the moderator density is the water density
$\rho_\text{solution}(1 - C_B\times10^{-6})$, which inverts `openmc.model.borated_water`, in
kg m⁻³. The rod state comes from the directory name (`CR_0` all banks out, `CR_1` all in).

### 7.3 The present library

The 72 statepoints of `~/Programming/OpenMC_inputs/BEAVRS_minimal/beavrs_parameter_sweep/`
(6 fuel temperatures × 6 moderator states × 2 rod states, TM §19.3) give one set,
`beavrs_full_core`:

- Axes `fuel_temperature` (6), `moderator_density` (6, sorted ascending), `boron` (1:
  975 ppm). The moderator temperature rides on the density (TM §7.3) and is no axis; it is
  recorded in `/provenance/composition_sets/beavrs_full_core/moderator_temperature`, aligned
  with the density axis.
- N = 7650 compositions: each of the 15 × 15 × 17 tally-mesh nodes unrodded and rodded
  (TM §7.1). Composition `c = r·3825 + x + 15y + 225z` with r = 0 unrodded, 1 rodded and
  x, y, z the mesh indices, x fastest; its name is `unrodded_xXX_yYY_zZZ` or
  `rodded_xXX_yYY_zZZ`, indices zero-based and two digits.
- The file replaces `beavrs_kinetics_library.hdf5` at the same path in the submodule.

### 7.4 Reproduction check

Before it writes the file over the previous library, the writer compares every raw quantity
it extracted ($D_g$, $\Sigma_{ag}$, the raw $\Sigma_{12}$ before Eq. (3.5), $\nu\Sigma_{fg}$,
$\kappa\Sigma_{fg}$ in eV cm⁻¹, $1/v_g$, $\beta_k$, $\lambda_k$) with the previous file, entry
by entry, matched by branch state. Every value must be bit-identical, with NaN at the same
entries. Any difference stops the writer and is reported. The check's result is recorded as
the provenance attribute `reproduction_check`.

## 8. Changes and versions

Additive changes keep format version 1: a new composition set, a new optional quantity, a new
provenance member, a new root attribute. A version-1 reader ignores members it does not
know. Anything else (a new axis, a changed unit, shape, rule or meaning) needs format
version 2.

| Date | Change |
|---|---|
| 2026-11-18 | First version, written at the start of sub-milestone 1.2. |
