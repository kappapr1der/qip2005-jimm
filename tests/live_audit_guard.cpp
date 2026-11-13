// Local audit utility. Operates only on the explicitly named working installation.
#include "../updater/src/transaction.hpp"
#include <iostream>
using namespace qip;
static json inventory(const fs::path& root){json result=json::object();for(auto& entry:fs::recursive_directory_iterator(root)){noLinks(entry.path());if(entry.is_regular_file())result[utf8(fs::relative(entry.path(),root).generic_wstring())]=hashFile(entry.path());}return result;}
int wmain(int argc,wchar_t** argv){
    try{
        require(argc==4,"arguments");auto app=fs::absolute(argv[2]).lexically_normal(),dir=fs::absolute(argv[3]).lexically_normal();
        require(!appBusy(app),"working_application_busy");WindowsIntegration integration(app);noLinks(app);noLinks(dir);
        if(std::wstring(argv[1])==L"capture"){
            require(!fs::exists(dir/L"integration.json"),"audit_backup_exists");fs::create_directories(dir);
            writeAtomic(dir/L"integration.json",integration.capture(dir).dump());writeAtomic(dir/L"working-hashes.json",inventory(app).dump());
            std::cout<<json{{"working_files",inventory(app).size()},{"captured",true}}.dump()<<'\n';
        }else if(std::wstring(argv[1])==L"restore"){
            integration.restore(dir,readJson(dir/L"integration.json"));auto before=readJson(dir/L"working-hashes.json"),after=inventory(app);
            require(before==after,"working_files_changed");
            auto expected=readJson(dir/L"integration.json"),current=integration.capture(dir/L"post-restore");
            require(expected==current,"integration_restore_mismatch");
            std::cout<<json{{"working_files",after.size()},{"working_files_unchanged",true},{"integration_restored",true}}.dump()<<'\n';
        }else throw std::runtime_error("arguments");return 0;
    }catch(const std::exception& error){std::string code=error.what();std::cerr<<(std::regex_match(code,std::regex("[a-z_]{1,64}"))?code:"audit_guard_failed")<<'\n';return 1;}
}
