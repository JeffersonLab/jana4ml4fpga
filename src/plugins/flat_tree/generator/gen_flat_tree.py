# PODIO on minimum - this script generates some boilerplate ROOT IO from simply defined structures
from collections import namedtuple
from typing import Callable
from dataclasses import dataclass


struct_template = """
#pragma once

#include <cstdint>
#include <vector>
#include <TTree.h>
#include "AlignedArraysIO.h"

namespace flatio {
    struct {{name}}
    {
{{struct_decl_code}}
    };
    
    class {{name}}IO: public AlignedArraysIO
    {
    public:
        void bindToTree(TTree *tree) override {
            m_is_bound = true;
            tree->Branch("{{root_name}}_count", &m_count, "{{root_name}}_count/l");
{{record_bind_code}}
        }

        void clear() override {
            m_count = 0;
{{record_clear_code}}
        }
        
        void add(const {{name}}& data) {
            if(!m_is_bound) {
                throw std::logic_error("Can't add {{name}} data because {{name}}IO is not bound to tree");
            }
            m_count++;
{{record_add_code}}
        }
        
        bool isBoundToTree() const { return m_is_bound; }
        
    private:
        bool m_is_bound = false;
        uint64_t m_count;
{{record_decl_code}}
    };
}
"""


@dataclass
class FieldInfo:
    """Class for keeping track of an item in inventory."""
    type: str
    name: str
    comment: str = ""

    def gen_struct_decl(self):
        return f'        {self.type} {self.name};'

    def gen_record_bind(self, class_root_name):
        return f'            tree->Branch("{class_root_name}_{self.name}", &m_vect_{self.name});'

    # Structure declaration
    def gen_record_clear(self):
        return f'            m_vect_{self.name}.clear();'

    def gen_record_add(self):
        return f'            m_vect_{self.name}.push_back(data.{self.name});'

    # Fields declaration
    def gen_record_decl(self):
        return f'        std::vector<{self.type}> m_vect_{self.name};'


@dataclass
class VectorFieldInfo:
    """Class for keeping track of an item in inventory."""
    type: str
    name: str
    comment: str = ""

    def gen_struct_decl(self):
        return f'        std::vector<{self.type}> {self.name};'

    def gen_record_bind(self, class_root_name):
        return f'            tree->Branch("{class_root_name}_{self.name}_index", &m_vect_{self.name}_index);\n'\
               f'            tree->Branch("{class_root_name}_{self.name}_count", &m_vect_{self.name}_count);\n'\
               f'            tree->Branch("{class_root_name}_{self.name}", &m_vect_{self.name});'

    # Structure declaration
    def gen_record_clear(self):
        return f'            m_vect_{self.name}.clear();\n' \
               f'            m_vect_{self.name}_index.clear();\n' \
               f'            m_vect_{self.name}_count.clear();'

    def gen_record_add(self):
        return f'            for(auto item: data.{self.name}) m_vect_{self.name}.push_back(item);\n'\
               f'            // First record, {self.name} index = 0\n'\
               f'            if(m_vect_{self.name}_count.size() == 0) {{\n'\
               f'                m_vect_{self.name}_index.push_back(0);\n'\
               f'            }} else {{\n'\
               f'                auto last_count = m_vect_{self.name}_count[m_vect_{self.name}_count.size() - 1];\n'\
               f'                auto last_index = m_vect_{self.name}_index[m_vect_{self.name}_index.size() - 1];\n'\
               f'                m_vect_{self.name}_index.push_back(last_index+last_count);\n'\
               f'            }}\n'\
               f'            m_vect_{self.name}_count.push_back(data.{self.name}.size());'

    def gen_record_decl(self):
        return f'        std::vector<{self.type}> m_vect_{self.name}_count;\n'\
               f'        std::vector<{self.type}> m_vect_{self.name}_index;\n'\
               f'        std::vector<{self.type}> m_vect_{self.name};'


@dataclass
class ClassInfo:
    name: str             # Name. E.g. SrsRecord
    root_name: str        # Part of root file name: e.g. 'srs' => fileds will be srs_id, srs_value, etc.
    fields: list          # List of variable fields

    def gen_struct_decl(self):
        return "\n" + "\n".join([f.gen_struct_decl() for f in self.fields])

    def gen_record_bind(self):
        return "\n" + "\n".join([f.gen_record_bind(self.root_name) for f in self.fields])

    # Structure declaration
    def gen_record_clear(self):
        return "\n" + "\n".join([f.gen_record_clear() for f in self.fields])

    def gen_record_add(self):
        return "\n" + "\n".join([f.gen_record_add() for f in self.fields])

    # Fields declaration
    def gen_record_decl(self):
        return "\n" + "\n".join([f.gen_record_decl() for f in self.fields])


io_classes = [

    ClassInfo(
        name="SrsRawRecord",
        root_name="srs_raw",
        fields=[
            FieldInfo('uint32_t', 'roc'),
            FieldInfo('uint32_t', 'slot'),
            FieldInfo('uint32_t', 'index'),
            FieldInfo('uint32_t', 'apv_id'),
            FieldInfo('uint32_t', 'channel_apv'),
            FieldInfo('uint16_t', 'best_sample'),
            VectorFieldInfo('uint16_t', 'samples'),
        ]),
    ClassInfo(
        name="SrsDecodedRecord",
        root_name="srs_decoded",
        fields=[
            FieldInfo('uint32_t', 'apv_id'),
            FieldInfo('std::string', 'plane_name'),
            FieldInfo('std::string', 'detector'),
            VectorFieldInfo('uint16_t', 'samples'),
        ]),
    ClassInfo(
        name="SrsPreReconRecord",
        root_name="srs_prerecon",
        fields=[
            FieldInfo('double', 'y'),
            FieldInfo('double', 'x'),
        ]),

    ClassInfo(
        name="F125WindowRawRecord",
        root_name="f125_wraw",
        fields=[
            FieldInfo('uint32_t', 'roc'),
            FieldInfo('uint32_t', 'slot'),
            FieldInfo('uint32_t', 'index'),
            FieldInfo('bool',     'invalid_samples'),
            FieldInfo('bool',     'overflow'),
            FieldInfo('uint32_t', 'itrigger'),
            VectorFieldInfo('uint16_t', 'samples'),
        ]),

    ClassInfo(
        name="F250WindowRawRecord",
        root_name="f250_wraw",
        fields=[
            FieldInfo('uint32_t', 'roc'),
            FieldInfo('uint32_t', 'slot'),
            FieldInfo('uint32_t', 'index'),
            FieldInfo('bool',     'invalid_samples'),
            FieldInfo('bool',     'overflow'),
            FieldInfo('uint32_t', 'itrigger'),
            VectorFieldInfo('uint16_t', 'samples'),
        ]),

    ClassInfo(
        name="F125FDCPulseRecord",
        root_name="f125_pulse",
        fields=[
            FieldInfo('uint32_t', 'roc'),
            FieldInfo('uint32_t', 'slot'),
            FieldInfo('uint32_t', 'index'),
            FieldInfo('uint32_t', 'npk',                       'from first word'),
            FieldInfo('uint32_t', 'le_time',                   'from first word'),
            FieldInfo('uint32_t', 'time_quality_bit',          'from first word'),
            FieldInfo('uint32_t', 'overflow_count',            'from first word'),
            FieldInfo('uint32_t', 'pedestal',                  'from second word'),
            FieldInfo('uint32_t', 'integral',                  'from second word (type 6)'),
            FieldInfo('uint32_t', 'peak_amp',                  'from second word (type 9)'),
            FieldInfo('uint32_t', 'peak_time',                 'from second word'),
            FieldInfo('uint32_t', 'word1',                     'first word'),
            FieldInfo('uint32_t', 'word2',                     'second word'),
            FieldInfo('uint32_t', 'nsamples_pedestal',         'number of samples used in integral'),
            FieldInfo('uint32_t', 'nsamples_integral',         'number of samples used in pedestal'),
            FieldInfo('bool',     'emulated',                  'true if emulated values are copied to the main input'),
            FieldInfo('uint32_t', 'le_time_emulated',          'emulated from raw data when available'),
            FieldInfo('uint32_t', 'time_quality_bit_emulated', 'emulated from raw data when available'),
            FieldInfo('uint32_t', 'overflow_count_emulated',   'emulated from raw data when available'),
            FieldInfo('uint32_t', 'pedestal_emulated',         'emulated from raw data when available'),
            FieldInfo('uint32_t', 'integral_emulated',         'emulated from raw data when available'),
            FieldInfo('uint32_t', 'peak_amp_emulated',         'emulated from raw data when available'),
            FieldInfo('uint32_t', 'peak_time_emulated',        'emulated from raw data when available'),
        ]),
    ClassInfo(
        name="F250FDCPulseRecord",
        root_name="f250_pulse",
        fields=[
            FieldInfo('uint32_t', 'roc'),
            FieldInfo('uint32_t', 'slot'),
            FieldInfo('uint32_t', 'index'),
            FieldInfo('uint32_t', 'event_within_block'),
            FieldInfo('bool', 'qf_pedestal'),
            FieldInfo('uint32_t', 'pedestal'),
            FieldInfo('uint32_t', 'integral'),
            FieldInfo('bool', 'qf_nsa_beyond_ptw'),
            FieldInfo('bool', 'qf_overflow'),
            FieldInfo('bool', 'qf_underflow'),
            FieldInfo('uint32_t', 'nsamples_over_threshold'),
            FieldInfo('uint32_t', 'course_time'),               # < 4 ns/count
            FieldInfo('uint32_t', 'fine_time'),                 # < 0.0625 ns/count
            FieldInfo('uint32_t', 'pulse_peak'),
            FieldInfo('bool', 'qf_vpeak_beyond_nsa'),
            FieldInfo('bool', 'qf_vpeak_not_found'),
            FieldInfo('bool', 'qf_bad_pedestal'),
            FieldInfo('uint32_t', 'pulse_number'),
            FieldInfo('uint32_t', 'nsamples_integral'),
            FieldInfo('uint32_t', 'nsamples_pedestal'),
            FieldInfo('bool', 'emulated'),
            FieldInfo('uint32_t', 'integral_emulated'),
            FieldInfo('uint32_t', 'pedestal_emulated'),
            FieldInfo('uint32_t', 'time_emulated'),
            FieldInfo('uint32_t', 'course_time_emulated'),
            FieldInfo('uint32_t', 'fine_time_emulated'),
            FieldInfo('uint32_t', 'pulse_peak_emulated'),
            FieldInfo('uint32_t', 'qf_emulated'),
        ]
    ),
    ClassInfo(
        name="GemSimpleCluster",
        root_name="gem_scluster",
        fields=[
            FieldInfo('uint32_t', 'x_index'),
            FieldInfo('uint32_t', 'y_index'),
            FieldInfo('double', 'x'),
            FieldInfo('double', 'y'),
            FieldInfo('double', 'energy'),
            FieldInfo('double', 'adc'),
        ]),

    ClassInfo(
        name="GemPlanePeak",
        root_name="gem_peak",
        fields=[
            FieldInfo('uint32_t', 'plane_id'),
            FieldInfo('std::string', 'plane_name'),
            FieldInfo('uint32_t', 'index'),
            FieldInfo('uint32_t', 'apv_id'),
            FieldInfo('uint32_t', 'time_id'),
            FieldInfo('double', 'height'),
            FieldInfo('double', 'width'),
            FieldInfo('double', 'area'),
            FieldInfo('double', 'real_pos'),
        ]),
    ClassInfo(
        name="FpgaF125Cluster",
        root_name="fpga_f125_cluster",
        fields=[
            FieldInfo('uint32_t', 'id'),
            FieldInfo('float', 'pos_x'),
            FieldInfo('float', 'pos_y'),
            FieldInfo('float', 'pos_z'),
            FieldInfo('float', 'dedx'),
            FieldInfo('float', 'size'),
            FieldInfo('float', 'width_y1'),
            FieldInfo('float', 'width_y2'),
            FieldInfo('float', 'width_dy'),
            FieldInfo('float', 'length_x1'),
            FieldInfo('float', 'length_x2'),
            FieldInfo('float', 'length_dx'),
        ]),
    ClassInfo(
        name="FpgaHitToTrack",
        root_name="fpga_hit_track",
        fields=[
            FieldInfo('uint32_t', 'hit_index'),
            FieldInfo('uint32_t', 'track_index')
        ]),
    ClassInfo(
        name="FpgaTrackFit",
        root_name="fpga_track_fit",
        fields=[
            FieldInfo('uint32_t', 'id'),
            FieldInfo('float', 'slope'),
            FieldInfo('float', 'intersect')
        ]),
    ClassInfo(
        name="GemSampleData",
        root_name="gem_sample_data",
        fields=[
            FieldInfo('uint64_t', 'id'),
            FieldInfo('uint32_t', 'channel'),
            FieldInfo('uint32_t', 'raw_channel'),
            FieldInfo('uint32_t', 'time_bin'),
            FieldInfo('uint32_t', 'apv'),
            FieldInfo('uint32_t', 'plane'),
            FieldInfo('uint32_t', 'detector'),
            FieldInfo('bool',     'is_noise'),
            FieldInfo('double',   'value'),
            FieldInfo('double',   'raw_value'),
            FieldInfo('double',   'rolling_average'),
            FieldInfo('double',   'rolling_std'),
        ]),

]

def gen_fields_code(template: str, class_info: ClassInfo, spaces: int):
    code = ""
    for i, field_info in enumerate(class_info.fields):
        assert isinstance(field_info, FieldInfo)
        code += template\
            .replace("{{class_root_name}}", class_info.root_name)\
            .replace("{{field_name}}", field_info.name)\
            .replace("{{field_type}}", field_info.type)\
            .replace("{{field_comment}}", field_info.comment)
        code += '\n' + spaces*' ' if i < len(class_info.fields) - 1 else ''
    return code


def generate_header(class_info: ClassInfo):

    # Process the main template
    result = struct_template\
        .replace('{{name}}', class_info.name) \
        .replace('{{root_name}}', class_info.root_name) \
        .replace('{{struct_decl_code}}', class_info.gen_struct_decl()) \
        .replace('{{record_bind_code}}', class_info.gen_record_bind()) \
        .replace('{{record_clear_code}}', class_info.gen_record_clear()) \
        .replace('{{record_add_code}}', class_info.gen_record_add()) \
        .replace('{{record_decl_code}}', class_info.gen_record_decl())
    return result


def generate():
    for class_info in io_classes:
        code = generate_header(class_info)
        print("="*30)
        print(code)
        with open(f"{class_info.name}.h", "w") as text_file:
            text_file.write(code)


if __name__ == "__main__":
    generate()