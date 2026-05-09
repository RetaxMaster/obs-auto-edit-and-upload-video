#include "progress.h"
#include <fstream>
#include <regex>
#include <cmath>

double parse_duration_seconds(const std::string &line)
{
    std::smatch m;
    static const std::regex re(R"(Duration:\s+(\d+):(\d+):(\d+)\.(\d+))");
    if (!std::regex_search(line, m, re))
        return -1.0;

    double h    = std::stod(m[1].str());
    double mn   = std::stod(m[2].str());
    double s    = std::stod(m[3].str());
    double frac = std::stod(m[4].str()) / std::pow(10.0, m[4].str().size());
    return h * 3600.0 + mn * 60.0 + s + frac;
}

ProgressWriter::ProgressWriter(const std::string &path) : path_(path) {}

void ProgressWriter::write(int percent)
{
    std::ofstream f(path_, std::ios::trunc);
    f << percent;
}

void ProgressWriter::write_error(const std::string &message)
{
    std::ofstream f(path_, std::ios::trunc);
    f << "error: " << message;
}
