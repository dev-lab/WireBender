# WireBender WASM — API Reference

WireBender is a schematic routing library compiled to WebAssembly. It takes a
netlist (components and nets), computes an initial automatic placement, and
routes orthogonal wires with correct junction dots and crossing minimisation.
It also visualises PCB pad connections as optimized multi-point networks.

---

## Loading the module

```js
import WireBenderModule from 'https://dev-lab.github.io/WireBender/latest/WireBender.js';

const Module = await WireBenderModule({
  locateFile: f => f === 'WireBender.wasm' ? 'https://dev-lab.github.io/WireBender/latest/WireBender.wasm' : f
});
```

All classes and types are accessed via `Module.*`.

---

## Data Structures and Memory

Scalar fields (`number`, `string`, `boolean`) and plain structs (`Point2D`, `Placement`, `Transform`, `PinDescriptor`, `PinRef`, etc.) marshal transparently as plain JS objects across the WASM boundary.

**Arrays do not.** Every field typed as a sequence — whether passed in or returned — is an **Emscripten vector proxy**, not a JS array. Plain JS arrays will throw a `BindingError` at runtime. The registered vector types are:

| WASM type | Element type | Used for |
|---|---|---|
| `Module.VectorPinDescriptor` | `PinDescriptor` | `ComponentDescriptor.pins` (input) |
| `Module.VectorPinRef` | `PinRef` | `NetDescriptor.pins` (input) |
| `Module.VectorPoint2D` | `Point2D` | `PcbNet.pads` (input), `Wire.points` (output) |
| `Module.VectorWire` | `Wire` | Route result `wires` (output) |
| `Module.VectorJunctionDot` | `JunctionDot` | Route result `junctions` (output) |
| `Module.VectorNetLabelHint` | `NetLabelHint` | Route result `netLabels` (output) |
| `Module.VectorComponentLabelHint` | `ComponentLabelHint` | Route result `componentLabels` (output) |
| `Module.VectorNetClassification` | `NetClassification` | `classify()` return value |
| `Module.VectorString` | `string` | `IncrementalRouteResult.affectedNets` (output) |

**Building an input vector:**
```js
const pins = new Module.VectorPinDescriptor();
pins.push_back({ number: 1, name: 'VCC', x: 0, y: -30, directionFlags: Module.PinDirection.DirUp });
pins.push_back({ number: 2, name: 'GND', x: 0, y:  30, directionFlags: Module.PinDirection.DirDown });
wb.addComponent({ id: 'U1', width: 80, height: 60, padding: 16, pins });
pins.delete();  // free C++ memory immediately after the call
```

**Reading an output vector:**
```js
const result = wb.routeAll();
for (let i = 0; i < result.wires.size(); i++) {
  const wire = result.wires.get(i);    // Wire — value_object, no delete needed
  for (let j = 0; j < wire.points.size(); j++) {
    const p = wire.points.get(j);      // Point2D — { x, y }
  }
}
```

**Memory rules for vectors:**

- **Input vectors** (`VectorPinDescriptor`, `VectorPinRef`, `VectorPoint2D` for `pads`): allocate with `new`, call `.delete()` immediately after passing to the API.
- **`VectorNetClassification`** returned by `classify()`: treat as an input vector — call `.delete()` after passing to `applyClassification()`.
- **Output vectors** embedded in `value_object` results (`SchematicRouteResult`, `IncrementalRouteResult`, `PcbRouteResult`): these are JS-side value copies; **do not call `.delete()`** on them or their fields.
- **Stateful C++ classes** (`WireBender`, `PcbVisualizer`, `ComponentPlacements`, `PinMap`): call `.delete()` when the session ends.

**Recommended helper utilities:**
```js
/** Convert a JS array to an Emscripten vector. Caller must .delete() the result. */
function toVector(VectorClass, items) {
  const v = new VectorClass();
  for (const item of items) v.push_back(item);
  return v;
}

/** Copy an Emscripten output vector to a plain JS array for easier processing. */
function fromVector(vec) {
  const arr = [];
  for (let i = 0; i < vec.size(); i++) arr.push(vec.get(i));
  return arr;
}
```

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
  id:      string,                       // unique identifier e.g. "U1", "R3"
  width:   number,
  height:  number,
  padding: number,                       // routing clearance, default 16
  pins:    VectorPinDescriptor           // Emscripten vector — NOT a plain JS array
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
  pins: VectorPinRef                     // Emscripten vector — NOT a plain JS array
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
{ net: string, points: VectorPoint2D }
```
`points` is an ordered polyline (Emscripten vector). Each consecutive pair of points is one wire
segment. All segments are orthogonal (horizontal or vertical only). Points at junctions are
snapped to the junction centre. Iterate with `.size()` / `.get(i)`.

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
  wires:           VectorWire,                // iterate: .size() / .get(i)
  junctions:       VectorJunctionDot,         // iterate: .size() / .get(i)
  netLabels:       VectorNetLabelHint,        // iterate: .size() / .get(i)
  componentLabels: VectorComponentLabelHint   // iterate: .size() / .get(i)
}
```
Returned by `routeAll()` and `replaceComponent()`. This is a value-copy result — **do not call
`.delete()`** on it or on any of its vector fields.

### `IncrementalRouteResult`
```js
{
  affectedNets: VectorString,          // Emscripten vector of strings — iterate: .size() / .get(i)
  routes:       SchematicRouteResult   // wires/junctions for those nets only
}
```
Partial routing result from `moveComponent()`. Contains only the nets connected to the moved component.
`routes.componentLabels` contains exactly one entry — the updated hint for
the moved component. Merge it into the full result by replacing the entry
whose `componentId` matches. Do not call `.delete()` on this result or its fields.

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
{ name: string, pads: VectorPoint2D }  // pads: Emscripten vector — NOT a plain JS array
```

### `PcbRouteResult`
```js
{
  wires:     VectorWire,        // iterate: .size() / .get(i)
  junctions: VectorJunctionDot  // iterate: .size() / .get(i)
}
```
Returned by `pcb.route()`. Value-copy result — **do not call `.delete()`** on it or its fields.

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
with (0,0) as the center. The `pins` field must be a `VectorPinDescriptor`; delete it
after the call.

```js
const pins = new Module.VectorPinDescriptor();
pins.push_back({ number: 1, name: 'VCC', x: -20, y: -30, directionFlags: Module.PinDirection.DirUp });
pins.push_back({ number: 2, name: 'GND', x:  20, y:  30, directionFlags: Module.PinDirection.DirDown });
pins.push_back({ number: 3, name: 'IN',  x: -40, y:   0, directionFlags: Module.PinDirection.DirLeft });
pins.push_back({ number: 4, name: 'OUT', x:  40, y:   0, directionFlags: Module.PinDirection.DirRight });
wb.addComponent({ id: 'U1', width: 80, height: 60, padding: 16, pins });
pins.delete();
```

#### `wb.addNet(descriptor)`
Add or replace a net. Pins are referenced by `(componentId, pinNumber)`. The `pins` field must
be a `VectorPinRef`; delete it after the call.

```js
const pins = new Module.VectorPinRef();
pins.push_back({ componentId: 'U1', pinNumber: 1 });
pins.push_back({ componentId: 'U2', pinNumber: 1 });
pins.push_back({ componentId: 'U3', pinNumber: 1 });
wb.addNet({ name: 'VCC', pins });
pins.delete();
```

#### `wb.clear()`
Remove all components and nets, reset to empty state.

---

### Step 2 — Classify nets

The library auto-detects power buses (VCC, GND, etc.) using a statistical
outlier heuristic on pin counts. Review and optionally override before routing.

#### `wb.classify()` → `VectorNetClassification`

Returns a `VectorNetClassification` (Emscripten vector). Iterate with `.size()` / `.get(i)`.
Modify entries by index, then pass back to `applyClassification()`. Call `.delete()` afterwards.

```js
const cls = wb.classify();
for (let i = 0; i < cls.size(); i++) {
  const c = cls.get(i);
  console.log(c.name, c.isBus ? 'bus' : 'signal', 'level:', c.busLevel);
}
```

#### `wb.applyClassification(cls)`
Pass the (possibly modified) `VectorNetClassification` back to the library. Must be called before
`computePlacements()`. Call `.delete()` on the vector after this call.

```js
// Example: force RST to be treated as a signal, not a bus
for (let i = 0; i < cls.size(); i++) {
  const c = cls.get(i);
  if (c.name === 'RST') {
    // value_object fields are read-only through get() — rebuild the entry
    cls.set(i, { ...c, isBus: false, busLevel: -1 });
  }
}
wb.applyClassification(cls);
cls.delete();
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

#### `wb.setComponentPlacement(componentId, placement)`
Override the placement of one component. Takes effect on the next `routeAll()` or `moveComponent()` call.

```js
wb.setComponentPlacement('U1', {
  position: { x: 200, y: 150 },
  transform: { rotation: 90, flipX: false }
});
```

#### `wb.setPlacements(placements)`
Override the placements for many components at once. Takes effect on the next `routeAll()` or `moveComponent()` call.

```js
const p = new Module.ComponentPlacements();
p.set('U1', { position: { x: 200, y: 150 }, transform: { rotation: 0, flipX: false } });
p.set('U2', { position: { x: 400, y: 150 }, transform: { rotation: 0, flipX: false } });
wb.setPlacements(p);
p.delete();
```

---

### Step 4 — Route all nets

#### `wb.routeAll()` → `SchematicRouteResult`
Routes all nets using the current component placements and classification.
Must be called after `computePlacements()`. All fields of the returned result are Emscripten
vectors; iterate with `.size()` / `.get(i)`. Do not call `.delete()` on the result or its fields.

```js
const result = wb.routeAll();

// Draw wires
for (let i = 0; i < result.wires.size(); i++) {
  const wire = result.wires.get(i);
  ctx.beginPath();
  for (let j = 0; j < wire.points.size(); j++) {
    const p = wire.points.get(j);
    j === 0 ? ctx.moveTo(p.x, p.y) : ctx.lineTo(p.x, p.y);
  }
  ctx.stroke();
}

// Draw junction dots
for (let i = 0; i < result.junctions.size(); i++) {
  const d = result.junctions.get(i);
  ctx.arc(d.position.x, d.position.y, 4, 0, Math.PI * 2);
  ctx.fill();
}

// Draw net labels (optional)
for (let i = 0; i < result.netLabels.size(); i++) {
  const l = result.netLabels.get(i);
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

// Which nets changed? (VectorString)
for (let i = 0; i < delta.affectedNets.size(); i++) {
  console.log('re-routed:', delta.affectedNets.get(i));
}

// Updated wires for the affected nets only (VectorWire)
for (let i = 0; i < delta.routes.wires.size(); i++) {
  const wire = delta.routes.wires.get(i);
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
// Old pin 1 (Base)      → new pin 2
// Old pin 2 (Collector) → new pin 3
// Old pin 3 (Emitter)   → new pin 1

const pinMap = new Module.PinMap();
pinMap.set(1, 2);
pinMap.set(2, 3);
pinMap.set(3, 1);

const newPins = new Module.VectorPinDescriptor();
newPins.push_back({ number: 1, name: 'E', x:   0, y:  30, directionFlags: Module.PinDirection.DirDown });
newPins.push_back({ number: 2, name: 'B', x: -40, y:   0, directionFlags: Module.PinDirection.DirLeft });
newPins.push_back({ number: 3, name: 'C', x:   0, y: -30, directionFlags: Module.PinDirection.DirUp });

const result = wb.replaceComponent({
  componentId: 'Q1',
  newDescriptor: { id: 'Q1', width: 80, height: 60, padding: 16, pins: newPins },
  pinMapping: pinMap,
});

newPins.delete();
pinMap.delete();
// result is a full SchematicRouteResult — re-render everything
```

---

### Diagnostics

#### `wb.printRoutingStats()` → `boolean`

Prints diagnostic information about the current routing state to the console output.
Only meaningful after `routeAll()` or `moveComponent()`. Returns `true` if the
routing state is consistent, `false` if problems were detected.

```js
const ok = wb.printRoutingStats();
if (!ok) console.warn('routing diagnostics reported problems — check console output');
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
The `pads` field must be a `VectorPoint2D`; delete it after the call.

```js
const pads = new Module.VectorPoint2D();
pads.push_back({ x: 120, y: 340 });
pads.push_back({ x: 450, y: 280 });
pads.push_back({ x: 230, y: 510 });
pcb.addNet({ name: 'VCC', pads });
pads.delete();
```

#### `pcb.route()` → `PcbRouteResult`

```js
const result = pcb.route();

for (let i = 0; i < result.wires.size(); i++) {
  const wire = result.wires.get(i);
  // Draw the wire polyline for wire.net
  for (let j = 0; j < wire.points.size(); j++) {
    const p = wire.points.get(j);
    // use p.x, p.y
  }
}
for (let i = 0; i < result.junctions.size(); i++) {
  const d = result.junctions.get(i);
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

// Helper: build a VectorPinDescriptor from a plain JS array and pass it to addComponent.
function addComp(id, w, h, pinDefs) {
  const pins = new M.VectorPinDescriptor();
  for (const pd of pinDefs) pins.push_back(pd);
  wb.addComponent({ id, width: w, height: h, padding: 16, pins });
  pins.delete();
}

// Helper: build a VectorPinRef from a plain JS array and pass it to addNet.
function addNet(name, refs) {
  const pins = new M.VectorPinRef();
  for (const r of refs) pins.push_back(r);
  wb.addNet({ name, pins });
  pins.delete();
}

// ── 1. Components ────────────────────────────────────────────────────────────

addComp('U1', 80, 60, [
  { number: 1, name: 'VCC', x: -20, y: -30, directionFlags: M.PinDirection.DirUp },
  { number: 2, name: 'GND', x:  20, y:  30, directionFlags: M.PinDirection.DirDown },
  { number: 3, name: 'OUT', x:  40, y:   0, directionFlags: M.PinDirection.DirRight },
]);
addComp('U2', 80, 60, [
  { number: 1, name: 'VCC', x: -20, y: -30, directionFlags: M.PinDirection.DirUp },
  { number: 2, name: 'GND', x:  20, y:  30, directionFlags: M.PinDirection.DirDown },
  { number: 3, name: 'IN',  x: -40, y:   0, directionFlags: M.PinDirection.DirLeft },
]);
addComp('R1', 25, 55, [
  { number: 1, name: 'A', x: -0.5, y: -27.5, directionFlags: M.PinDirection.DirUp },
  { number: 2, name: 'B', x: -0.5, y:  27.5, directionFlags: M.PinDirection.DirDown },
]);

// ── 2. Nets ──────────────────────────────────────────────────────────────────

addNet('VCC', [{ componentId: 'U1', pinNumber: 1 }, { componentId: 'U2', pinNumber: 1 }]);
addNet('GND', [{ componentId: 'U1', pinNumber: 2 }, { componentId: 'U2', pinNumber: 2 }]);
addNet('SIG', [{ componentId: 'U1', pinNumber: 3 }, { componentId: 'R1', pinNumber: 1 },
               { componentId: 'U2', pinNumber: 3 }]);

// ── 3. Classify ──────────────────────────────────────────────────────────────

const cls = wb.classify();           // VectorNetClassification
wb.applyClassification(cls);
cls.delete();

// ── 4. Place ─────────────────────────────────────────────────────────────────

const placements = wb.computePlacements();
const pos = placements.toObject();   // plain JS object for your own bookkeeping
placements.delete();

// ── 5. Route ─────────────────────────────────────────────────────────────────

const routes = wb.routeAll();        // SchematicRouteResult — do NOT .delete()

// render routes.wires (VectorWire), routes.junctions, routes.netLabels,
// routes.componentLabels — all Emscripten vectors, iterate with size()/get(i)

// ── 6. Move on drop ──────────────────────────────────────────────────────────

function onDrop(compId, newX, newY) {
  const delta = wb.moveComponent(compId, { 
    position: { x: newX, y: newY }, 
    transform: { rotation: 0, flipX: false } 
  });
  // delta.affectedNets  → VectorString: iterate with size()/get(i)
  // delta.routes.wires  → VectorWire:   iterate with size()/get(i)
}
```

---

## Memory management

| Object | When to `.delete()` |
|---|---|
| `VectorPinDescriptor` | Immediately after `addComponent()` or `replaceComponent()` |
| `VectorPinRef` | Immediately after `addNet()` |
| `VectorPoint2D` (pads) | Immediately after `pcb.addNet()` |
| `VectorNetClassification` | Immediately after `applyClassification()` |
| `ComponentPlacements` | After passing to / retrieving from WASM |
| `PinMap` | After passing to `replaceComponent()` |
| `WireBender` instance | When the schematic session ends |
| `PcbVisualizer` instance | When done visualising |

`SchematicRouteResult`, `IncrementalRouteResult`, and `PcbRouteResult` are value-copy results
returned from WASM. **Do not call `.delete()`** on them or on any vector field accessed through
them (`result.wires`, `result.junctions`, `wire.points`, `delta.affectedNets`, etc.).

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
