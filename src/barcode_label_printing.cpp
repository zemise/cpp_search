#include "barcode_label_printing.h"

#include "app_settings_io.h"
#include "search_text.h"

#if defined(LIS_HAS_LABELPRINT)
#include "labelprint/labelprint.h"
#endif

#include <stdexcept>

namespace search {
namespace {

constexpr const wchar_t* DEFAULT_BARCODE_PRINTER_NAME = L"Xprinter XP-360B #2";

#if defined(LIS_HAS_LABELPRINT)
constexpr int ZEBRA_BARCODE_TEXT_WIDTH = 290;

labelprint::MedicalLabelLayout zebraMedicalLabelLayout() {
    labelprint::MedicalLabelLayout layout;
    const auto& zebra = labelprint::PrinterProfiles::zebra_zd888();
    layout.settings.darkness = zebra.darkness;
    layout.settings.printSpeed = zebra.speed;
    layout.settings.quantity = 1;
    layout.testItem.pos.x = layout.barcode.pos.x;
    layout.testItem.maxWidth = ZEBRA_BARCODE_TEXT_WIDTH;
    layout.testItem.align = labelprint::MedicalLabelTextAlign::Center;
    return layout;
}

bool printZebraMedicalLabelWithoutFallback(const labelprint::MedicalLabelData& data,
                                           const std::wstring& printer_name) {
    const auto model = labelprint::detectMedicalLabelPrinterModel(printer_name);
    if (model != labelprint::MedicalLabelPrinterModel::ZebraZd888) {
        return false;
    }

    labelprint::PrinterProfile profile = labelprint::PrinterProfiles::zebra_zd888();
    profile.nativeChineseFontFallback.clear();

    labelprint::LabelDocument doc = labelprint::buildMedicalLabel(data, zebraMedicalLabelLayout());
    labelprint::ZplBackend backend;
    labelprint::PrintJob job = backend.render(doc, profile);

    labelprint::PrinterConnection conn;
    conn.wideName = printer_name;
    labelprint::WindowsRawTransport transport;
    transport.send(job, conn);
    return true;
}
#endif

void append_detail_line(std::wstring& message, const wchar_t* label, const std::string& value) {
    message += label;
    message += utf8_to_wide(value);
    message += L"\n";
}

}  // namespace

const wchar_t* default_barcode_printer_name() {
    return DEFAULT_BARCODE_PRINTER_NAME;
}

std::wstring configured_barcode_printer_name() {
    std::wstring printer = load_module_str(L"RegularReport", L"BarcodePrinterName",
                                           default_barcode_printer_name());
    if (printer.empty()) {
        printer = default_barcode_printer_name();
    }
    return printer;
}

std::wstring barcode_label_details(const BarcodeLabelPayload& payload) {
    std::wstring message;
    append_detail_line(message, L"样本号：", payload.sample_no);
    append_detail_line(message, L"组合项目：", payload.test_item);
    append_detail_line(message, L"条码号：", payload.barcode_value);
    append_detail_line(message, L"姓名：", payload.patient_name);
    append_detail_line(message, L"标本：", payload.specimen_type);
    append_detail_line(message, L"开单日期：", payload.timestamp);
    append_detail_line(message, L"科室代码：", payload.department);
    message += L"病人号：";
    message += utf8_to_wide(payload.patient_id);
    return message;
}

bool barcode_label_printing_available() {
#if defined(LIS_HAS_LABELPRINT)
    return true;
#else
    return false;
#endif
}

void print_barcode_label(const BarcodeLabelPayload& payload, const std::wstring& printer_name) {
#if defined(LIS_HAS_LABELPRINT)
    labelprint::MedicalLabelData data;
    data.sampleNo = payload.sample_no;
    data.testItem = payload.test_item;
    data.barcodeValue = payload.barcode_value;
    data.patientName = payload.patient_name;
    data.specimenType = payload.specimen_type;
    data.department = payload.department;
    data.patientId = payload.patient_id;
    data.timestamp = payload.timestamp;

    if (printZebraMedicalLabelWithoutFallback(data, printer_name)) {
        return;
    }

    labelprint::MedicalLabelPrintOptions options;
    options.model = labelprint::MedicalLabelPrinterModel::Auto;
    options.fallbackModel = labelprint::MedicalLabelPrinterModel::XprinterXp360b;
    options.quantity = 1;
    labelprint::printMedicalLabel(printer_name, data, options);
#else
    (void)payload;
    (void)printer_name;
    throw std::runtime_error("构建时未找到 LabelPrint 项目");
#endif
}

}  // namespace search
