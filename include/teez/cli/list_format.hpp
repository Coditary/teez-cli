#pragma once

#include <iostream>
#include <string>
#include <vector>

namespace teez::cli {

struct ListOutputOptions {
    bool tree = false;
    bool json = false;
};

void write_test_list(const std::vector<std::string>& tests, const ListOutputOptions& options,
                     std::ostream& out = std::cout);

} // namespace teez::cli
