#include "config.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace gemreco {

namespace {

std::vector<std::string> split(const std::string& line, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, sep)) out.push_back(item);
    return out;
}

}  // namespace

Config load_config_csv(const std::string& dir) {
    Config cfg;

    // mapping_apvs.csv: apv_id,plane_name,plane_id,det_name,det_id,index_on_plane,orientation
    {
        std::ifstream f(dir + "/mapping_apvs.csv");
        if (!f) throw std::runtime_error("gemreco: cannot open " + dir + "/mapping_apvs.csv");
        std::string line;
        std::getline(f, line);  // header
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            auto c = split(line, ',');
            if (c.size() < 7) throw std::runtime_error("gemreco: bad mapping_apvs row: " + line);
            ApvInfo a;
            a.apv_id = std::stoi(c[0]);
            a.plane_name = c[1];
            a.plane_id = std::stoi(c[2]);
            a.detector_name = c[3];
            a.detector_id = std::stoi(c[4]);
            a.index_on_plane = std::stoi(c[5]);
            a.orientation = std::stoi(c[6]);
            if (c.size() >= 9) {
                a.sample_plane_id = std::stoi(c[7]);
                a.sample_detector_id = std::stoi(c[8]);
            }
            cfg.apvs[a.apv_id] = std::move(a);
        }
    }

    // mapping_planes.csv: plane_name,size_mm,apv_ids (space-joined, ordered by index)
    {
        std::ifstream f(dir + "/mapping_planes.csv");
        if (!f) throw std::runtime_error("gemreco: cannot open " + dir + "/mapping_planes.csv");
        std::string line;
        std::getline(f, line);  // header
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            auto c = split(line, ',');
            if (c.size() < 3) throw std::runtime_error("gemreco: bad mapping_planes row: " + line);
            PlaneInfo p;
            p.name = c[0];
            p.size_mm = std::stod(c[1]);
            std::stringstream ids(c[2]);
            int id;
            while (ids >> id) p.apv_ids.push_back(id);
            cfg.planes[p.name] = std::move(p);
        }
    }

    return cfg;
}

}  // namespace gemreco
