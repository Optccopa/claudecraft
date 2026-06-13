#include "app/Application.hpp"
#include "core/Log.hpp"

#include <exception>

int main() {
    try { 
        cc::Application app;
        app.run();
    } catch (const std::exception& e) {
        cc::logError(e.what());
        return 1;
    }
    return 0;
}
