#include "lalrbld.h"

#include <cstring>

int main(int argc, char** argv) {
    try {
        LRBuilder lr_builder;
        int res = lr_builder.loadGrammar(std::cin);
        if (res != 0) {
            std::cerr << std::endl << lr_builder.getErrorString() << std::endl;
            return res;
        }
        lr_builder.buildAnalizer();
        return 0;
    } catch (std::exception& e) { std::cerr << "****Exception catched: " << e.what() << "." << std::endl; }
    return -1;
}
