// Native loader for the legacy SRS mapping .cfg format (DET/APV lines).
// Replaces GemMapping::LoadMapping for the subset the reconstruction uses.
// Only CARTESIAN DET rows are supported (the only kind in current mappings).
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "config.hpp"

namespace gemreco {

namespace {

/// Split a comma-separated cfg line into trimmed tokens.
std::vector<std::string> tokens(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ',')) {
        // trim spaces/tabs
        auto b = item.find_first_not_of(" \t\r");
        auto e = item.find_last_not_of(" \t\r");
        out.push_back(b == std::string::npos ? "" : item.substr(b, e - b + 1));
    }
    return out;
}

}  // namespace

Config load_config_mapping_cfg(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("gemreco: cannot open mapping cfg: " + path);

    Config cfg;
    int det_counter = 0;  // legacy: detector id = DET row order

    struct PlaneMeta {
        std::string det_name;
        int det_id;
        int axis;  // X=0, Y=1
    };
    std::map<std::string, PlaneMeta> plane_meta;

    std::string line;
    while (std::getline(f, line)) {
        auto b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos || line[b] == '#') continue;

        auto t = tokens(line);
        if (t.empty()) continue;

        if (t[0] == "DET") {
            // DET, CARTESIAN, detType, detName, planeX, sizeX, connX, orientX,
            //                                   planeY, sizeY, connY, orientY
            if (t.size() < 12 || t[1] != "CARTESIAN") {
                throw std::runtime_error("gemreco: unsupported DET row (only CARTESIAN): " + line);
            }
            const std::string det_name = t[3];
            const int det_id = det_counter++;
            // LEGACY QUIRK (preserved): GemMapping stores plane sizes as float
            // (fCartesianPlaneMap is vector<float>), so strip positions are computed
            // from the float-rounded size. Cast through float for bit-parity.
            struct { std::string name; double size; int axis; } plane_defs[2] = {
                {t[4], static_cast<float>(std::stod(t[5])), 0},
                {t[8], static_cast<float>(std::stod(t[9])), 1}};
            for (const auto& pd : plane_defs) {
                PlaneInfo p;
                p.name = pd.name;
                p.size_mm = pd.size;
                cfg.planes[p.name] = std::move(p);
                plane_meta[pd.name] = {det_name, det_id, pd.axis};
            }
        } else if (t[0] == "APV") {
            // APV, fecId, adcCh, planeName, apvOrient, apvIndex, apvHdr, status
            if (t.size() < 7) throw std::runtime_error("gemreco: bad APV row: " + line);
            ApvInfo a;
            const int fec_id = std::stoi(t[1]);
            const int adc_ch = std::stoi(t[2]);
            a.apv_id = (fec_id << 4) | adc_ch;
            a.plane_name = t[3];
            a.orientation = std::stoi(t[4]);
            a.index_on_plane = std::stoi(t[5]);

            auto meta = plane_meta.find(a.plane_name);
            if (meta == plane_meta.end()) {
                throw std::runtime_error("gemreco: APV row references unknown plane: " + line);
            }
            a.detector_name = meta->second.det_name;
            a.detector_id = meta->second.det_id;
            a.plane_id = meta->second.axis;         // legacy fPlaneIDFromPlaneMap: X=0, Y=1
            a.sample_plane_id = meta->second.axis;  // legacy GetPlaneID: same axis value
            a.sample_detector_id = 0;               // legacy quirk: never-populated map -> 0

            cfg.planes.at(a.plane_name).apv_ids.push_back(a.apv_id);  // cfg-file order
            cfg.apvs[a.apv_id] = std::move(a);
        }
        // FEC and other rows: not needed by reconstruction
    }

    if (cfg.apvs.empty()) throw std::runtime_error("gemreco: no APV rows in " + path);

    // Legacy semantic: reconstruction iterates GetAPVIDListFromPlaneMap, i.e. only
    // planes that actually have APVs. Drop APV-less planes (e.g. an X plane defined
    // in a DET row but never referenced by an APV row).
    for (auto it = cfg.planes.begin(); it != cfg.planes.end();) {
        if (it->second.apv_ids.empty()) it = cfg.planes.erase(it);
        else ++it;
    }
    return cfg;
}

}  // namespace gemreco
