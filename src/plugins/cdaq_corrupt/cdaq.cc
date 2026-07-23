#include "CDaqEventSource.h"
#include "CDaqTCPEventSender.h"

#include <rawdataparser/EVIOBlockedEvent.h>
#include <rawdataparser/CDaqTCPevent.h>

#include <JANA/JApplication.h>

extern "C" {
    void InitPlugin(JApplication *app) {
        InitJANAPlugin(app);
        app->Add(new JEventSourceGeneratorT<CDaqEventSource>);
        app->Add(new CDaqTCPEventSender);

        // The following create factories to serve as containers so the
        // event->Insert calls in either CDAQEVIOFileSource or CDaqEventSource
        // don't have to do it automatically. The reason for doing it here
        // is to ensure both factories exist, even if Insert calls are not
        // made. (At least one of them won't be!). This makes the code in
        // CDaqTCPEventSender::Process simpler and more efficient since it
        // does not need to check for and handle exceptions being thrown
        // every event.
        app->Add(new JFactoryGeneratorT<JFactoryT<EVIOBlockedEvent>>());
        app->Add(new JFactoryGeneratorT<JFactoryT<CDaqTCPevent>>());
    }
}
