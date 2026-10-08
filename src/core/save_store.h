#pragma once
#include <string>
namespace gtabr::save_store {
// Legacy v3/v4 saves remain readable; v5 requires a checksum and mandatory fields.
bool valid(const std::string& data);
bool read(const std::string& path, std::string& data, bool* recovered = nullptr);
bool write(const std::string& path, const std::string& payload);
}
