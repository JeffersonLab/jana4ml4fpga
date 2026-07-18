// Fixture dumper plugin - see FixtureDumpProcessor.h
#include <JANA/JApplication.h>
#include "FixtureDumpProcessor.h"

extern "C" {
void InitPlugin(JApplication* app) {
    InitJANAPlugin(app);
    app->Add(new FixtureDumpProcessor(app));
}
}
