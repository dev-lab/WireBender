# Third-Party Licenses

WireBender utilizes several open-source libraries from the **Adaptagrams** project. We are grateful to the maintainers and contributors of these projects.

## Included Libraries

1.  **libavoid**: A library for object-avoiding routing.
2.  **libcola**: A library for constraint-based layout.
3.  **libvpsc**: A library for variable placement with separation constraints.

## License: LGPL 2.1

These libraries are licensed under the **GNU Lesser General Public License, version 2.1 (LGPL 2.1)**. 

### Compliance for WebAssembly (WASM) Distributions
To satisfy the "Right to Relink" requirement of the LGPL 2.1 in a statically-linked WASM environment:

* **Open Source Users**: The full source code for WireBender is provided, allowing you to re-link the LGPL components yourself.
* **Commercial Users**: If you are using a proprietary version of WireBender, we provide pre-compiled object files (`.o`) for the proprietary sections upon request. This allows you to re-link the LGPL libraries with our logic without requiring access to our proprietary source code.

For the full text of the LGPL 2.1, please see: https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html
