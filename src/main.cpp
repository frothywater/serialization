#include <iostream>
#include <set>
#include <list>
#include <cassert>

#include "binary.hpp"

using namespace binary;

struct example {
    int a = 5;
    double b = 10.0;
    bool c = true;
    char d = 'd';

    std::string str = "Hello";
    std::vector<int> int_vector = {1, 2, 3, 4, 5};
    std::list<char> char_list = {'a', 'b', 'c'};
    std::set<double> double_set = {0.1, 0.2, 0.3, 0.5};

    struct {
        int a = 5;
        double b = 10.0;
        bool c = true;
        char d = 'd';
    } trivial;


    struct {
        std::vector<std::string> int_vector = {"A", "simple", "serialization", "library"};
    } nontrivial;
};

int main() {
    example original;

    auto data = dump(original);
    auto loaded = load<example>(data);

    return 0;
}
