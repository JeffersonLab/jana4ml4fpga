// CDAQfileMT - EVIO file source plugin with a runtime-switchable PARALLEL parse path.
// (JANA2 2026.x version; the old JBlockSourceArrow/JBlockDisentanglerArrow topology
//  from JANA 2.1.x was retired with the framework migration.)
//
// Usage: load INSTEAD of the (untouched) CDAQfile plugin:
//   -Pplugins=CDAQfileMT,...                   -> serial source, identical to CDAQfile
//   -Pplugins=CDAQfileMT,... -Pevio:parallel=1 -> MmapEvioSource: sequential Emit()
//                                                 does only the mmap block handoff;
//                                                 EVIO parsing runs on the PARALLEL
//                                                 map arrow via ProcessParallel().
//
// Tunables (parallel mode): evio:prefetch_mb (mmap read-ahead, default 256).
#include <JANA/JApplication.h>
#include <JANA/JEventSourceGeneratorT.h>

#include "MmapEvioSource.h"
#include "SerialEvioFileSource.h"

extern "C" {
void InitPlugin(JApplication* app) {
    InitJANAPlugin(app);

    bool parallel = false;
    app->SetDefaultParameter("evio:parallel", parallel,
                             "EVIO source mode: 0 = serial (identical to CDAQfile), "
                             "1 = mmap source with parallel deserialization");

    if (parallel) {
        app->Add(new JEventSourceGeneratorT<MmapEvioSource>);
    } else {
        app->Add(new JEventSourceGeneratorT<SerialEvioFileSource>);
    }
}
}
