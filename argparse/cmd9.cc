#include <argparse.h>
#include <string>
#include <list>
#include <iterator>

using namespace argparse;

int main(int argc, char*const*const argv) {
    ArgumentParser           cmd;
    char                     cnt;
    unsigned int             verbose;
    std::list<std::string>   experiments;
    auto                     experimentor = std::back_inserter(experiments);

    cmd.add(long_name("help"), short_name('h'), print_help(),
            docstring("Prints help and exits succesfully"));

    cmd.add(short_name('f'), store_value<std::string>(), exactly(1));

    cmd.add(set_default(3.14f), long_name("threshold"),
            maximum_value(7.f), store_value<float>(), at_least(2));

    cmd.add(short_name('v'), count(), docstring("verbosity level - add more v's to increase"));

    cmd.add(long_name("exp"), collect_into(experimentor),
            minimum_size(4), match("[a-zA-Z]{2}[0-9]{3}[a-zA-Z]?"));

    cmd.add(collect_into(experiments), match("[a-zA-Z]{2}[0-9]{3}[a-zA-Z]?"));

    cmd.add(short_name('c'), count_into(cnt));
    //cmd.add(short_name('C'), count_into(experiments));

    cmd.parse(argc, argv);

    std::cout << ">>>>>>>>>>>>>>>" << std::endl;
    float         threshold;
    std::string   f;

    cmd.get("f", f);
    cmd.get("threshold", threshold);
    cmd.get("v", verbose);

    std::cout << "got '-f' = " << f << std::endl;
    std::cout << "got '--threshold' = " << threshold << std::endl;
    std::cout << "verbosity level = " << verbose << std::endl;
    std::cout << "cnt = " << (int)cnt << std::endl;

    for(auto const& e: experiments)
        std::cout << "Experiment: " << e << std::endl;
    return 0;
}

