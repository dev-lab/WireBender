# WireBender

**WireBender** is a high-performance schematic routing and PCB net visualization library.
It is written in C++ and compiled to WebAssembly via Emscripten for use in web environments. 

The library handles complex tasks such as automatic component placement, connection routing, and auto-detecting buses.

## 🚀 Key Features

* **Initial Component Placement**: Automatic component placement on schematic diagrams.
* **Schematic Routing**: Automatic orthogonal hyperedge wire routing with junction dot generation.
* **Smart Labeling**: Computes collision-free coordinates for net, reference, and value labels.
* **PcbVisualizer**: Optimized multi-point network routing for PCB pad clusters (non-orthogonal, crossing minimization).
* **Smart Classification**: Statistical auto-detection of power and ground buses (VCC, GND, etc.) with option to override.
* **Interactive Re-routing**: Real-time incremental routing updates during netlist expansion or component drag-and-drop/transform.
* **Component Swapping**: Swap component geometries while maintaining netlist integrity via pin mapping.

## 🛠 Usage Example

```javascript
import WireBenderModule from './WireBender.js';
const Module = await WireBenderModule();

// 1. Initialize the router
const wb = new Module.WireBender();

// 2. Describe your netlist using standard JS arrays/objects
wb.addComponent({ 
    id: 'U1', width: 80, height: 60, padding: 16, 
    pins:[
        { number: 1, name: 'VCC', x: -20, y: -30, directionFlags: Module.PinDirection.DirUp },
        { number: 2, name: 'GND', x:  20, y:  30, directionFlags: Module.PinDirection.DirDown }
    ]
});

wb.addNet({ name: 'VCC', pins:[{ componentId: 'U1', pinNumber: 1 }] });

// 3. Auto-place and Route
const classification = wb.classify();
wb.applyClassification(classification);

const placements = wb.computePlacements();
const routes = wb.routeAll();
```

## 📦 Memory Management

Because this is a WASM-wrapped C++ library, certain class instances (e.g. `WireBender`, `PcbVisualizer`, `ComponentPlacements`, `PinMap`) must be manually destroyed to free C++ memory by calling `.delete()` when they are no longer needed. 

However, thanks to the streamlined API, data models like arrays of pins, nets, and routing results are passed and returned as standard JavaScript arrays and objects.

## ⚖️ License & Commercial Use

WireBender is **Dual Licensed**:

1.  **Open Source (AGPLv3):** Ideal for hobbyists, educational use, and open-source projects. You are free to use and modify the software, provided that any modifications are also made open source under the same terms.
2.  **Commercial License:** For proprietary use, internal corporate deployment without copyleft restrictions, or integration into closed-source workflows, a commercial license is available.

Please see [LICENSE](LICENSE) for details.

For commercial licensing inquiries, please contact: Taras Greben <taras.greben@gmail.com>.

## 📦 Third-Party Libraries

WireBender utilizes several open-source libraries from the **Adaptagrams** project. We are grateful to the maintainers and contributors of these projects.

See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for full text.
