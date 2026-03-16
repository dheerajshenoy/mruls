#include "mruls.hpp"

void
init_args(argparse::ArgumentParser &parser)
{
    parser.add_argument("-c", "--config")
        .help("Path to the configuration file")
        .metavar("FILE_PATH");

    parser.add_argument("-u", "--user")
        .help("Username to view jobs for")
        .nargs(1)
        .metavar("USERNAME");
}

int
main(int argc, char *argv[])
{
    argparse::ArgumentParser parser(APP_NAME, APP_VERSION);
    init_args(parser);
    try
    {
        parser.parse_args(argc, argv);
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    mruls m(parser);
    m.loop();
    return 0;
}
