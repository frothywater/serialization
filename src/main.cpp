#include <iostream>
#include <set>
#include <list>

#include "binary.hpp"

struct example {
    int a = 5;
    double b = 10.0;
    bool c = true;
    char d = 'd';

    std::string str = "Hello";
    std::vector<int> int_vector = {1, 2, 3, 4, 5};
    std::list<char> char_list = {'a', 'b', 'c'};
    std::set<double> double_set = {0.1, 0.2, 0.3, 0.5};

    struct trivial {
        int a = 5;
        double b = 10.0;
        bool c = true;
        char d = 'd';
    } t;


    struct nontrivial {
        std::vector<std::string> int_vector = {"A", "simple", "serialization", "library"};

        std::optional<std::string> str = "Optional";
        std::pair<int, std::string> pair = {5, "Five"};
        std::tuple<int, double, bool> tuple = {10, 3.2, false};

        trivial t;
    } n;

    std::unique_ptr<nontrivial> p = std::make_unique<nontrivial>();
};

struct Node {
    int value = 0;
    std::unique_ptr<Node> next;

    static auto make_list(int count) {
        std::unique_ptr<Node> p;
        for (int i = 0; i < 10; ++i) {
            auto n = new Node{i, std::move(p)};
            p = std::unique_ptr<Node>(n);
        }
        return p;
    }
};

int main() {
//    example original;
    auto original = Node::make_list(10);

    serialization::xml::dump<>(original, "test.xml");
    auto loaded = serialization::xml::load<decltype(original)>("test.xml");

    serialization::binary::dump<>(original, "test.dat");
    auto loaded_bin = serialization::binary::load<decltype(original)>("test.dat");

    return 0;
}
