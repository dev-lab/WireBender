#include "PcbVisualizer.h"
#include "internal/PcbRouter.h"
#include <vector>

namespace WireBender {

class PcbVisualizerImpl {
public:
	PcbRouteResult route() {
		return router.route(nets);
	}
	PcbRouter router;
	std::vector<PcbNet> nets;
};

// ─────────────────────────────────────────────────────────────────────────────
PcbVisualizer::PcbVisualizer(): impl(std::make_unique<PcbVisualizerImpl>()) {}
PcbVisualizer::~PcbVisualizer() = default;

void PcbVisualizer::addNet(const PcbNet& net) {
	impl->nets.push_back(net);
}

void PcbVisualizer::clear() {
	impl->nets.clear();
}

PcbRouteResult PcbVisualizer::route() {
	return impl->route();
}

} // namespace WireBender
