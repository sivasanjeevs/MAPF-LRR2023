#include "MAPFServer.h"
#include <boost/program_options.hpp>
#include <iostream>
#include <signal.h>

namespace po = boost::program_options;

std::unique_ptr<MAPFServer> server_ptr;

void sigint_handler(int a) {
    fprintf(stdout, "Stopping MAPF server...\n");
    if (server_ptr) {
        server_ptr->stop();
    }
    _exit(0);
}

int main(int argc, char **argv) {
    // Declare the supported options
    po::options_description desc("MAPF Server Options");
    desc.add_options()
        ("help", "produce help message")
        ("mapFile,m", po::value<std::string>()->required(), "map file path")
        ("configFile,c", po::value<std::string>()->required(), "config file path")
        ("port,p", po::value<int>()->default_value(8080), "server port")
        ("preprocessTimeLimit", po::value<int>()->default_value(30), "preprocessing time limit in seconds");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 1;
    }

    po::notify(vm);

    std::string map_file = vm["mapFile"].as<std::string>();
    std::string config_file = vm["configFile"].as<std::string>();
    int port = vm["port"].as<int>();
    int preprocess_time_limit = vm["preprocessTimeLimit"].as<int>();

    std::cout << "Starting MAPF Server..." << std::endl;
    std::cout << "Map file: " << map_file << std::endl;
    std::cout << "Config file: " << config_file << std::endl;
    std::cout << "Port: " << port << std::endl;

    // Create and initialize server
    server_ptr = std::make_unique<MAPFServer>(map_file, config_file, port);
    
    if (!server_ptr->initialize()) {
        std::cerr << "Failed to initialize MAPF Server" << std::endl;
        return 1;
    }

    // Set up signal handler
    signal(SIGINT, sigint_handler);

    // Start server
    std::cout << "Starting HTTP server..." << std::endl;
    server_ptr->run();

    return 0;
} 