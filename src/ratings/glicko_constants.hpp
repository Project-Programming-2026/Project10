#pragma once

#include <cmath>

namespace project10::ratings::glicko_constants {

inline const double pi = std::atan(1.0) * 4.0;
// 底が10のレート尺度を自然対数の尺度へ変換する係数。
inline const double q = std::log(10.0) / 400.0;

}  // namespace project10::ratings::glicko_constants
