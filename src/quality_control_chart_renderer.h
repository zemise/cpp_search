#pragma once

#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace qc {

struct ChartPoint {
    std::string time;
    std::string sample_no;
    std::string rep_no;
    std::string item_name;
    std::string level;
    std::string result_text;
    std::string unit;
    std::string qc_status;
    std::string qc_rules;
    double value = 0.0;
    double z = 0.0;
    bool has_value = false;
    bool has_z = false;
    size_t point_index = 0;
};

struct ChartSeries {
    std::string title;
    std::string subtitle;
    std::string unit;
    double mean = 0.0;
    double sd = 0.0;
    bool has_mean = false;
    bool has_sd = false;
    std::vector<ChartPoint> points;
};

#ifdef _WIN32
struct ChartHitPoint {
    RECT bounds{};
    size_t point_index = 0;
};

void draw_levey_jennings_chart(HDC dc, const RECT& rect, const ChartSeries& series,
                               int selected_index = -1,
                               std::vector<ChartHitPoint>* hit_points = nullptr);
#endif

}  // namespace qc
