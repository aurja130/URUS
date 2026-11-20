# Definitions

The glossary of the concept map. It links to [CONCEPTS](CONCEPTS.md), [CODE](CODE.md),
[TESTS](TESTS.md) and [BENCHMARKS](BENCHMARKS.md). "TM §n" is a section of
[the theory manual](../THEORY_MANUAL.md), where every equation behind a term is stated and
cited; "Vision §n" is a section of [the vision](../VISION.md). D1–D5 are the departures and
O1–O14 the decisions recorded in TM §20.2 and §21. The notation of the equations is TM §2.
"Project plan §n" is a section of [the project plan](../PROJECT_PLAN.md).

## Neutronics

| Term | Meaning | Decision |
| --- | --- | --- |
| node | A box of the 3D Cartesian mesh with one composition and one average flux per energy group; radially one assembly or a part of one, axially the layers shared with the coolant channels. | Vision §2.1; TM §3.2 |
| energy group, group structure | The engine has exactly two energy groups (fast and thermal, every fission neutron born fast) and eight delayed-neutron groups. Any other structure is rejected. | O2, 2026-11-16; Vision §2.1, §5; TM §0.1 |
| two-group nodal diffusion | Neutron diffusion in two energy groups, solved per node with node-average fluxes and face currents. | TM §3 |
| delayed-neutron precursor | A fission product that emits a neutron after a delay; eight groups with fixed half-lives from 55.6 s to 0.195 s, abundances from the library. | TM §3.1, §7.5 |
| effective delayed-neutron fraction | The delayed fraction weighted by neutron importance; about 3 % smaller than the physical fraction for a PWR. The single-assembly library tallies it (decided 2026-11-19); the present library holds the physical fraction. (2026-11-19; before: "The model gives the physical fraction, about 3 % larger for a PWR. **Open:** whether to tally the effective fractions into the library is deferred to the data generation plan.") | O5; TM §9.3 |
| eigenvalue | The multiplication factor k_eff of the steady state and its fundamental-mode flux. | TM §4.4 |
| Wielandt shift | A shift of the eigenvalue problem that speeds up the power iteration. | TM §4.4 |
| CMFD | Coarse-mesh finite difference: the global problem in node-average fluxes, with face currents from finite differences plus a correction. | TM §4 |
| outer iteration | One update of the fission source and the eigenvalue in the shifted power iteration. | TM §4.4 |
| inner iteration | The iterative linear solve inside an outer iteration: BiCGSTAB with a preconditioner (incomplete LU first; PARCS's BILU3D is a later performance item), stopped when its residual has fallen by a set factor. | TM §4.5 |
| boundary condition | The relation at an outer face of the domain between outward current and surface flux, given as an albedo: reflective (0), vacuum, also called Marshak or zero incoming current (1/2), zero flux (10³⁰), or an input value per group. | TM §4.2 |
| effective down-scatter | The fast-to-thermal transfer cross section with the small thermal-to-fast transfer folded in at the generating flux, so that the data carry no up-scatter. | TM §3.1, Eq. (3.5) |
| coupling correction | The term added to the finite-difference face current so that it equals the accurate current from a two-node problem (PARCS's D-hat). Never clipped. | TM §4.1, §6.2 |
| nodal kernel | A method that solves the one-dimensional problem inside a node to higher accuracy than finite differences. | TM §5 |
| analytic nodal method | The kernel that solves the transverse-integrated two-group equation exactly with exponential and trigonometric functions (ANM). | TM §5.5 |
| nodal expansion method | The kernel that expands the flux in fourth-order polynomials and determines the coefficients by weighted residuals (NEM). | TM §5.4 |
| node response | The common form both kernels deliver: the relation between a node's face fluxes, currents and average flux. | TM §5.3 |
| kernel switch | The rule that uses the expansion method where the analytic kernel would divide by a near-zero quantity; its threshold is an input, default 0.01. | O8; TM §5.8 |
| transverse leakage | Leakage in the two directions a transverse-integrated problem leaves out, approximated by a quadratic through three neighbouring nodes. | TM §5.2; O7 |
| discontinuity factor | The ratio of heterogeneous to homogeneous surface flux of an assembly; lets the homogeneous flux jump at interfaces. Equal to one until the library carries them. | TM §5.6, §5.9 |
| nodal update | One recomputation of the coupling corrections from the current fluxes. | TM §6.1, §11.7 |
| cross section | A macroscopic reaction rate per unit flux of a node and group, interpolated at the node's state. | TM §7 |
| multilinear interpolation | Interpolation that is linear in each table axis at once, keeping cross terms between axes; reproduces every table point. | D2, 2026-11-16; TM §7.2 |
| table clamp | A node state outside the library table, set to the nearest edge and counted per node, state axis and side (below the first point or above the last). A clamped converged state is not a valid result. An initial node state is clamped only when the settings allow table clamps; otherwise it is an input error (library coverage). Changed 2026-11-20; was "A node state outside the library table, set to the nearest edge and counted per node, state axis and side (below the first point or above the last). A clamped converged state is not a valid result." | TM §7.2; Milestone 1 plan §3; Problem input contract §6–§7, 2026-11-20 |
| critical boron | The uniform boron concentration that makes the core exactly critical, found by a search. | TM §6.3 |
| boron | Soluble boron in the coolant, in ppm by mass. | TM §0.5, §7.1 |
| rod bank | A group of control rods moving together, its position given in steps; each rod is a stack of absorber zones. | TM §8.1 |
| rodded fraction | The fraction of a node's height occupied by an absorber zone. | TM §8.2 |
| flux weight | The share of a partly rodded node's reaction rates in its rodded part, from the axial flux shape. | TM §8.2 |
| rod cusping | The artificial bump in reactivity against tip position that comes from volume-weighting a partly rodded node. | TM §8.3 |
| three-node problem | The axial problem across a partly rodded node and its two neighbours that removes rod cusping. Fine-mesh finite differences first, at 40 meshes per node by default; the kernel form is adopted only after it passes its two tests. | D5 conditional, 2026-11-16; TM §8.3 |
| adjoint flux | The solution of the transposed steady problem; the importance of a neutron at a place and energy. | TM §9.1 |
| reactivity | The departure from criticality, (k − 1)/k statically; dynamically the adjoint-weighted net production, split by cause. | TM §9.2 |
| point kinetics | The single-node model of reactor kinetics with adjoint-weighted parameters; used here for edits and checks. | TM §9.3 |
| xenon | Xenon-135 and its parent iodine-135, tracked per node. | TM §10; D3 |
| samarium | Samarium-149 and its parent promethium-149, tracked per node. | TM §10; D3 |
| theta method | Time differencing that weights the start and the end of a step by 1 − θ and θ. | TM §11.1, §11.8 |
| exponential transform | Writing the flux as an exponential in time times a slowly varying part, so that differencing errors shrink. | TM §11.2 |
| inverse period | The growth rate of the exponential transform, per node and group. | TM §11.2 |
| transient fixed-source problem | The coarse-mesh problem a time step reduces to; it has a source instead of an eigenvalue. | TM §11.4 |
| decay heat | Heat released by fission products after the fissions that made them. | TM §12 |
| decay-heat group | One of the exponential groups that carry the decay heat; integrated with the power linear over the step. | D4, 2026-11-16; TM §12 |
| pin power | The power of one fuel pin, from the intranodal flux shape times the form function. | TM §13 |
| form function | The ratio of pin to assembly-average power from the lattice calculation. | TM §13.1 |

## Thermal-hydraulics and loop

| Term | Meaning | Decision |
| --- | --- | --- |
| fuel rod | The average pin of a node: fuel pellet, gas gap and cladding, with radial conduction only. | TM §14.1 |
| gap conductance | The heat transfer coefficient across the pellet–cladding gap. | TM §14.8 |
| Doppler temperature | The effective fuel temperature the cross sections are interpolated at, a weighted mix of centreline and surface temperatures; the weight is set with the lattice calculations. | O9; TM §14.6 |
| moderator density | The density of the coolant in a node, the moderator axis of the library table. | TM §7.1, §7.3 |
| coolant channel | One flow path per assembly with the axial layers of the neutronic nodes, solving mass, energy and momentum. | Vision §2.2; TM §15.1 |
| drift-flux model | The two-phase model in which vapour and liquid share one momentum equation and the vapour moves with a drift velocity. No slip until a correlation valid at PWR pressure is chosen. **Open:** the correlation is chosen when fidelity work starts. | O13; TM §15.3, §16.4 |
| enthalpy | Specific enthalpy of the coolant, the energy variable of channels and loop. | TM §15.2 |
| flow distribution | The division of the core flow among the channels so that all have the same pressure drop. | TM §15.8 |
| pressure drop | The pressure difference along a channel or a loop component from friction, form losses, gravity and acceleration. | TM §15.2, §16.2 |
| form loss | The pressure loss of a local obstacle such as a spacer grid or a nozzle. Values for BEAVRS come from a generic referenced data set. | O12; TM §16.2 |
| cross flow | Lateral flow between neighbouring assemblies; optional. | TM §15.9 |
| IAPWS-IF97 | The industrial formulation of water and steam properties the engine uses. | Vision §2.2; TM §16.1 |
| closure correlation | An empirical relation that closes the balance equations: heat transfer, friction, drift-flux parameters, critical heat flux. | TM §16 |
| heat transfer coefficient | The ratio of wall heat flux to the difference between wall and coolant temperature. | TM §16.3 |
| DNB ratio | The ratio of the critical heat flux to the actual heat flux, reported as a margin and never fed back. **Open:** the critical heat flux correlation is chosen when fidelity work starts. | O13; TM §16.5 |
| boron transport | Boron carried with the coolant through channels and loop, so that a change reaches each place after its transit time. | O6; TM §15.7, §17.7 |
| primary loop | The coolant path outside the core in one equivalent loop: plena, hot leg, steam generator, pump, cold leg. Not in PARCS or SIMULATE-5. | Vision §2.2; TM §17 |
| plenum | A mixing volume below or above the core. | TM §17.1 |
| hot leg | The pipe from the upper plenum to the steam generator. | TM §17.1 |
| cold leg | The pipe from the pump to the lower plenum. | TM §17.1 |
| transport delay | The time coolant takes to travel through a pipe, which delays every change it carries. | TM §17.2 |
| steam generator | The heat exchanger to the secondary side; only its primary side is modelled. | TM §17.5 |
| heat load | The total heat the steam generator removes from the primary coolant, imposed as a knob and shared among the tube cells by conductance. | Vision §3; O14; TM §17.5 |
| secondary temperature | The single secondary-side temperature solved so that the tube cells remove exactly the heat load. | TM §17.5 |
| pump | The reactor coolant pump of the equivalent loop. | TM §17.4 |
| homologous curve | Pump head and torque as functions of speed and flow ratios, in two branches covering forward and reversed flow. | TM §17.4 |
| pump speed | The rotational speed of the pump, the meaning of the flow knob; imposed flow comes first. | O3; TM §17.4 |
| loop closure | The secant iteration on the core inlet enthalpy that makes loop and core agree inside each coupled pass. | TM §17.8 |
| system pressure | The primary pressure, imposed; no pressurizer model. | O4; TM §17.3 |

## Coupling and results

| Term | Meaning | Decision |
| --- | --- | --- |
| Picard iteration | Fixed-point iteration between neutronics and thermal-hydraulics, converged at steady state and in every time step. | D1, 2026-11-16; Vision §2.3; TM §18 |
| field exchange | The mapping of fields between solvers: power to fuel and channels; Doppler temperature, moderator density and boron to the cross sections. | TM §18.1 |
| node power | The fission power of a node plus its decay heat. | TM §3.3, §12.1 |
| step-mean heat | The heat delivered to fuel and coolant averaged over a step, so that generated and delivered energy balance. | TM §18.3, Eq. (18.1) |
| time step | One interval of a transient, from its start to its end. | TM §11, §18.3 |
| time-step control | The choice of each step: schedule discontinuities, input limits, optional halving on an error estimate. | TM §18.5 |
| tolerance | A convergence limit on a change or residual, given in the solver options. | TM §4.6, §11.5, §18.3 |
| convergence state | Iteration counts, final changes and residuals with their tolerances; every reported result carries it. | TM §18.4 |
| verification tolerances | Iteration tolerances far tighter than the production ones (PARCS's defaults), used in benchmarks so that iteration error never passes for agreement. | Milestone 1 plan §2.1 |
| run record | What a result is stamped with: convergence state, table clamp counts, methods used, the input. | TM §18.4 |
| balance check | A neutron, energy or mass balance evaluated and reported with its residual. | TM §17.9, §18.3 |
| solution field | A quantity defined on nodes or pins at one time. | Vision §1 |
| time history | A quantity sampled over a transient. | Vision §1 |

## Input, knobs and data

| Term | Meaning | Decision |
| --- | --- | --- |
| problem input | The HDF5 file a run is started from: it holds the four input documents, each written in it or in a document file it links to. Changed 2026-11-20; was "The single HDF5 file that defines a run." | Vision §1; Problem input contract §2, 2026-11-20 |
| reactor description | Geometry, composition map, boundary conditions, rod banks, channels and loop of the reactor; nothing about BEAVRS is built into the engine. Changed 2026-11-19; was "Geometry, composition map, rod banks, channels and loop of the reactor; nothing about BEAVRS is built into the engine." | Vision §5; Problem input contract §3–§4, 2026-11-19 |
| composition map | The node-by-node assignment of compositions. A position may be excluded, outside the domain, so a core need not be rectangular. | Vision §5; Milestone 1 plan §3 |
| excluded position | A position of the composition map that lies outside the domain: it has no node, no node state and no data. | Problem input contract §3, 2026-11-19 |
| bounding face | One of the six faces of the box the composition map fills; a node face lying on it takes its boundary condition. | Problem input contract §4, 2026-11-19 |
| exterior face | A node face next to an excluded position; all exterior faces take one boundary condition given in the input. | Milestone 1 plan §3; Problem input contract §4, 2026-11-19 |
| input composition | One entry of the problem input's composition list, naming a composition set of the library and a composition in it; the composition map points into this list. Several entries may name the same library composition. | Problem input contract §5, 2026-11-19 |
| node state | The Doppler temperature, moderator density, moderator temperature and boron of one node, at which its cross sections are interpolated; given in the input until the thermal-hydraulics computes it. | TM §7.1; Problem input contract §6, 2026-11-19 |
| operating state | The power, pressure, inlet conditions, boron and rod positions a run starts from; without thermal-hydraulics, the power and the node state of every node. Changed 2026-11-19; was "The power, pressure, inlet conditions, boron and rod positions a run starts from." | Vision §1; Problem input contract §6, 2026-11-19 |
| solver options | Tolerances, iteration limits and method choices, given in the settings document. Changed 2026-11-20; was "Tolerances, iteration limits and method choices given in the input." | Vision §1; Problem input contract §7, 2026-11-20 |
| input validation | The checks an input must pass before anything is solved. | Vision §5 |
| problem input contract | The documented layout, units, defaults and validation rules of the problem input, versioned like the library contract. | Milestone 1 plan §3, 2026-11-19 |
| input writer | The Python program that writes a problem input from a description in code, one description per input document, as one file or as document files linked from the problem input; it does not validate, the engine does. Changed 2026-11-20; was "The Python program that writes a problem input from a description in code; it does not validate, the engine does." | Milestone 1 plan §3, 2026-11-19 and (1.3a) 2026-11-20 |
| input document | One of the four parts of a problem input, one responsibility each: the reactor document, the initial state document, the control signals document and the settings document. Each carries its kind and its own format version, and is stored either as a group of the problem input or as a document file linked from it. | Vision §1; Captain, 2026-11-20; Milestone 1 plan §3 (1.3a) |
| reactor document | The input document of the reactor as built: its reactor description and its library binding. Written once and used by every run of that reactor. | Captain, 2026-11-20; Milestone 1 plan §3 (1.3a) |
| initial state document | The input document of the operating state a run starts from, stated value by value; starting from an earlier output comes later. | Captain, 2026-11-20; Milestone 1 plan §3 (1.3a) |
| control signals document | The input document of the knob schedules applied during a run. | Captain, 2026-11-20; Milestone 1 plan §3 (1.3a) |
| settings document | The input document of how a run is computed: the solver options, whether table clamps are allowed, and later the run length and time-step controls. It does not change what is simulated. | Captain, 2026-11-20; Milestone 1 plan §3 (1.3a) |
| document file | An HDF5 file holding one input document at its root, linked from a problem input. | Milestone 1 plan §3 (1.3a), 2026-11-20 |
| external link | An HDF5 link in one file to an object in another. The engine follows only the four document links of a problem input and resolves them against the problem input's directory, never the working directory. | Milestone 1 plan §3 (1.3a), 2026-11-20 |
| library coverage | The range a composition set's table spans: from the first to the last point of each of its state axes. An initial node state outside it is rejected unless the settings allow table clamps. | Captain, 2026-11-20; Milestone 1 plan §3 (1.3a) |
| knob | One of the four student controls: rods, boron, primary flow, heat load. | Vision §3 |
| knob schedule | The value of a knob over time, a constant or a table; a jump is a discontinuity a step must land on. | Vision §3; TM §11.8 |
| nodal library | The HDF5 file of nodal data the engine reads. | Vision §4 |
| library contract | The documented layout, units and validity rules of the nodal library. | Vision §4; TM §19.3 |
| composition | A material description of a node that has its own set of library data, such as an assembly type with or without rods, or a reflector. | TM §7.4 |
| branch state | One combination of fuel temperature, moderator state, boron and rod state at which data are generated. | TM §7.1, §19.3 |
| rod state | Whether the control rods of a branch state are out or in; the present library has all banks out or all in, and its rodded and unrodded data are separate compositions. | TM §7.1; Library contract §7.2, 2026-11-18 |
| tensor grid | Branch states forming every combination of the values on each axis. | D2; TM §7.2 |
| lattice calculation | A single-assembly calculation with reflective boundaries that produces the nodal data of one assembly type at one branch state. | O1, 2026-11-16; TM §19.3 |
| sizing campaign | Lattice calculations run before the DM1 plan is approved, at the nominal state and along one- and two-dimensional scans of the branch-state axes, that give the statistics target, the compute estimate and the branch-grid spacing. They produce no library data. | Project plan §4, DM1 scope 1; Captain, 2026-11-20 |
| statistics target | The number of histories per lattice calculation that brings the tallies the library needs within their required uncertainty. | Project plan §4, DM1 scope 1 |
| shard | One of N runners that share a campaign's run list, each taking every N-th run, so that they run side by side on one machine without taking the same run. | Captain, 2026-11-20 |
| OpenMC | The Monte Carlo transport code that generates the nodal data. | Vision §4 |
| tally | An OpenMC estimate of a reaction rate or flux, with a statistical uncertainty. | TM §19.3 |
| statepoint | The output file of an OpenMC run, holding each tally's sum and sum of squares, from which its mean and standard deviation follow. | TM §19.3 |
| library writer | The program in the BEAVRS data submodule that turns OpenMC output into a nodal library under the library contract. | Project plan §3, 2026-11-17 |
| full-core reference | Full-core OpenMC solutions at library states, the reference the nodal results must reproduce. | O1, 2026-11-16; TM §19.3 |
| validity flag | The mark of a library entry that holds no physical value, such as an empty tally. | TM §19.3, item L14 |
| composition set | Compositions of a nodal library that share one table of branch states, with their own state axes. | Library contract §3, 2026-11-18 |
| state axis | One table dimension of a composition set: fuel temperature, moderator density, moderator temperature or boron, with strictly ascending points. A one-point axis is a fixed value. | Library contract §3.1, 2026-11-18; TM §7.2 |
| library entry | One composition at one branch state, the unit a validity flag marks. | Library contract §3.4, 2026-11-18 |
| entry rule | The physical bound a library quantity must meet in a valid entry, such as a positive diffusion coefficient; a broken rule makes the writer mark the entry invalid. | Library contract §3.3, 2026-11-18 |
| provenance | The record of where library data came from: the run of every entry, its statepoint checksum and eigenvalue, and the code and settings that produced it. | Library contract §6, 2026-11-18 |
| reproduction check | The library writer's comparison of what it extracts with the library it replaces: every raw value bit for bit, at the same branch state. | Library contract §7.4, 2026-11-18 |
| reflector | Non-fuel material around the core whose data come from fuel-and-reflector models. | TM §5.9, §19.3 |

## Plant states and measurements

| Term | Meaning | Decision |
| --- | --- | --- |
| hot zero power | The reactor critical at operating temperature and pressure with negligible fission power, so no fuel or coolant feedback; the state of the BEAVRS startup physics tests. | BEAVRS §3; TM §19.2 |
| hot full power | The reactor at rated power with its operating temperatures and flows. | TM §19.2 |
| power defect | The reactivity lost between hot zero power and hot full power, from the fuel and coolant temperature rise. | TM §19.2 |
| bank worth | The reactivity a rod bank removes when inserted, measured at hot zero power. | BEAVRS §3.2; TM §19.2 |
| flux map | A set of in-core detector signals measured across the core at one time. | BEAVRS §3.1–3.3; TM §13.7 |
| scram | Insertion of all rod banks at once to shut the reactor down. | TM §12 |
| depletion | The change of fuel composition with burnup over a cycle. Out of scope. | TM §0.1 |
| beginning of cycle | The fresh core of a cycle, before depletion matters; the only part of a cycle this simulator models. | TM §0.1 |

## Tooling and working words

| Term | Meaning | Decision |
| --- | --- | --- |
| toolchain pin | The recorded minimum versions of compilers and tools, and the check against the machine. | AGENTS.md |
| usage guard | The check of the model provider's cached usage that agents run at batch boundaries. | AGENTS.md, Usage limits |
| build preset | A named build configuration of the engine. | AGENTS.md |
| sanitizer | A compiler instrumentation that detects memory errors, undefined behaviour or data races at run time. | `.omp/LOCAL_ENVIRONMENT.md` |
| local CI | The one local script that runs every gate. | AGENTS.md |
| urus | The engine's executable, Uppsala universitets reaktorsimulator; it reads a problem input and writes the results. | Captain, 2026-11-17 |
| test framework | The library the engine's C++ tests are written with: GoogleTest. | Project plan §5.1, 2026-11-17 |
| license header | The three-line GPL-3.0-or-later notice every code file of ours starts with. | AGENTS.md, License |
| invariant check | A check of a condition the code itself guarantees; on in every build, it stops the program with its location and message when violated. | Milestone 1 plan §2 |
| floating-point trap | A hardware exception, switched on at program start in debug and sanitizer builds, that stops the program with SIGFPE at the operation that makes a NaN (invalid operation), divides by zero or overflows. Underflow and inexact results are not trapped. | Milestone 1 plan §2 |
| runtime checks | The invariant checks and floating-point traps together. | Coding standards §4, 2026-11-17 |
| milestone | One large step of the project plan with its own milestone plan; engine milestones 1–6 carry the numbers of the vision's stages. | Project plan §1 |
| data milestone | A milestone that produces nodal data or reference solutions with OpenMC, beside the engine milestones: DM1 and DM2. | Project plan §4 |
| sub-milestone | The part of a milestone one session delivers; its contract freezes when it starts. | AGENTS.md |
| milestone branch, sub-milestone branch | The git branch a milestone (`milestone-<n>`, from `main`) or sub-milestone (`milestone-<n>.<k>`, from its milestone branch) is built on; each merges into its parent when it closes. | Captain, 2026-11-17 |
| gate | The checks a snapshot must pass before a milestone or sub-milestone closes. | AGENTS.md; Project plan §2 |
| prerequisite | Something the Captain does so that a milestone can proceed: decide, obtain a document, approve and give machine time to OpenMC runs, or approve an assembled data set. Each is presented to her only when it is due. | Project plan §5 |
| departure | A method that differs from PARCS or SIMULATE-5 and needs the Captain's approval; D1–D5. | TM §20.2 |
| verification | Showing that the code solves its equations correctly. | TM §19.2 |
| validation | Showing that the equations describe the reactor. | TM §19.2 |
| benchmark | A reference problem with a published or computed solution that results are compared with, within a stated tolerance. | AGENTS.md; TM §19.2 |
| reference script | A committed program, sharing no code with the engine, that computes a benchmark's reference values. | Milestone 1 plan §2 |
| observed order | The rate at which an error falls under mesh or step refinement, measured from successive refinements. | Milestone 1 plan §5 |
| code-path exercise | A run that shows the code works end to end on real data and makes no accuracy claim. | Project plan §3 |
| pcm | Per cent mille, 10⁻⁵ in reactivity or relative eigenvalue. | TM §0.5 |
