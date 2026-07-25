// evio6_file plugin: SRO streaming evio chain -
//   EvioSroBlockSource (timeslice = evio block, naive reader + parser)
//   -> SroFrameUnfolder (frame -> PhysicsEvent; bypass or ECAL finder)
//   -> SroRNTupleWriter (frames/fadc_hits/dcrb_hits tables).
//
// Run:  jana4ml4fpga -Pplugins=evio6_file -Pjana:nevents=<blocks> \
//         -Pevio6_file:output_file=... /data/sro_boyarinov_data_2026/sro_000791.evio.00000

#include <string>

#include <JANA/JApplication.h>
#include <JANA/JEventSourceGeneratorT.h>

#include "EvioSroBlockSource.h"
#include "SroFrameUnfolder.h"
#include "SroNullWriter.h"
#include "SroRNTupleWriter.h"

extern "C" {
void InitPlugin(JApplication* app) {
    InitJANAPlugin(app);
    app->Add(new JEventSourceGeneratorT<EvioSroBlockSource>);
    app->Add(new SroFrameUnfolder);

    // Writer choice is a performance-isolation knob: 'null' walks the same data
    // but writes nothing, so (rntuple - null) wall time = cost of RNTuple output.
    std::string writer = "rntuple";
    app->SetDefaultParameter("evio6_file:writer", writer, "Output backend: 'rntuple' (default) or 'null' (checksum only, for performance isolation)");
    if (writer == "null") {
        app->Add(new SroNullWriter);
    } else {
        app->Add(new SroRNTupleWriter);
    }
}
}
