# Problem input contract · format version 1

The layout, units and validation rules of the problem input: the single HDF5 file that defines
a run (Vision §1). Written at the start of sub-milestone 1.3 (milestone 1 plan §3), which
freezes it. Version 1 holds the neutronics part: geometry, boundary conditions, the library
binding, the node states and the solver options. Later milestones add rods, channels, the
loop and the knobs as additive changes (§9). Terms: `Planning/DBC/DEFINITIONS.md`.

The Python problem input writer in `tools/inputs/` writes it and does not validate; the
engine reads and validates it (`src/nts/io/problem.hpp`) and is the authority.

## 1. Conventions

- **Units.** Every float64 dataset of a physical quantity carries a string attribute `units`
  with exactly the value tabled below; the engine rejects any other string. The units are the
  engine's internal units (cm, K, kg m⁻³, ppm, W), so the engine converts nothing. A
  dimensionless physical quantity has `units` = `1`. Solver options (§7) are pure numbers and
  carry no `units`.
- **Types.** Floating-point datasets are IEEE 754 binary64 (`float64`). Integer datasets and
  attributes are any HDF5 integer type; the engine reads them as signed 64-bit and checks
  their range. Strings, datasets and attributes, are variable-length UTF-8 (h5py's `str`); the
  engine also reads fixed-length strings, NUL- or space-padded.
- **Scalars.** A single number is a scalar dataset (rank 0, h5py `create_dataset(name,
  data=x)` with a Python number). A one-element array is not a scalar.
- **Positions.** The geometry is a box of `nx × ny × nz` positions. Arrays over positions are
  row-major `[z][y][x]` (x fastest), as the node ordering of the engine (coding standards §3).
  Position `(i, j, k)` has x index i, y index j and z index k, zero-based.
- **Groups.** Energy group index 0 is fast and 1 is thermal.
- **Members.** Every group and dataset this contract names is listed in its section. A link
  (group or dataset) that the contract does not name, at the root or inside a group it names,
  is rejected, so that a misspelled member never falls back to a default unnoticed. Attributes
  the contract does not name are ignored.

## 2. Root

| Member | Kind | Value |
|---|---|---|
| `format_version` | integer attribute | `1` |
| `geometry` | group | §3 |
| `boundary_conditions` | group | §4 |
| `library` | group | §5 |
| `compositions` | group | §5 |
| `operating_state` | group | §6 |
| `solver_options` | group, optional | §7 |

When `format_version` is missing, not an integer or not 1, nothing else is checked: every
other rule depends on the layout it names.

## 3. Geometry

`/geometry/`:

| Dataset | Type and shape | Units | Rule |
|---|---|---|---|
| `width_x`, `width_y`, `width_z` | float64 `[n]`, n ≥ 1 | `cm` | every width finite and > 0; non-uniform widths allowed |
| `composition_map` | integer `[nz][ny][nx]` | — | nx, ny, nz are the lengths of the three width datasets; every value −1 or an input composition index 0 … K − 1 (§5) |

The node widths along x are `width_x[i]`, and so on. A position whose map value is −1 is an
**excluded position**: outside the domain, with no node, no state and no data. Every other
position is a **node** of the problem, made of input composition `composition_map[k][j][i]`.
At least one position is a node. The engine numbers the nodes in position order, x fastest,
skipping excluded positions.

## 4. Boundary conditions

The six **bounding faces** of the box are named `x_minus`, `x_plus`, `y_minus`, `y_plus`,
`z_minus`, `z_plus` (`u_minus` at the smaller u). A node face that lies on a bounding face
takes that face's condition. A node face next to an excluded position is an **exterior face**
and takes the condition named `exterior`.

`/boundary_conditions/<face>/` is a group per face, with the string attribute `kind`:

| `kind` | Albedo α_b of TM Eq. (4.4), J_out = α_b φ_s, both groups | Members |
|---|---|---|
| `reflective` | 0 (zero net current) | none |
| `vacuum` | 1/2 (zero incoming current, Marshak) | none |
| `zero_flux` | 10³⁰ | none |
| `albedo` | `albedo[g]` per energy group | dataset `albedo`: float64 `[2]`, `units` = `1`, each finite and ≥ 0 (TM §4.2) |

- The six bounding-face groups are required.
- `exterior` is required when the map holds an excluded position; otherwise it may be present
  and is unused.
- A face group with any member other than the `albedo` its kind requires is rejected; so is a
  missing `albedo` for kind `albedo`.

## 5. Library binding

`/library/` carries the string attribute `path`: the nodal library file, non-empty. A relative
path is resolved against the directory that holds the problem input; an absolute path is used
as written. The library must read and pass every check of the library contract
(`docs/contracts/nodal_library.md` §4); its diagnostics are reported with the input's.

`/compositions/` lists the K **input compositions** the map indexes:

| Dataset | Type and shape | Rule |
|---|---|---|
| `library_set` | string `[K]`, K ≥ 1 | each names a composition set of the library |
| `library_composition` | string `[K]` | each names a composition of that set |

Input composition c is composition `library_composition[c]` of set `library_set[c]`. Several
input compositions may name the same library composition. An input composition is **used**
when some node is made of it. Every input composition must resolve; a used one must also have
every entry valid (library contract §4, per composition in use). An unused one is not checked
for validity.

## 6. Operating state

`/operating_state/`:

| Dataset | Symbol (TM §2) | Shape | Units | Rule |
|---|---|---|---|---|
| `thermal_power` | P_th (TM §3.3) | scalar | `W` | finite, > 0 |
| `doppler_temperature` | T_D | scalar or `[nz][ny][nx]` | `K` | finite, > 0 |
| `moderator_density` | ρ_m | scalar or `[nz][ny][nx]` | `kg m^-3` | finite, > 0 |
| `moderator_temperature` | T_m | scalar or `[nz][ny][nx]` | `K` | finite, > 0 |
| `boron` | C_B | scalar or `[nz][ny][nx]` | `ppm` | finite, ≥ 0 |

The four node-state fields give the **node state** each node's cross sections are
interpolated at (TM §7.1). A scalar applies to every node. In an array, the values at excluded
positions are not read and may be anything (NaN included); the rule applies at every node. The
bounds match the state-axis bounds of the library contract §3.1. A node state outside the
table of its composition is not an input error: the cross-section model clamps and counts it
(TM §7.2). A composition set without some axis ignores that field.

## 7. Solver options

`/solver_options/` is optional, and so is each member; an absent member takes its default.
Every member is a scalar dataset.

| Dataset | Symbol | Type | Rule | Default | Source of the default |
|---|---|---|---|---|---|
| `kernel` | — | string | `finite_difference` (D̂ = 0), `nem`, `anm` or `hybrid` | `hybrid` | TM §5.8 |
| `kernel_switch_threshold` | ε_k of TM Eq. (5.27) | float64 | finite, ≥ 0 | 0.01 | O8 |
| `wielandt_shift` | δk of TM Eq. (4.8) | float64 | finite, > 0 | 0.04 | TM §4.4 |
| `eigenvalue_tolerance` | ε_k of TM Eq. (4.10) | float64 | finite, > 0 | 5e-6 | TM §4.6 |
| `fission_source_l2_tolerance` | ε_L2 | float64 | finite, > 0 | 5e-5 | TM §4.6 |
| `fission_source_linf_tolerance` | ε_L∞ | float64 | finite, > 0 | 5e-4 | TM §4.6 |
| `inner_reduction` | ε_erf of TM §4.5 | float64 | finite, 0 < ε < 1 | 0.01 | TM §4.5 |
| `inner_iteration_limit` | N_in | integer | ≥ 1 | 1 | PARCS §10.1.7 |
| `outer_iteration_limit` | — | integer | ≥ 1 | 1000 | [OURS] |
| `nodal_update_period` | N_upd of TM §6.1 | integer | ≥ 1 | 5 | [OURS] |
| `coupling_relaxation` | ω_D of TM §6.2 | float64 | finite, 0 < ω ≤ 1 | 1 | TM §6.2 |
| `adjoint` | — | integer | 0 (off) or 1 (on) | 0 | — |

The two ε_k are different quantities: the kernel switch threshold compares k∞ with k_eff in
a node; the eigenvalue tolerance bounds the change of k_eff between outer iterations. The
tolerance defaults are PARCS's production values (milestone 1 plan §2.1); verification runs
set the tighter ones explicitly. PARCS gives no default for N_upd (ΔN₀, "somewhat arbitrary
but can be optimized", PARCS §4.2) or for the outer iteration limit. N_upd changes the path,
not the fixed point: the tests (4.10) are checked on the first CMFD solution after a nodal
update (TM §6.1). An iteration that reaches the outer limit is reported as not converged.

## 8. Validation and diagnostics

The engine checks every rule of §1–§7 before it solves anything, collects every failure and
reports them together. A diagnostic reads `<file>: <dataset or attribute>: <entry>: <message>`
(library contract §5); the entry part is left out with its separator where none applies. The
file is the problem input, or the library for the library's own problems. An attribute is
written `<object>@<name>`. Entries are written:

- a position: `position (i, j, k)`;
- an element of a 1-D dataset: `index <n>`, plus ` group <g>` for an albedo (g = 1, 2);
- an input composition: `input composition <c> ('<set>', '<composition>')`.

At most 20 entry diagnostics are listed per dataset, then one diagnostic giving the number
not listed. The rules checked, in this order, all of them on every run:

1. The file exists and opens as HDF5.
2. `format_version` (§2); on failure nothing else.
3. Root members (§2); unknown members (§1).
4. Geometry (§3): types, shapes, units, widths, map shape and values, at least one node.
5. Boundary conditions (§4), the `exterior` face when the map excludes a position.
6. Library path (§5); the library itself (library contract §4).
7. Input compositions (§5): types, lengths, resolution against the library, validity of every
   used one. Skipped when the library could not be read; an unresolved composition is not
   checked for validity.
8. Operating state (§6), at every node.
9. Solver options (§7).

Shape checks that need the map dimensions are skipped when the widths are unusable, and
per-node checks when the map is; the failures that caused the skip are reported.

## 9. Changes and versions

Additive changes keep format version 1: a new optional group or dataset, a new solver option
with a default that reproduces the version-1 behaviour, a new boundary `kind`. The engine of
the same commit knows every member it accepts; an older engine rejects a newer member (§1).
Anything else (a changed unit, shape, rule, default or meaning, or a new required member)
needs format version 2.

| Date | Change |
|---|---|
| 2026-11-19 | First version, written at the start of sub-milestone 1.3. |
