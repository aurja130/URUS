# Project Vision

A coupled 3D nodal neutronics and thermal-hydraulics reactor simulator. It is
built around the MIT BEAVRS PWR benchmark, and students use it to explore
reactor behavior through a small set of operator controls.

## 1. Product shape

| Layer | Form | When |
|---|---|---|
| Simulation engine | C++ library + executable | From the start |
| Interface | Command line; HDF5 input, HDF5 output | From the start |
| Python package | Thin wrapper that writes input HDF5, runs the engine, reads output HDF5 | Polishing stage only |

- The engine is a command-line program until the polishing stage. No GUI, no
  embedded scripting, and no Python bindings into the engine internals.
- HDF5 is the only input and output format. An input file contains the reactor
  description, the operating state, the knob settings or schedules, and solver
  options. An output file contains the solution fields and time histories.
- The future Python package depends only on the HDF5 file contract. The C++ side
  must not need changes to support it.

## 2. Physics scope

The internal physics must be equivalent to **PARCS** and **SIMULATE-5**
(references: `Resources/PARCS_theory_manual.pdf`,
`Resources/Studsvik_next-generation_nodal_code_SIMULATE-5.pdf`). It must be
comprehensive, fully 3D, and fully coupled.

### 2.1 Neutronics

- Few-group nodal diffusion on a 3D Cartesian node mesh. Two groups is the
  BEAVRS baseline, but the group count is not hard-coded.
- Nonlinear nodal method: coarse-mesh finite difference (CMFD) global solve
  with two-node corrections from the analytic nodal method (ANM) and/or the
  nodal expansion method (NEM), as in PARCS.
- Assembly discontinuity factors, plus a transverse-leakage approximation.
- Steady-state eigenvalue solve (k-eff) and critical boron search.
- Time-dependent spatial kinetics with delayed-neutron precursors (the existing
  data library has 8 delayed groups). Time integration is theta-method with
  exponential transformation, as in PARCS.
- Xenon and samarium dynamics, and decay heat.
- Partial control-rod insertion within a node, with rod cusping correction.
- Pin power reconstruction from nodal solutions and form functions.
- Cross-section feedback: per-node interpolation in fuel temperature, moderator
  temperature/density, boron concentration, and control-rod state.

### 2.2 Thermal-hydraulics

- Core: one flow channel per assembly (or finer), with the same axial nodes as
  the neutronics. Each channel solves mass, energy, and momentum balances.
  Optional assembly-to-assembly cross flow, as in SIMULATE-5's COBRA-type
  PWR model.
- Fuel rod: radial heat conduction through fuel, gap, and cladding for the
  average pin of each node. Gap conductance and temperature-dependent material
  properties.
- Water and steam properties: IAPWS-IF97.
- **Primary loop (beyond PARCS and SIMULATE-5):** both codes model only the
  core region and take inlet conditions as boundary conditions. The two TH
  knobs require a closed primary loop: lower/upper plenum, hot leg, steam
  generator primary side with an imposed heat removal, cold leg, and pump.
  Core inlet temperature and flow then come from this loop model.

### 2.3 Coupling

- Neutronics supplies power density per node to TH. TH supplies fuel
  temperature, moderator temperature/density, and boron per node back to
  neutronics.
- Fixed-point (Picard) iteration between the two to convergence, at steady
  state and in every transient time step.

## 3. Student-facing knobs

The full physics runs internally. Only four controls are exposed to students.
Everything else comes from the input file and is not a student control.

| Domain | Knob | Primary physical path |
|---|---|---|
| Neutronics | Control rod insertion / withdrawal | Per-node rodded fraction → cross sections |
| Neutronics | Soluble poison (boron) concentration | Boron → cross sections; transported with the coolant |
| Thermal-hydraulics | Primary coolant flow rate | Pump → loop flow → channel flow, heat transfer, moderator temperature |
| Thermal-hydraulics | Heat load on the primary-to-secondary heat exchanger | SG heat removal → cold-leg temperature → core inlet temperature |

Each knob can be a constant or a time schedule in the input file, so one run
can model a transient (rod withdrawal, boration, pump coastdown, load change).

## 4. Nodal data generation

- Nodal cross sections and kinetics parameters are generated with **OpenMC**.
- Third-party repositories live as git submodules under `external/`. The first
  is `external/BEAVRS_minimal` (OpenMC BEAVRS model builder plus the current
  nodal-data sweep scripts).
- The data library is an HDF5 file indexed by branch state. The engine reads
  any library that follows the documented layout. Nothing about BEAVRS is
  compiled in.

### 4.1 Current library (`beavrs_kinetics_library.hdf5`) vs. required

| Aspect | Present | Needed |
|---|---|---|
| Groups | 2 | 2 (generalizable) |
| Spatial mesh | 15×15×17 full core, 1 node per assembly radially, ~21.5 cm axially | Plus axial reflector nodes and radial reflector treatment |
| Branch axes | T_fuel (6) × moderator state (6) × rod state (2, all-in / all-out) | Add a boron axis; rod state per node rather than core-wide |
| Kinetics | β, λ for 8 delayed groups, 1/v | Same |
| Equivalence data | none | Discontinuity factors |
| Pin power | none | Form functions for reconstruction |
| Xe/Sm | none | Microscopic Xe-135/Sm-149 absorption and fission yields |
| Decay heat | none | Decay-heat group data |

## 5. Generality

BEAVRS is the reference reactor for development and validation, not a built-in
assumption. Any reactor that has a matching input file and nodal data library
must run without code changes. This means core size and layout, assembly
pitch, axial zoning, group count, delayed-group count, branch axes, and loop
parameters all come from input.

## 6. Validation

- Compare against PARCS and SIMULATE-5 results for BEAVRS as each capability
  lands: eigenvalue, critical boron, rod worths, radial/axial power
  distributions, and transient responses.
- Where applicable, compare against BEAVRS measured plant data (hot zero power
  physics tests, cycle 1 boron letdown, flux maps).

## 7. Development stages

1. **Core engine:** HDF5 input/output, steady-state nodal neutronics, cross-section interpolation.
2. **Coupling:** core TH, fuel conduction, steady-state coupled solve, critical boron.
3. **Kinetics:** transient neutronics, Xe/Sm, decay heat, coupled transients.
4. **Primary loop:** loop model driving the two TH knobs; time-scheduled knobs.
5. **Fidelity:** discontinuity factors, rod cusping, pin power reconstruction, cross flow.
6. **Polishing:** Python package, documentation, student-facing examples.

## 8. Open questions

- Fuel depletion and multi-cycle analysis (both in PARCS and SIMULATE-5): in
  scope or not?
- Hexagonal geometry for non-PWR/VVER generality, or Cartesian only?
- SP3 transport option (both reference codes have it), or diffusion only?
- C++ standard and build system (proposal: C++20, CMake, HDF5 C++ API).
