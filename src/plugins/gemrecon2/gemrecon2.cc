// gemrecon2 - thin JANA adapter over the standalone gemreco library (MT plan W2-F).
// Replaces the legacy gemrecon plugin (7 factories + GemMapping singleton) with:
//   GemRecoService (immutable Config + learn->freeze calibration)
//   GemRecoOmniFactory (declarative JOmniFactory: marshal in, reconstruct, republish)
// Do NOT load together with the legacy 'gemrecon' plugin - they publish the same types.
#include <JANA/JApplication.h>
#include <JANA/JFactoryGenerator.h>

#include "GemRecoOmniFactory.h"
#include "GemRecoService.h"

extern "C" {
void InitPlugin(JApplication* app) {
    InitJANAPlugin(app);
    app->ProvideService(std::make_shared<GemRecoService>(app));
    app->Add(new JFactoryGeneratorT<GemRecoOmniFactory>());
}
}
