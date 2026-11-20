# Problem input contract · format version 2

The layout, units and validation rules of the problem input: the HDF5 file a run is started
from (Vision §1). It holds four **input documents**, each with one responsibility: the
**reactor**, the **initial state**, the **control signals** and the **settings**. A document
is written inside the problem input or in a **document file** of its own that the problem
input links to, so one reactor serves many initial states and settings. Version 1 (one file,
written in sub-milestone 1.3) is no longer read; version 2 was frozen at the start of
sub-milestone 1.3a (milestone 1 plan §3). Later milestones add rods, channels, the loop and
the knob schedules as additive changes (§10). Terms: `Planning/DBC/DEFINITIONS.md`.

The Python problem input writer in `tools/inputs/` writes it and does not validate; the
engine reads and validates it (`src/nts/io/problem.hpp`) and is the authority.

## 1. Conventions

- **Units.** Every float64 dataset of a physical quantity carries a string attribute `units`
  with exactly the value tabled below; the engine rejects any other string. The units are the
  engine's internal units (cm, K, kg m⁻³, ppm, W), so the engine converts nothing. A
  dimensionless physical quantity has `units` = `1`. Settings (§7) are pure numbers and carry
  no `units`.
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
  (group or dataset) that the contract does not name, at a root or inside a group it names,
  is rejected, so that a misspelled member never falls back to a default unnoticed. Attributes
  the contract does not name are ignored.
- **External links.** The four document members of the problem input's root (§2) are the only
  HDF5 external links the engine follows. An external link anywhere else, in the problem input
  or in a document file, is rejected without being followed.
- **Paths.** A path below is relative to the root of the document that holds it: `/geometry`
  is the `geometry` group of the reactor document. Inside the problem input that is
  `/reactor/geometry`; in a document file it is `/geometry`.

## 2. Problem input and documents

The root of the problem input:

| Member | Kind | Value |
|---|---|---|
| `format_version` | integer attribute | `2` |
| `reactor` | document, required | §3–§5 |
| `initial_state` | document, required | §6 |
| `control_signals` | document, optional | §8 |
| `settings` | document, optional; absent means every setting at its default | §7 |

When `format_version` is missing, not an integer or not 2, nothing else is checked: every
other rule depends on the layout it names.

A document member is either

- a **group** of the problem input: the document is written in place; or
- an **external link** to the root group `/` of a document file. A relative file name is
  resolved against the directory that holds the problem input, never against the working
  directory; an absolute one is used as written. The link must name the object `/`. The file
  must exist and open as HDF5.

Both forms define the same problem. The root group of every document, in place or in a
document file, carries:

| Attribute | Value |
|---|---|
| `document` | string, the member name it is read as: `reactor`, `initial_state`, `control_signals` or `settings` |
| `format_version` | integer, `2` |

A document whose `document` or `format_version` is missing or wrong is not read further.

## 3. Reactor: geometry

The reactor document holds the groups `geometry` (§3), `boundary_conditions` (§4), `library`
and `compositions` (§5), all required, and nothing else.

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

## 4. Reactor: boundary conditions

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

## 5. Reactor: library binding

`/library/` carries the string attribute `path`: the nodal library file, non-empty. A relative
path is resolved against the directory of the file that holds the reactor document: the
problem input when the reactor is written in place, its document file when linked. An
absolute path is used as written. The library must read and pass every check of the library
contract (`docs/contracts/nodal_library.md` §4); its diagnostics are reported with the
input's.

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

## 6. Initial state

The initial state document's root carries the string attribute `source`, which says where the
state comes from. Version 2 accepts only `stated`: the state is the datasets below. (Starting
from an earlier run's output is a later, additive value.) Its datasets:

| Dataset | Symbol (TM §2) | Shape | Units | Rule |
|---|---|---|---|---|
| `thermal_power` | P_th (TM §3.3) | scalar | `W` | finite, > 0 |
| `doppler_temperature` | T_D | scalar or `[nz][ny][nx]` | `K` | finite, > 0 |
| `moderator_density` | ρ_m | scalar or `[nz][ny][nx]` | `kg m^-3` | finite, > 0 |
| `moderator_temperature` | T_m | scalar or `[nz][ny][nx]` | `K` | finite, > 0 |
| `boron` | C_B | scalar or `[nz][ny][nx]` | `ppm` | finite, ≥ 0 |

All five are required. The four node-state fields give the **node state** each node's cross
sections are interpolated at (TM §7.1). A scalar applies to every node. An array has the
shape of the reactor's map; the values at excluded positions are not read and may be anything
(NaN included); the rule applies at every node. The bounds match the state-axis bounds of the
library contract §3.1.

**Library coverage.** Every node state lies within the tables of its composition: on each
state axis the node's composition set has, the value is at least the axis's first point and
at most its last (library contract §3.1). A field is not checked against an axis the set does
not have. A value outside is an input error unless the setting `allow_table_clamps` is 1
(§7); then the cross-section model clamps it to the nearest end point and counts it
(TM §7.2).

## 7. Settings

The settings document is optional, and so is each member; an absent member takes its
default. Every member is a scalar dataset.

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
| `allow_table_clamps` | — | integer | 0 (a node state outside its tables is an input error) or 1 (clamped and counted) | 0 | [OURS] (§6) |

The first twelve are the **solver options**. The two ε_k are different quantities: the
kernel switch threshold compares k∞ with k_eff in a node; the eigenvalue tolerance bounds the
change of k_eff between outer iterations. The tolerance defaults are PARCS's production
values (milestone 1 plan §2.1); verification runs set the tighter ones explicitly. PARCS
gives no default for N_upd (ΔN₀, "somewhat arbitrary but can be optimized", PARCS §4.2) or
for the outer iteration limit. N_upd changes the path, not the fixed point: the tests (4.10)
are checked on the first CMFD solution after a nodal update (TM §6.1). An iteration that
reaches the outer limit is reported as not converged. `allow_table_clamps` defaults to 0 so
that a run never extrapolates its tables silently; a clamped state is a modelling choice the
input must state.

## 8. Control signals

The control signals document will hold the knob schedules (milestones 3 and 4). Version 2
accepts only an empty one: a document with no members.

## 9. Validation and diagnostics

The engine checks every rule of §1–§8 before it solves anything, collects every failure and
reports them together. A diagnostic reads `<file>: <dataset or attribute>: <entry>: <message>`
(library contract §5); the entry part is left out with its separator where none applies. The
file is the file that holds the object: the problem input, a document file, or the library
for the library's own problems. The location is the object's path inside that file: in the
problem input a document's members are under `/<document>/`, in a document file under `/`. An
attribute is written `<object>@<name>`, the root `/` as `/@<name>`. Entries are written:

- a position: `position (i, j, k)`;
- an element of a 1-D dataset: `index <n>`, plus ` group <g>` for an albedo (g = 1, 2);
- an input composition: `input composition <c> ('<set>', '<composition>')`.

At most 20 entry diagnostics are listed per dataset, then one diagnostic giving the number
not listed. The rules checked, in this order, all of them on every run:

1. The problem input exists and opens as HDF5.
2. `format_version` of the problem input (§2); on failure nothing else.
3. Root members (§2): unknown members, missing required documents. Each document present:
   a group or an external link; for a link, its object `/` and its file, which must exist and
   open as HDF5; then the document's `document` and `format_version`. A document that fails
   here is not read further, and checks that need it are skipped. Unknown members and external
   links of a document's root are reported here; those of a named group, with its section.
4. Geometry (§3): types, shapes, units, widths, map shape and values, at least one node.
5. Boundary conditions (§4), the `exterior` face when the map excludes a position.
6. Library path (§5); the library itself (library contract §4).
7. Input compositions (§5): types, lengths, resolution against the library, validity of every
   used one. Resolution and validity are skipped when the library could not be read; types,
   lengths and unknown members are still checked. An unresolved composition is not checked
   for validity; every used one that resolves is, whatever happens to the others.
8. Initial state (§6): `source`; the five datasets, at every node.
9. Settings (§7).
10. Control signals (§8): its members are its root's, reported at item 3.
11. Library coverage (§6), unless `allow_table_clamps` is 1: per node-state field, at every
    node whose composition resolves, each value outside an axis of its set, naming the node
    position, the axis, the side and the end point. Skipped when the library, the map or that
    field is unusable.

Shape checks that need the map dimensions are skipped when the widths are unusable, and
per-node checks when the map is; the failures that caused the skip are reported. An
`allow_table_clamps` that breaks its rule is reported and counts as 0.

## 10. Changes and versions

Additive changes keep format version 2: a new optional group or dataset, a new setting with a
default that reproduces the version-2 behaviour, a new boundary `kind`, a new value of
`source`. The engine of the same commit knows every member it accepts; an older engine
rejects a newer member (§1). Anything else (a changed unit, shape, rule, default or meaning,
or a new required member) needs a new format version of the document it changes and of the
problem input.

| Date | Change |
|---|---|
| 2026-11-19 | Version 1, written at the start of sub-milestone 1.3. |
| 2026-11-19 | §8 item 7 wording after review, behaviour unchanged: only resolution and validity are skipped when the library could not be read; a used composition that resolves is checked even when another does not. |
| 2026-11-19 | §8 item 3 wording after review, behaviour unchanged: item 3 names the unknown root members; unknown members of a named group are reported with that group's section. |
| 2026-11-20 | Version 2, frozen at the start of sub-milestone 1.3a (the Captain's decision, milestone 1 plan §9): four input documents in place or in linked document files (§2); the library path resolves against the reactor document's file (§5); the initial state's `source` (§6); library coverage is an input error unless `allow_table_clamps` is 1, replacing version 1 §6, where a state outside the table was clamped and counted without error (§6, §7); the settings document holds the solver options of version 1 §7 under the same names and defaults; control signals must be empty (§8). Version 1 is no longer read. |
| 2026-11-20 | §9 items 3 and 10 wording before delivery, from the first implementation: unknown members and external links of a document's root are reported at item 3, so a member of the control signals document is reported there. |
