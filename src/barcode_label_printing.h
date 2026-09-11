#pragma once

#include <string>

namespace search {

// Shared data contract for LabelPrint medical barcode labels.
struct BarcodeLabelPayload {
    std::string sample_no;
    std::string test_item;
    std::string barcode_value;
    std::string patient_name;
    std::string specimen_type;
    std::string department;
    std::string patient_id;
    std::string timestamp;
};

std::wstring configured_barcode_printer_name();
const wchar_t* default_barcode_printer_name();
std::wstring configured_zebra_chinese_font();
const wchar_t* default_zebra_chinese_font();
const wchar_t* zebra_simsun_font();
const wchar_t* zebra_csong_font();
std::wstring normalize_zebra_chinese_font(const std::wstring& font);
std::wstring barcode_label_details(const BarcodeLabelPayload& payload);
bool barcode_label_printing_available();
void print_barcode_label(const BarcodeLabelPayload& payload, const std::wstring& printer_name);

}  // namespace search
