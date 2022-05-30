#include <set>
#include <map>
#include <list>

#include "binary.hpp"

struct Example {
    // String
    std::string str = "Hello";

    // Containers
    std::vector<std::string> string_vector = {"A", "simple", "serialization", "library"};
    std::list<char> char_list = {'a', 'b', 'c'};
    std::set<double> double_set = {0.1, 0.2, 0.3, 0.4};
    std::map<std::string, long> long_map = {{"One", 1}, {"Two", 2}, {"Three", 3}};

    // Trivial aggregate (embedded)
    struct Trivial{
        int a = 5;
        bool b = true;
        char c = 'c';
        double d = 3.14;
    } trivial;

    // Vector of trivial aggregates
    std::vector<Trivial> trivials = {{}, {}, {}};

    struct NonTrivial {
        std::optional<std::string> empty;
        std::optional<std::string> str = "Optional";
        std::pair<int, std::string> pair = {5, "Five"};
        std::tuple<int, double, bool> tuple = {10, 3.14, false};
    };

    // Non-trivial aggregate (in a `unique_ptr`)
    std::unique_ptr<NonTrivial> ptr = std::make_unique<NonTrivial>();
    std::unique_ptr<int> empty_ptr;
};

struct Node {
    int value = 0;
    std::unique_ptr<Node> next;

    // Make linked list with given number of nodes.
    static auto make(int count) {
        std::unique_ptr<Node> p;
        for (int i = 0; i < 10; ++i) {
            auto n = new Node{i, std::move(p)};
            p = std::unique_ptr<Node>(n);
        }
        return p;
    }
};


int main() {
    // Test 1: Comprehensive
    Example example;

    serialization::binary::dump(example, "example.dat");
    auto loaded_1 = serialization::binary::load<Example>("example.dat");

    serialization::xml::dump(example, "example.xml");
    auto loaded_2 = serialization::xml::load<Example>("example.xml");

    serialization::xml::dump<true>(example, "example_base64.xml");
    auto loaded_3 = serialization::xml::load<Example, true>("example_base64.xml");

    // Test 2: Linked list
    using LinkedList = Node;
    auto list = LinkedList::make(10);

    serialization::xml::dump(list, "list.xml");
    auto loaded_list = serialization::xml::load<std::unique_ptr<LinkedList>>("list.xml");

    return 0;
}
