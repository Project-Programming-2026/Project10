#pragma once

#include <string>
#include <vector>

void writePlayers(const std::vector<int>& players, const std::string& outputPath);
void writeLatentParameters(
  const std::vector<double>& parameters,
  const std::string& outputPath
);

void writeGroundTruth(
  const std::vector<double>& parameters,
  double performanceStd,
  const std::string& outputPath
);
