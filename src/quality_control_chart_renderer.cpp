#include "quality_control_chart_renderer.h"

#ifdef _WIN32

#include "search_text.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace qc {
namespace {

struct NumericPoint {
    const ChartPoint* point = nullptr;
    size_t source_index = 0;
};

void fillRect(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void drawLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width = 1, int style = PS_SOLID) {
    HPEN pen = CreatePen(style, width, color);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void drawTextInRect(HDC dc, const RECT& rect, const std::wstring& text, UINT format) {
    RECT copy = rect;
    DrawTextW(dc, text.c_str(), -1, &copy, format);
}

std::wstring formatNumber(double value, int precision = 2) {
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(precision) << value;
    return ss.str();
}

std::wstring shortTimeLabel(const std::string& text) {
    if (text.size() >= 16) {
        return search::utf8_to_wide(text.substr(5, 11));
    }
    return search::utf8_to_wide(text);
}

int xForIndex(const RECT& plot, size_t index, size_t total) {
    if (total <= 1) return (plot.left + plot.right) / 2;
    const double ratio = static_cast<double>(index) / static_cast<double>(total - 1);
    return plot.left + static_cast<int>(ratio * (plot.right - plot.left));
}

int yForValue(const RECT& plot, double value, double minValue, double maxValue) {
    if (maxValue <= minValue) return (plot.top + plot.bottom) / 2;
    const double ratio = (value - minValue) / (maxValue - minValue);
    return plot.bottom - static_cast<int>(ratio * (plot.bottom - plot.top));
}

COLORREF pointColor(const ChartPoint& point) {
    if (point.qc_status == "out_of_control") return RGB(205, 45, 45);
    if (point.qc_status == "warning") return RGB(220, 135, 20);
    return RGB(30, 110, 65);
}

void drawReferenceLine(HDC dc, const RECT& plot, double yValue, double minValue, double maxValue,
                       const wchar_t* label, COLORREF color, int style = PS_DOT) {
    const int y = yForValue(plot, yValue, minValue, maxValue);
    drawLine(dc, plot.left, y, plot.right, y, color, 1, style);
    RECT labelRect{plot.right + 6, y - 10, plot.right + 70, y + 10};
    drawTextInRect(dc, labelRect, label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

}  // namespace

void draw_levey_jennings_chart(HDC dc, const RECT& rect, const ChartSeries& series,
                               int selected_index,
                               std::vector<ChartHitPoint>* hit_points) {
    if (hit_points) hit_points->clear();
    fillRect(dc, rect, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(40, 45, 52));

    RECT titleRect{rect.left + 12, rect.top + 8, rect.right - 12, rect.top + 32};
    HFONT titleFont = CreateFontW(18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    HGDIOBJ oldFont = SelectObject(dc, titleFont);
    drawTextInRect(dc, titleRect, search::utf8_to_wide(series.title), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
    DeleteObject(titleFont);

    RECT subRect{rect.left + 12, rect.top + 34, rect.right - 12, rect.top + 56};
    drawTextInRect(dc, subRect, search::utf8_to_wide(series.subtitle), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    std::vector<NumericPoint> points;
    points.reserve(series.points.size());
    for (size_t i = 0; i < series.points.size(); ++i) {
        const auto& point = series.points[i];
        if (point.has_value) points.push_back({&point, point.point_index});
    }
    if (points.empty()) {
        RECT emptyRect{rect.left, rect.top + 60, rect.right, rect.bottom};
        drawTextInRect(dc, emptyRect, L"当前分组没有可绘制的数值结果。", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    double minValue = points.front().point->value;
    double maxValue = points.front().point->value;
    for (const auto& point : points) {
        minValue = std::min(minValue, point.point->value);
        maxValue = std::max(maxValue, point.point->value);
    }
    if (series.has_sd && series.sd > 0.0) {
        minValue = std::min(minValue, series.mean - 3.0 * series.sd);
        maxValue = std::max(maxValue, series.mean + 3.0 * series.sd);
    } else if (series.has_mean) {
        minValue = std::min(minValue, series.mean);
        maxValue = std::max(maxValue, series.mean);
    }
    if (maxValue <= minValue) {
        minValue -= 1.0;
        maxValue += 1.0;
    }
    const double padding = (maxValue - minValue) * 0.08;
    minValue -= padding;
    maxValue += padding;

    RECT plot{rect.left + 70, rect.top + 72, rect.right - 78, rect.bottom - 48};
    if (plot.right <= plot.left + 40 || plot.bottom <= plot.top + 40) return;

    fillRect(dc, plot, RGB(248, 250, 252));
    for (int i = 0; i <= 4; ++i) {
        const double ratio = static_cast<double>(i) / 4.0;
        const int y = plot.bottom - static_cast<int>(ratio * (plot.bottom - plot.top));
        drawLine(dc, plot.left, y, plot.right, y, RGB(226, 232, 240), 1, PS_SOLID);
        const double value = minValue + ratio * (maxValue - minValue);
        RECT tickRect{rect.left + 8, y - 10, plot.left - 8, y + 10};
        drawTextInRect(dc, tickRect, formatNumber(value), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    drawLine(dc, plot.left, plot.top, plot.left, plot.bottom, RGB(120, 130, 145));
    drawLine(dc, plot.left, plot.bottom, plot.right, plot.bottom, RGB(120, 130, 145));

    if (series.has_mean) {
        drawReferenceLine(dc, plot, series.mean, minValue, maxValue, L"Mean", RGB(40, 95, 170), PS_SOLID);
    }
    if (series.has_sd && series.sd > 0.0) {
        drawReferenceLine(dc, plot, series.mean + series.sd, minValue, maxValue, L"+1SD", RGB(95, 150, 210));
        drawReferenceLine(dc, plot, series.mean - series.sd, minValue, maxValue, L"-1SD", RGB(95, 150, 210));
        drawReferenceLine(dc, plot, series.mean + 2.0 * series.sd, minValue, maxValue, L"+2SD", RGB(210, 145, 60));
        drawReferenceLine(dc, plot, series.mean - 2.0 * series.sd, minValue, maxValue, L"-2SD", RGB(210, 145, 60));
        drawReferenceLine(dc, plot, series.mean + 3.0 * series.sd, minValue, maxValue, L"+3SD", RGB(200, 70, 70));
        drawReferenceLine(dc, plot, series.mean - 3.0 * series.sd, minValue, maxValue, L"-3SD", RGB(200, 70, 70));
    }

    HPEN linePen = CreatePen(PS_SOLID, 2, RGB(70, 100, 140));
    HGDIOBJ oldPen = SelectObject(dc, linePen);
    bool started = false;
    for (size_t i = 0; i < points.size(); ++i) {
        const int x = xForIndex(plot, i, points.size());
        const int y = yForValue(plot, points[i].point->value, minValue, maxValue);
        if (!started) {
            MoveToEx(dc, x, y, nullptr);
            started = true;
        } else {
            LineTo(dc, x, y);
        }
    }
    SelectObject(dc, oldPen);
    DeleteObject(linePen);

    for (size_t i = 0; i < points.size(); ++i) {
        const int x = xForIndex(plot, i, points.size());
        const int y = yForValue(plot, points[i].point->value, minValue, maxValue);
        const bool selected = selected_index >= 0 && static_cast<size_t>(selected_index) == points[i].source_index;
        if (hit_points) {
            hit_points->push_back({RECT{x - 7, y - 7, x + 8, y + 8}, points[i].source_index});
        }
        if (selected) {
            HBRUSH highlight = CreateSolidBrush(RGB(255, 245, 180));
            HGDIOBJ oldHighlight = SelectObject(dc, highlight);
            Ellipse(dc, x - 9, y - 9, x + 10, y + 10);
            SelectObject(dc, oldHighlight);
            DeleteObject(highlight);
        }
        HBRUSH brush = CreateSolidBrush(pointColor(*points[i].point));
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        Ellipse(dc, x - 4, y - 4, x + 5, y + 5);
        SelectObject(dc, oldBrush);
        DeleteObject(brush);
        if (selected) {
            drawLine(dc, x - 10, y, x + 10, y, RGB(70, 70, 70), 1, PS_SOLID);
            drawLine(dc, x, y - 10, x, y + 10, RGB(70, 70, 70), 1, PS_SOLID);
        }
    }

    const size_t lastLabel = points.size() - 1;
    const size_t midLabel = points.size() / 2;
    for (size_t index : {size_t{0}, midLabel, lastLabel}) {
        const int x = xForIndex(plot, index, points.size());
        RECT labelRect{x - 55, plot.bottom + 8, x + 55, plot.bottom + 30};
        drawTextInRect(dc, labelRect, shortTimeLabel(points[index].point->time), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

}  // namespace qc

#endif
