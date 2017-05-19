#include <argparse.h>
#include <string>
#include <list>
#include <iterator>

namespace AP = argparse;

using sort_function = std::function<bool(int,int)>;


int main(int argc, char*const*const argv) {
    AP::ArgumentParser       cmd( AP::docstring("Sample program to demonstrate argparse"),
                                  AP::version("$Id: $") );
    char                     cnt;
    unsigned int             verbose;
    std::list<std::string>   experiments;
    std::list<int>           integers;
    auto                     experimentor = std::back_inserter(experiments);

    cmd.add(AP::long_name("help"), AP::short_name('h'), AP::print_help(),
            AP::docstring("Prints help and exits succesfully"));

    cmd.add(AP::long_name("version"), AP::print_version(),
            AP::docstring("Prints version and exits succesfully"));

    cmd.add(AP::short_name('f'), AP::store_value<std::string>(), AP::exactly(1),
            AP::is_member_of({"aap", "noot", "mies"}) );

    cmd.add(AP::set_default(3.14f), AP::long_name("threshold"),
            AP::maximum_value(7.f), AP::store_value<float>(), AP::minimum_value(0.f));

    cmd.add(AP::short_name('v'), AP::count(), AP::docstring("verbosity level - add more v's to increase"));

    cmd.add(AP::long_name("exp"), AP::collect_into(experimentor),
            AP::minimum_size(4), AP::match("[a-zA-Z]{2}[0-9]{3}[a-zA-Z]?"));

    //cmd.add(AP::collect_into(experiments), AP::match("[a-zA-Z]{2}[0-9]{3}[a-zA-Z]?"),
    cmd.add(AP::collect_into(integers), AP::minimum_value(3), AP::is_member_of({3, 4, 5}),
            AP::at_least(2));

    cmd.add(AP::short_name('c'), AP::count_into(cnt));

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

