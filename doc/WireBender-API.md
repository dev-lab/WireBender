# WireBender WASM — API Reference

WireBender is a schematic routing library compiled to WebAssembly. It takes a
netlist (components and nets), computes an initial automatic placement, and
routes orthogonal wires with correct junction dots and crossing minimisation.
It also visualises PCB pad connections as optimized multi-point networks.

---

## Loading the module

```js
import WireBenderModule from './WireBender.js';

const Module = await WireBenderModule({
  locateFile: f => f === 'WireBender.wasm' ? './WireBender.wasm' : f
});
```

All classes and types are accessed via `Module.*`.

---

## Data Structures and Memory

In the updated API, most data structures (like arrays of pins, nets, wires, and route results) automatically map to **standard JavaScript arrays and objects**. You no longer need to use Emscripten vectors for these standard types.

However, certain top-level classes (`WireBender`, `PcbVisualizer`, `ComponentPlacements`, `PinMap`) cross the WASM boundary as stateful C++ objects. You must manually manage their memory by calling `.delete()` when they are no longer needed.

---

## Coordinate system

- **Component-local coordinates:** `(0, 0)` is the center of the component's
  bounding box. Pin positions are in these coordinates.
- **World coordinates:** after `computePlacements()`, components are assigned
  absolute center positions and transforms. Pin world position = `compPos + (pin.localPos * transform)`.
- **Padding:** each component has a `padding` value (default `16`) that adds
  routing clearance around its bounding box. Pins are expressed in local
  coordinates *before* padding; the library adds padding internally.

---

## Pin numbering

Pins are identified by **integer `number`**, not by name. Numbers must be
unique within a component and must not change. The optional `name` field is
for display only. Use pin numbers in all `PinRef` references and `PinMap`
mappings.

---

## Types

### `Point2D`
```js
{ x: number, y: number }
```

### `Transform`
```js
{ rotation: number, flipX: boolean }
```

### `Placement`
```js
{ position: Point2D, transform: Transform }
```

### `PinDirection` (enum)
```js
Module.PinDirection.DirNone   // 0  — no preference
Module.PinDirection.DirUp     // 1
Module.PinDirection.DirDown   // 2
Module.PinDirection.DirLeft   // 4
Module.PinDirection.DirRight  // 8
Module.PinDirection.DirAll    // 15 — default, router chooses
```
Values are bit flags; combine with `|` for multiple directions.

### `PinDescriptor`
```js
{
  number:         number,   // integer pin identity, unique within component
  name:           string,   // optional display label; may be ""
  x:              number,   // local x (0 = center of component box)
  y:              number,   // local y (0 = center of component box)
  directionFlags: number    // PinDirection flags
}
```

### `ComponentDescriptor`
```js
{
  id:      string,                 // unique identifier e.g. "U1", "R3"
  width:   number,
  height:  number,
  padding: number,                 // routing clearance, default 16
  pins:    PinDescriptor[]         // standard JS array
}
```

### `PinRef`
```js
{ componentId: string, pinNumber: number }
```

### `NetDescriptor`
```js
{
  name: string,
  pins: PinRef[]                   // standard JS array
}
```

### `NetClassification`
```js
{
  name:       string,
  isBus:      boolean,   // true → drawn as horizontal rail
  isGround:   boolean,   // true → rail drawn below components
  isPositive: boolean,   // true → rail drawn above components
  busLevel:   number     // ordering among bus rails; 0 = topmost; -1 for signals
}
```

### `ComponentPlacements` (class with methods)
Container for assigning and retrieving layout data. Call `.delete()` when done.
```js
const cp = new Module.ComponentPlacements();
cp.set(id: string, placement: Placement);
cp.get(id: string) → Placement;
cp.toObject() → Object<string, Placement>;   // Converts to plain JS object
cp.fromObject(obj: Object<string, Placement>);
cp.delete();
```

### `Wire`
```js
{ net: string, points: Point2D[] }
```
`points` is an ordered polyline. Each consecutive pair of points is one wire
segment. All points are guaranteed to be orthogonal (horizontal or vertical
segments only). Points at junctions are snapped to the junction centre.

### `JunctionDot`
```js
{ net: string, position: Point2D }
```
Marks a T-junction where three or more wires of the same net meet.

### `NetLabelHint`
```js
{
  net:        string,
  position:   Point2D,  // Near the midpoint of the longest wire segment
  isVertical: boolean   // Is wire segment vertical at this point?
}
```
Suggested anchor for a net label. Label text is the net name.

### `ComponentLabelHint`
```js
{
  componentId:     string,
  refPosition:     Point2D,   // Centre anchor for the reference designator.
  refIsVertical:   boolean,   // Show ref label vertically.
  valuePosition:   Point2D,   // Centre anchor for the value / part-number label.
  valueIsVertical: boolean    // Show value label vertically.
}
```
Suggested text anchor positions for the two labels drawn beside a schematic
symbol: the reference designator (e.g. "R135") and the value (e.g. "68k").

The library picks the first candidate position (above / below / right / left
of the component bounding box) that does not overlap any wire segment or
other component, so the caller can render the labels at the given coordinates
without a separate collision pass. Both positions are world-space centre anchors for the text.

### `SchematicRouteResult`
```js
{
  wires:           Wire[],
  junctions:       JunctionDot[],
  netLabels:       NetLabelHint[],
  componentLabels: ComponentLabelHint[]
}
```

### `IncrementalRouteResult`
```js
{
  affectedNets: string[],              // names of re-routed nets
  routes:       SchematicRouteResult   // wires/junctions for those nets only
}
```
Partial routing result from `moveComponent()`. Contains only the nets connected to the moved component.
`routes.componentLabels` contains exactly one entry — the updated hint for
the moved component. Merge it into the full result by replacing the entry
whose `componentId` matches.

### `ValidationResult`
```js
{
  hasSegmentOverlap:      boolean,
  hasFixedSegmentOverlap: boolean,
  hasInvalidPaths:        boolean,
  crossingCount:          number
}
```

### `PinMap` (class with methods)
```js
const pm = new Module.PinMap();
pm.set(oldPinNumber: number, newPinNumber: number);
pm.get(oldPinNumber: number) → number;   // returns -1 if not mapped
pm.toObject() → Object<number, number>;  // Converts to plain JS object
pm.fromObject(obj: Object<number, number>);
pm.delete();
```
Used in component replacement. Pin numbers absent from the map are treated
as removed. New pin numbers not appearing as values must be wired up via
`addNet()` separately.

### `ComponentReplacement`
```js
{
  componentId:   string,
  newDescriptor: ComponentDescriptor,
  pinMapping:    PinMap
}
```

### `PcbNet`
```js
{ name: string, pads: Point2D[] }
```

### `PcbRouteResult`
```js
{ wires: Wire[], junctions: JunctionDot[] }
```

---

## `WireBender` class

The main class for schematic routing. One instance owns all routing state and
must be kept alive across the full session (create on load, destroy on unload).

```js
const wb = new Module.WireBender();
// ... use ...
wb.delete();   // free C++ memory when done
```

### Step 1 — Describe the netlist

#### `wb.addComponent(descriptor)`
Add or replace a component. Pin positions are in component-local coordinates,
with (0,0) as the center.

```js
wb.addComponent({
  id: 'U1', width: 80, height: 60, padding: 16,
  pins:[
    { number: 1, name: 'VCC', x: -20, y: -30, directionFlags: Module.PinDirection.DirUp },
    { number: 2, name: 'GND', x:  20, y:  30, directionFlags: Module.PinDirection.DirDown },
    { number: 3, name: 'IN',  x: -40, y:   0, directionFlags: Module.PinDirection.DirLeft },
    { number: 4, name: 'OUT', x:  40, y:   0, directionFlags: Module.PinDirection.DirRight }
  ]
});
```

#### `wb.addNet(descriptor)`
Add or replace a net. Pins are referenced by `(componentId, pinNumber)`.

```js
wb.addNet({
  name: 'VCC',
  pins:[
    { componentId: 'U1', pinNumber: 1 },
    { componentId: 'U2', pinNumber: 1 },
    { componentId: 'U3', pinNumber: 1 }
  ]
});
```

#### `wb.clear()`
Remove all components and nets, reset to empty state.

---

### Step 2 — Classify nets

The library auto-detects power buses (VCC, GND, etc.) using a statistical
outlier heuristic on pin counts. Review and optionally override before routing.

#### `wb.classify()` → `NetClassification[]`

```js
const cls = wb.classify();
for (const c of cls) {
  console.log(c.name, c.isBus ? 'bus' : 'signal', 'level:', c.busLevel);
}
```

#### `wb.applyClassification(cls)`
Return the (possibly modified) classification array. Must be called before
`computePlacements()`.

```js
// Example: force RST to be treated as a signal, not a bus
for (const c of cls) {
  if (c.name === 'RST') c.isBus = false;
}
wb.applyClassification(cls);
```

**Bus rail layout:**
- Positive rails (VCC, VDD, …) are drawn as a horizontal wire **above** all
  components.
- Ground/negative rails (GND, VSS, …) are drawn **below** all components.
- Multiple bus rails are stacked vertically in `busLevel` order (0 = topmost).

---

### Step 3 — Place components

#### `wb.computePlacements()` → `ComponentPlacements`
Computes an automatic placement. Components connected by more nets are pulled
closer together. Returns suggested center world positions and transforms.

```js
const placements = wb.computePlacements();
const positions = placements.toObject(); // JS Object<string, Placement>

for (const [id, placement] of Object.entries(positions)) {
  console.log(`Component ${id} is at ${placement.position.x}, ${placement.position.y}`);
}
placements.delete();
```

#### `wb.setLockedPlacements(locks)`

Lock specific components at fixed placements before calling `computePlacements()`.
Locked components are held immovable by the placement solver; all others are
placed around them. Call with an empty container to clear all locks.

```js
const locks = new Module.ComponentPlacements();
locks.set('U3', { position: { x: 100, y: 100 }, transform: { rotation: 0, flipX: false } });
locks.set('U5', { position: { x: 300, y: 200 }, transform: { rotation: 90, flipX: false } });
wb.setLockedPlacements(locks);
locks.delete();

wb.computePlacements();  // U3 and U5 stay fixed, others are re-placed

// Clear locks so the next computePlacements() is unconstrained
const empty = new Module.ComponentPlacements();
wb.setLockedPlacements(empty);
empty.delete();
```

Locks apply **only to `computePlacements()`** — they have no effect on
`routeAll()` or `moveComponent()`.

**Typical use case — iterative netlist development:**
1. First iteration: call `computePlacements()` with no locks — library places all components.
2. User manually drags components to preferred positions.
3. New components are discovered and added via `addComponent()` / `addNet()`.
4. Lock all existing components at their current positions, then call
   `computePlacements()` again — only the new components are placed, everything
   else stays where the user put it.

---

### Step 4 — Route all nets

#### `wb.routeAll()` → `SchematicRouteResult`
Routes all nets using the current component placements and classification.
Must be called after `computePlacements()`.

```js
const result = wb.routeAll();

// Draw wires
for (const wire of result.wires) {
  ctx.beginPath();
  wire.points.forEach((p, index) => {
    index === 0 ? ctx.moveTo(p.x, p.y) : ctx.lineTo(p.x, p.y);
  });
  ctx.stroke();
}

// Draw junction dots
for (const d of result.junctions) {
  ctx.arc(d.position.x, d.position.y, 4, 0, Math.PI * 2);
  ctx.fill();
}

// Draw net labels (optional)
for (const l of result.netLabels) {
  ctx.fillText(l.net, l.position.x, l.position.y);
}
```

---

### Step 5 — Interactive component dragging

The recommended pattern: move the component visually on every `mousemove`
(instant), then call `wb.moveComponent()` once on `mouseup` to get correctly
routed wires at the dropped position.

`wb.moveComponent()` recalculates the routing, which takes ~200ms in WASM at
`-O3`. This is a property of the global nudging algorithm and cannot be made
faster without sacrificing wire separation.

#### `wb.moveComponent(componentId, placement)` → `IncrementalRouteResult`

```js
// On mouseup / drop:
const delta = wb.moveComponent('U1', { 
  position: { x: newX, y: newY }, 
  transform: { rotation: 0, flipX: false } 
});

// Which nets changed?
const affected = delta.affectedNets; // e.g. ["VCC", "SIG"]

// Updated wires for the affected nets only
for (const wire of delta.routes.wires) {
  // replace this net's wires in your render state
}
```

`IncrementalRouteResult.routes` contains only the wires and junctions for the
nets connected to the moved component. Merge these into your existing full
route result; all other nets are unchanged.

**Drag pattern example:**
```js
let dragging = null;

canvas.addEventListener('mousedown', e => {
  const id = hitTest(e);
  if (!id) return;
  dragging = { id, startMouse: mouse(e), startPos: positions[id].position };
});

canvas.addEventListener('mousemove', e => {
  if (!dragging) return;
  const dm = delta(mouse(e), dragging.startMouse);
  positions[dragging.id].position = {
    x: dragging.startPos.x + dm.x,
    y: dragging.startPos.y + dm.y,
  };
  draw();   // instant — just redraws the box at new position
});

canvas.addEventListener('mouseup', e => {
  if (!dragging) return;
  const placement = positions[dragging.id];
  const id = dragging.id;
  dragging = null;
  
  // Route at settled position
  const delta = wb.moveComponent(id, placement);
  applyIncrementalResult(delta);
  draw();
});
```

---

### Step 6 — Component replacement

Used when a schematic component is resolved from a component library (e.g.
KiCad) and its pin geometry changes.

#### `wb.replaceComponent(replacement)` → `SchematicRouteResult`

1. Create a `PinMap` mapping old pin numbers to new ones.
2. Nets connected to pins absent from the map are disconnected.
3. Nets connected to entirely new pins must be added via `addNet()` first.
4. The full schematic is re-routed and the complete new result is returned.

```js
// Old component: 3-pin rectangle with pins 1, 2, 3
// New component: NPN transistor — KiCad numbering maps differently
// Old pin 1 (Base)     → new pin 2
// Old pin 2 (Collector) → new pin 3
// Old pin 3 (Emitter)  → new pin 1

const pinMap = new Module.PinMap();
pinMap.set(1, 2);
pinMap.set(2, 3);
pinMap.set(3, 1);

const result = wb.replaceComponent({
  componentId: 'Q1',
  newDescriptor: {
    id: 'Q1', width: 80, height: 60, padding: 16,
    pins:[
      { number: 1, name: 'E', x:   0, y:  30, directionFlags: Module.PinDirection.DirDown },
      { number: 2, name: 'B', x: -40, y:   0, directionFlags: Module.PinDirection.DirLeft },
      { number: 3, name: 'C', x:   0, y: -30, directionFlags: Module.PinDirection.DirUp }
    ]
  },
  pinMapping: pinMap,
});

pinMap.delete();
// result is a full SchematicRouteResult — re-render everything
```

---

### Validation

#### `wb.validate()` → `ValidationResult`

Only meaningful after `routeAll()` or `moveComponent()`. Returns diagnostic
counts. `crossingCount > 0` means wires from different nets cross; the router
minimises crossings but cannot always eliminate them for complex netlists.

```js
const v = wb.validate();
if (v.hasSegmentOverlap)  console.warn('wire overlap detected');
if (v.crossingCount > 0)  console.warn(`${v.crossingCount} crossings`);
```

---

## `PcbVisualizer` class

Visualises electrical connections between pads on a PCB photo. Routes each net
as an optimized multi-point connection (non-orthogonal, no component obstacles).
Different nets are routed with crossing minimisation between them.

```js
const pcb = new Module.PcbVisualizer();
```

#### `pcb.addNet(net)`

```js
pcb.addNet({
  name: 'VCC',
  pads:[
    { x: 120, y: 340 },
    { x: 450, y: 280 },
    { x: 230, y: 510 }
  ]
});
```

#### `pcb.route()` → `PcbRouteResult`

```js
const result = pcb.route();

for (const wire of result.wires) {
  // Draw the wire polyline for wire.net
}
for (const d of result.junctions) {
  // Draw junction dot at d.position
}
```

#### `pcb.clear()`
Remove all nets and reset.

---

## Complete schematic example

```js
import WireBenderModule from './WireBender.js';
const M = await WireBenderModule({ locateFile: f => f });

const wb = new M.WireBender();

// ── 1. Components ────────────────────────────────────────────────────────────

function addComp(id, w, h, pinDefs) {
  wb.addComponent({ id, width: w, height: h, padding: 16, pins: pinDefs });
}

addComp('U1', 80, 60,[
  { number: 1, name: 'VCC', x: -20, y: -30, directionFlags: M.PinDirection.DirUp },
  { number: 2, name: 'GND', x:  20, y:  30, directionFlags: M.PinDirection.DirDown },
  { number: 3, name: 'OUT', x:  40, y:   0, directionFlags: M.PinDirection.DirRight },
]);
addComp('U2', 80, 60,[
  { number: 1, name: 'VCC', x: -20, y: -30, directionFlags: M.PinDirection.DirUp },
  { number: 2, name: 'GND', x:  20, y:  30, directionFlags: M.PinDirection.DirDown },
  { number: 3, name: 'IN',  x: -40, y:   0, directionFlags: M.PinDirection.DirLeft },
]);
addComp('R1', 25, 55,[
  { number: 1, name: 'A', x: -0.5, y: -27.5, directionFlags: M.PinDirection.DirUp },
  { number: 2, name: 'B', x: -0.5, y:  27.5, directionFlags: M.PinDirection.DirDown },
]);

// ── 2. Nets ──────────────────────────────────────────────────────────────────

function addNet(name, refs) {
  wb.addNet({ name, pins: refs });
}

addNet('VCC',[{ componentId:'U1', pinNumber:1 }, { componentId:'U2', pinNumber:1 }]);
addNet('GND',[{ componentId:'U1', pinNumber:2 }, { componentId:'U2', pinNumber:2 }]);
addNet('SIG',[{ componentId:'U1', pinNumber:3 }, { componentId:'R1', pinNumber:1 },
               { componentId:'U2', pinNumber:3 }]);

// ── 3. Classify ──────────────────────────────────────────────────────────────

const cls = wb.classify();
wb.applyClassification(cls);

// ── 4. Place ─────────────────────────────────────────────────────────────────

const placements = wb.computePlacements();
const pos = placements.toObject();
placements.delete();

// ── 5. Route ─────────────────────────────────────────────────────────────────

const routes = wb.routeAll();

// render routes.wires, routes.junctions, routes.netLabels, routes.componentLabels …

// ── 6. Move on drop ──────────────────────────────────────────────────────────

function onDrop(compId, newX, newY) {
  const delta = wb.moveComponent(compId, { 
    position: { x: newX, y: newY }, 
    transform: { rotation: 0, flipX: false } 
  });
  // update render state for delta.affectedNets only
}
```

---

## Memory management

| Object | When to `.delete()` |
|---|---|
| `ComponentPlacements` | After you are done passing/retrieving placements to/from WASM |
| `PinMap` | After passing to `replaceComponent()` |
| `WireBender` instance | When the schematic session ends |
| `PcbVisualizer` instance | When done visualising |

`SchematicRouteResult`, `IncrementalRouteResult`, `PcbRouteResult`, and other returned data items (like standard arrays) are JS-owned standard objects — **do not call** `.delete()` on them.

---

## Build configuration

| CMake option | Effect |
|---|---|
| `cmake ..` | Native static library (`libWireBender.a`) |
| `emcmake cmake ..` | WASM (`WireBender.js` + `WireBender.wasm`) |
| `-DWASM_DEBUG=ON` | Unoptimised WASM with debug info (`-O0 -g3`) |
| `-DWASM_DEBUG=OFF` | Optimised release WASM (`-O3`) — default |
| `-DASAN=ON` | AddressSanitizer (native build only) |
| `-DADAPTAGRAMS_DIR=…` | Path to adaptagrams checkout |

Always use `WASM_DEBUG=OFF` (the default) for production. `-O3` vs `-g3` is
roughly a 100× difference in routing speed.
