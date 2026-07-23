// Created by Dmitry Romanov
// Subject to the terms in the LICENSE file found in the top-level directory.
//

#pragma once


struct EVIOBlockedEventParserConfig {
    int VERBOSE = 1;
    bool PARSE_EPICS = true;
    bool PARSE_TRIGGER = true;
    bool PARSE_CONFIG = true;
    bool PARSE_GEMSRS = true;
    bool PARSE_F250 = true;
    bool PARSE_F125 = true;

    int32_t NSAMPLES_GEMSRS = 3;
};
