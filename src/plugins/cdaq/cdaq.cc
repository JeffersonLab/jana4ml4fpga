#include "CDaqEventSource.h"

#include <JANA/JApplication.h>

extern "C" {
    void InitPlugin(JApplication *app) {
        InitJANAPlugin(app);
        app->Add(new JEventSourceGeneratorT<CDaqEventSource>);
    }
}
