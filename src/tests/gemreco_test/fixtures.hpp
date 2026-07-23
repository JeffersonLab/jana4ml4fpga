// Fixture loading helpers for gemreco golden-parity tests.
// Fixtures are CSVs written by the gemreco_fixdump plugin from the LEGACY chain.
// Location comes from the GEMRECO_FIXTURES env var.
#pragma once

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gemreco/event_data.hpp>

namespace fixtures {

inline std::string dir() {
    const char* env = std::getenv("GEMRECO_FIXTURES");
    if (!env) throw std::runtime_error("GEMRECO_FIXTURES env var not set (path to fixture CSVs)");
    return env;
}

inline std::vector<std::string> split(const std::string& line, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, sep)) out.push_back(item);
    return out;
}

inline std::vector<double> parse_doubles(const std::string& space_joined) {
    std::vector<double> out;
    std::stringstream ss(space_joined);
    double v;
    while (ss >> v) out.push_back(v);
    return out;
}

/// srs_raw.csv -> RawEvent per evt_seq, preserving row order (duplicate-channel
/// overwrite semantics in the legacy code depend on it).
inline std::map<size_t, gemreco::RawEvent> load_raw_events() {
    std::ifstream f(dir() + "/srs_raw.csv");
    if (!f) throw std::runtime_error("cannot open srs_raw.csv");
    std::map<size_t, gemreco::RawEvent> events;
    std::string line;
    std::getline(f, line);  // header
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto c = split(line, ',');
        size_t evt = std::stoul(c[0]);
        auto& ev = events[evt];
        ev.event_number = std::stoull(c[1]);
        gemreco::SrsChannelData hit;
        hit.apv_id = std::stoi(c[2]);
        hit.raw_channel_apv = std::stoi(c[3]);
        std::stringstream ss(c[4]);
        unsigned s;
        while (ss >> s) hit.samples.push_back(static_cast<uint16_t>(s));
        ev.srs.push_back(std::move(hit));
    }
    return events;
}

/// Generic loader for "evt_seq,apv_id,<space-joined doubles>[,<space-joined doubles>]" tables.
/// Returns evt -> apv -> columns (one vector<double> per remaining CSV column).
inline std::map<size_t, std::map<int, std::vector<std::vector<double>>>>
load_per_apv(const std::string& filename) {
    std::ifstream f(dir() + "/" + filename);
    if (!f) throw std::runtime_error("cannot open " + filename);
    std::map<size_t, std::map<int, std::vector<std::vector<double>>>> out;
    std::string line;
    std::getline(f, line);  // header
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto c = split(line, ',');
        size_t evt = std::stoul(c[0]);
        int apv = std::stoi(c[1]);
        std::vector<std::vector<double>> cols;
        for (size_t i = 2; i < c.size(); i++) cols.push_back(parse_doubles(c[i]));
        out[evt][apv] = std::move(cols);
    }
    return out;
}

}  // namespace fixtures
