#include "search_core.h"

#include <cstdlib>
#include <iostream>

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    check(search::sql_datetime_diff_seconds(
              "2026-09-03 08:00:00", "2026-09-03 10:20:30") == 8430,
          "datetime difference should be accurate to seconds");
    check(search::format_duration_seconds_zh(8430) == "2小时20分钟30秒",
          "hour duration format");
    check(search::format_duration_seconds_zh(150) == "2分钟30秒",
          "minute duration format");
    check(search::format_duration_seconds_zh(30) == "30秒",
          "second duration format");
    check(search::format_duration_seconds_zh(0) == "0秒",
          "zero duration format");
    check(search::sql_datetime_diff_seconds("", "2026-09-03 10:20:30") < 0,
          "missing datetime should be rejected");
    check(search::sql_datetime_diff_seconds(
              "2026-09-03 10:20:30", "2026-09-03 08:00:00") < 0,
          "negative datetime difference should be rejected");
    check(search::format_duration_seconds_zh(-1).empty(),
          "invalid duration should be blank");

    std::cout << "search datetime tests passed\n";
    return 0;
}
