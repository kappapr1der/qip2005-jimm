#include "../updater/src/transaction.hpp"
#include <iostream>
using namespace qip;
class MockHttp:public Transport {
    json fixture;
public:
    int requests=0,downloads=0;
    explicit MockHttp(json f):fixture(std::move(f)){}
    Response get(const std::string& url,size_t limit) override {
        require(!fixture.value("connection_error",false),"network_error");
        ++requests;require(fixture.at("responses").contains(url),"unexpected_request");auto r=fixture.at("responses").at(url);
        std::string body=r.at("body");require(body.size()<=limit,"download_limit");return {r.at("status"),body};
    }
    void download(const std::string&,const fs::path& destination,uint64_t limit) override {
        ++downloads;std::string body=fixture.at("installer_bytes");require(body.size()<=limit,"download_limit");writeAtomic(destination,body);
    }
};
class MockIntegration:public Integration {
public:
    int state=1;
    json capture(const fs::path&) override{return {{"state",state}};}
    void restore(const fs::path&,const json& snapshot) override{state=snapshot.at("state");}
};
class MockInstaller:public Installer {
    json fixture;MockIntegration& integration;
public:
    int calls=0;
    MockInstaller(json f,MockIntegration& i):fixture(std::move(f)),integration(i){}
    bool busy(const fs::path&) override{return fixture.value("busy",false);}
    DWORD run(const fs::path&,const fs::path& app) override{
        ++calls;integration.state=2;
        for(auto it=fixture.at("installed_files").begin();it!=fixture.at("installed_files").end();++it)writeAtomic(app/wide(it.key()),it.value().get<std::string>());
        if(fixture.value("fail",false)){
            writeAtomic(app/L"qip.exe","broken client");
            for(auto& f:fs::recursive_directory_iterator(app/L"Users")){if(f.is_regular_file()){writeAtomic(f.path(),"damaged private test data");break;}}
            return 1;
        }
        if(fixture.value("post_verify_failure",false))writeAtomic(app/L"QipUpdateAgent.exe","corrupted installed agent");
        return 0;
    }
};
static std::map<std::string,std::string> fileHashes(const fs::path& dir){
    std::map<std::string,std::string> r;for(auto& entry:fs::recursive_directory_iterator(dir)){noLinks(entry.path());if(entry.is_regular_file())r[utf8(fs::relative(entry.path(),dir).generic_wstring())]=hashFile(entry.path());}return r;
}
int wmain(int argc,wchar_t** argv){
    json output=json::object();
    try{
        require(argc==3,"arguments");std::string mode=utf8(argv[1]);auto f=json::parse(read(argv[2],2*1024*1024));
        if(mode=="semver")output["comparison"]=SemVer::parse(f.at("a").get<std::string>()).compare(SemVer::parse(f.at("b").get<std::string>()));
        else if(mode=="busy")output["busy"]=appBusy(wide(f.at("app").get<std::string>()));
        else if(mode=="spawn"){
            auto app=fs::path(wide(f.at("app").get<std::string>()));auto exe=fs::path(wide(f.at("exe").get<std::string>()));
            auto start=std::chrono::steady_clock::now();process(exe,L"",false,app);
            output["elapsed_ms"]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
            output["child_running"]=appBusy(app);std::this_thread::sleep_for(std::chrono::seconds(5));output["child_closed"]=!appBusy(app);
        }
        else if(mode=="due")output["due"]=due(f.at("now"),f.at("last"),f.at("enabled"));
        else{
            Policy p{f.at("repository"),f.at("modulus"),f.at("qip_hash"),f.at("docking_hash")};MockHttp http(f);
            if(!f.value("manual",false) && !due(f.at("now"),f.value("last",0ULL),f.value("enabled",true))){output["result"]="throttled";output["requests"]=0;}
            else{
                auto offer=checkUpdate(http,p,f.at("current"),f.value("highest",std::string()),f.at("now"));
                if(!offer)output["result"]="no_update";
                else{
                    auto setup=fs::path(wide(f.at("setup").get<std::string>()));bool prompt=false;
                    bool approved=downloadAndApprove(http,*offer,setup,f.value("automatic",false),[&]{prompt=true;return f.value("consent",false);});output["prompted"]=prompt;
                    if(!approved)output["result"]="declined";
                    else if(mode=="check")output["result"]="approved";
                    else{
                        auto app=fs::path(wide(f.at("app").get<std::string>()));auto before=fileHashes(app/L"Users");MockIntegration integration;MockInstaller installer(f,integration);
                        auto backup=fs::path(wide(f.at("backup").get<std::string>()));auto phase=fs::path(wide(f.at("phase").get<std::string>()));
                        auto result=apply(installer,integration,app,setup,backup,phase,{},*offer);
                        output["result"]=result==ApplyResult::installed?"installed":result==ApplyResult::busy?"busy":"rolled_back";
                        output["installer_calls"]=installer.calls;output["user_data_preserved"]=fileHashes(app/L"Users")==before;
                        output["integration_state"]=integration.state;output["backup_exists"]=fs::exists(backup/L"snapshot.json");
                        output["current_after"]=installedVersion(app);output["qip_unchanged"]=hashFile(app/L"qip.exe")==p.qipHash;
                        if(mode=="recover" && result==ApplyResult::installed){Snapshot(backup).restore(app,integration,offer->manifest.at("files"));output["recovered"]=installedVersion(app)==f.at("current").get<std::string>() && fileHashes(app/L"Users")==before && integration.state==1;}
                    }
                }
                output["requests"]=http.requests;output["downloads"]=http.downloads;
            }
        }
    }catch(const std::exception& e){std::string code=e.what();output["error"]=std::regex_match(code,std::regex("[a-z_]{1,64}"))?code:"io_or_json_error";}
    std::cout<<output.dump()<<"\n";return 0;
}
