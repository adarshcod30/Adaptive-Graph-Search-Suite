#include <string>

#include "microtest.hpp"

int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    return mt::run(filter);
}
