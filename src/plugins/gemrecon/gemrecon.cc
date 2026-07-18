// Copyright 2022, David Lawrence
// Subject to the terms in the LICENSE file found in the top-level directory.
//
//

#include "ClusterFactory.h"
#include "PedestalFactory.h"
#include "RawDataFactory.h"
#include "GemReconDqmProcessor.h"
#include "ApvDecodedDataFactory.h"
#include "PlaneDecodedDataFactory.h"
#include "GemMappingService.h"
#include "PeakFactory.h"

#include <JANA/JFactoryGenerator.h>

#include "SampleAnalysisFactory.h"
#include <extensions/jana/CozyFactoryGeneratorT.h>
#include <extensions/jana/CozyFactory.h>


extern "C" {
    void InitPlugin(JApplication *app) {
        // Initializes this plugin
        InitJANAPlugin(app);

        // Adds our processor to JANA2 to execute
        app->Add(new ml4fpga::gem::GemReconDqmProcessor(app));

        app->Add(new JFactoryGeneratorT<ml4fpga::gem::PedestalFactory>());
        app->Add(new JFactoryGeneratorT<ml4fpga::gem::ClusterFactory>());
        app->Add(new JFactoryGeneratorT<ml4fpga::gem::RawDataFactory>());
        app->Add(new JFactoryGeneratorT<ml4fpga::gem::ApvDecodedDataFactory>());
        app->Add(new JFactoryGeneratorT<ml4fpga::gem::PlaneDecodedDataFactory>());
        app->Add(new JFactoryGeneratorT<ml4fpga::gem::PeakFactory>());
        app->Add(new CozyFactoryGeneratorT<ml4fpga::gem::SampleAnalysisFactory>("sample", app));

        app->ProvideService(std::make_shared<ml4fpga::gem::GemMappingService>(app));
    }
}
    
