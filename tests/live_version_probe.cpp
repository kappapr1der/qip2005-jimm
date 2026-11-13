#include "../updater/src/native.hpp"
#include "../updater/src/generated_config.hpp"
#include <iostream>
int wmain(int argc,wchar_t** argv){
    try{
        qip::require(argc==2,"arguments");auto installed=qip::installedVersion(argv[1]);qip::Http http;
        qip::Policy policy{qip::Repository,qip::PublicModulus,qip::QipHash,qip::DockingHash};
        auto current=qip::checkUpdate(http,policy,installed,"",qip::now());
        auto older=qip::checkUpdate(http,policy,"1.1.0","",qip::now());
        auto future=qip::checkUpdate(http,policy,"1.2.0","",qip::now());
        qip::require(installed=="1.1.1" && !current && older && !future,"version_comparison");
        std::cout<<qip::json{{"installed_package_version",installed},{"same_release_no_update",!current},
            {"baseline_1_1_0_offered",older->manifest.at("version")},{"newer_local_1_2_0_not_downgraded",!future},
            {"actual_github_api_and_pinned_rsa",true}}.dump()<<'\n';return 0;
    }catch(const std::exception& error){std::string code=error.what();std::cerr<<(std::regex_match(code,std::regex("[a-z_]{1,64}"))?code:"probe_failed")<<'\n';return 1;}
}
