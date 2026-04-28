# WireBender

**WireBender** is a high-performance schematic routing and PCB net visualization library.
It is written in C++ and compiled to WebAssembly via Emscripten for use in web environments. 

⚡ **WireBender is the routing engine used by [PCB ReTrace](https://pcb.etaras.com/) for generating schematic diagrams.**

The library handles complex tasks such as automatic component placement, connection routing, and auto-detecting buses.

## 🚀 Key Features

* **Initial Component Placement**: Automatic component placement on schematic diagrams.
* **Schematic Routing**: Automatic orthogonal hyperedge wire routing with junction dot generation.
* **Smart Labeling**: Computes collision-free coordinates for net, reference, and value labels.
* **PcbVisualizer**: Optimized multi-point network routing for PCB pad clusters (non-orthogonal, crossing minimization).
* **Smart Classification**: Statistical auto-detection of power and ground buses (VCC, GND, etc.) with option to override.
* **Interactive Re-routing**: Real-time incremental routing updates during netlist expansion or component drag-and-drop/transform.
* **Component Swapping**: Swap component geometries while maintaining netlist integrity via pin mapping.

## 📥 Installation & Downloads

* **GitHub Releases**: Zipped release versions of the compiled library can be downloaded from the [GitHub Releases page](https://github.com/dev-lab/WireBender/releases).
* **Direct Hosting for Web Apps**: Unzipped `WireBender.js` and `WireBender.wasm` files are hosted directly via GitHub Pages for immediate use in web applications. They are available for `latest`, `wip` (work-in-progress), and all specific release tags:
  * Base URL:[https://dev-lab.github.io/WireBender/](https://dev-lab.github.io/WireBender/)
  * Example import: `https://dev-lab.github.io/WireBender/latest/WireBender.js`

## 📖 Documentation

For complete details on classes, data structures, coordinate systems, and advanced use cases, please see the **[API Reference](doc/WireBender-API.md)**.

## 🛠 Usage Example

```javascript
import WireBenderModule from 'https://dev-lab.github.io/WireBender/latest/WireBender.js';

// Instruct Emscripten to fetch the .wasm file from the remote URL
const Module = await WireBenderModule({
  locateFile: f => f === 'WireBender.wasm' ? 'https://dev-lab.github.io/WireBender/latest/WireBender.wasm' : f
});

// 1. Initialize the router
const wb = new Module.WireBender();

// 2. Describe your netlist
// Note: See API reference for details on passing Vectors vs plain JS arrays.
const pins = new Module.VectorPinDescriptor();
pins.push_back({ number: 1, name: 'VCC', x: -20, y: -30, directionFlags: Module.PinDirection.DirUp });
pins.push_back({ number: 2, name: 'GND', x:  20, y:  30, directionFlags: Module.PinDirection.DirDown });

wb.addComponent({ 
    id: 'U1', width: 80, height: 60, padding: 16, 
    pins: pins
});
pins.delete(); // Free C++ memory

const netPins = new Module.VectorPinRef();
netPins.push_back({ componentId: 'U1', pinNumber: 1 });
wb.addNet({ name: 'VCC', pins: netPins });
netPins.delete(); // Free C++ memory

// 3. Auto-place and Route
const classification = wb.classify();
wb.applyClassification(classification);
classification.delete();

const placements = wb.computePlacements();
const routes = wb.routeAll();
placements.delete();
```

## 📦 Memory Management

Because this is a WASM-wrapped C++ library, certain class instances (e.g. `WireBender`, `PcbVisualizer`, `ComponentPlacements`, `PinMap`) and input vectors must be manually destroyed to free C++ memory by calling `.delete()` when they are no longer needed. 

Output result objects are copied to standard JavaScript variables and do not need to be deleted. *(See the [API Reference](doc/WireBender-API.md#data-structures-and-memory) for detailed vector memory rules).*

## ⚖️ License & Commercial Use

WireBender is **Dual Licensed**:

1.  **Open Source (AGPLv3):** Ideal for hobbyists, educational use, and open-source projects. You are free to use and modify the software, provided that any modifications are also made open source under the same terms.
2.  **Commercial License:** For proprietary use, internal corporate deployment without copyleft restrictions, or integration into closed-source workflows, a commercial license is available.

Please see [LICENSE](LICENSE) for details.

For commercial licensing inquiries, please contact: Taras Greben <taras.greben@gmail.com>.

## 📦 Third-Party Libraries

WireBender utilizes several open-source libraries from the **Adaptagrams** project. We are grateful to the maintainers and contributors of these projects.

See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for full text.
