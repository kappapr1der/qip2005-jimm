// Compiled only in a generated, isolated tree with QIP_AGENT_TESTING and an ephemeral key.
// The shipped executable has neither this entry point nor the loopback transport.
#define wWinMain originalAgentEntry
#ifdef QIP_E2E_LEGACY_WORKER
#include "../updater/src/legacy_main.cpp"
#else
#include "../updater/src/main.cpp"
#endif
#undef wWinMain
#include <iostream>
#ifdef QIP_E2E_LEGACY_WORKER
static void finalizeInstalledProfile(const fs::path& app){prepareProfile(app);}
static void finalizeFromInstaller(const fs::path&){throw std::runtime_error("legacy_installer_mode_unsupported");}
#endif

class LoopbackTransport final:public Transport {
    INTERNET_PORT port;
    Response fetch(const std::string& url,size_t limit,const fs::path* destination){
        auto path=url.ends_with("/releases/latest")?L"/api/latest":L"/assets/"+wide(url.substr(url.find_last_of('/')+1));
        Internet session(WinHttpOpen(L"QIP-E2E-test-only",WINHTTP_ACCESS_TYPE_NO_PROXY,nullptr,nullptr,0));require(session.h,"test_http");
        WinHttpSetTimeouts(session.h,3000,3000,5000,5000);
        Internet connection(WinHttpConnect(session.h,L"127.0.0.1",port,0));require(connection.h,"test_http");
        Internet request(WinHttpOpenRequest(connection.h,L"GET",path.c_str(),nullptr,nullptr,nullptr,0));require(request.h,"test_http");
        DWORD disable=WINHTTP_DISABLE_REDIRECTS;WinHttpSetOption(request.h,WINHTTP_OPTION_DISABLE_FEATURE,&disable,sizeof(disable));
        require(WinHttpSendRequest(request.h,nullptr,0,nullptr,0,0,0) && WinHttpReceiveResponse(request.h,nullptr),"test_http");
        DWORD status=0,size=sizeof(status);require(WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr),"test_http");
        std::string body;char block[65536];DWORD count=0;size_t total=0;std::ofstream out;
        if(destination){fs::create_directories(destination->parent_path());out.open(*destination,std::ios::binary);require(bool(out),"test_download");}
        do{require(WinHttpReadData(request.h,block,sizeof(block),&count),"test_http");total+=count;require(total<=limit,"download_limit");if(destination)out.write(block,count);else body.append(block,count);}while(count);
        if(destination){out.flush();require(bool(out),"test_download");}return{(int)status,body};
    }
public:
    explicit LoopbackTransport(INTERNET_PORT p):port(p){require(p>0,"test_port");}
    Response get(const std::string& url,size_t limit) override{return fetch(url,limit,nullptr);}
    void download(const std::string& url,const fs::path& target,uint64_t limit)override{require(fetch(url,(size_t)limit,&target).status==200,"download_status");}
};
static std::wstring environment(const wchar_t* name){wchar_t value[32768];DWORD n=GetEnvironmentVariableW(name,value,32768);require(n && n<32768,"test_environment");return {value,n};}
int wmain(int argc,wchar_t** argv){
    try{
        testDataRoot=environment(L"QIP_E2E_STATE");noLinks(testDataRoot);
        LoopbackTransport http((INTERNET_PORT)std::stoi(environment(L"QIP_E2E_PORT")));
        std::wstring mode=argc>1?argv[1]:L"--launch";
        if(mode==L"--worker"){require(argc==3,"arguments");worker(utf8(argv[2]));return 0;}
        auto app=argc>2?validatedApp(argv[2]):validatedApp(modulePath().parent_path());
        json output;
        if(mode==L"--cycle")output={{"scheduled",checkCycle(app,true,&http)}};
        else if(mode==L"--recover")output={{"recover_started",recoverPending(app)}};
        else if(mode==L"--prime-crash"){
            require(argc==5,"arguments");auto raw=read(argv[3],128*1024),sig=read(argv[4],2048);auto m=authenticate(policy(),raw,sig,now());
            auto id=uniqueId();auto dir=dataRoot()/L"transactions"/wide(id);fs::create_directories(dir);
            fs::copy_file(modulePath(),dir/L"QipUpdateWorker.exe");writeAtomic(dir/L"manifest.json",raw);writeAtomic(dir/L"manifest.sig",sig);
            writeAtomic(dir/L"job.json",json{{"app",utf8(app.wstring())},{"id",id},{"automatic",true},{"installer","unused-during-recovery"}}.dump());
            WindowsIntegration integration(app);Snapshot(dataRoot()/L"backups"/wide(id)).create(app,{},integration);
            writeAtomic(dir/L"phase","installing");writeAtomic(app/L"installed-version.json",json{{"version",m.at("version")}}.dump());
            writeAtomic(app/L"Users"/L"fixture"/L"History"/L"messages.txt","interrupted-test-installation");
            output={{"journal_created",true}};
        }
        else if(mode==L"--launch") {launch(app);output={{"launched",true}};}
        else if(mode==L"--prepare-profile") {Mutex lock(lockName("Install",app),true);require(lock.acquired(),"application_busy");finalizeInstalledProfile(app);output={{"prepared",true}};}
        else if(mode==L"--prepare-profile-from-installer") {
            wchar_t fail[2]{};if(GetEnvironmentVariableW(L"QIP_E2E_FAIL_FINALIZE",fail,2)){
                writeAtomic(app/L"Users"/L"fixture"/L"History"/L"messages.txt","fixture finalization failure");
                throw std::runtime_error("fixture_finalize_failure");
            }
            finalizeFromInstaller(app);output={{"prepared",true}};
        }
        else if(mode==L"--monitor") {monitor(app,false,&http);return 0;}
        else if(mode==L"--busy")output={{"busy",appBusy(app)}};
        else throw std::runtime_error("test_mode");
        std::cout<<output.dump()<<'\n';return 0;
    }catch(const std::exception& error){logFailure(error);std::string code=error.what();std::cout<<json{{"error",std::regex_match(code,std::regex("[a-z_]{1,64}"))?code:"test_io_error"}}.dump()<<'\n';return 1;}
}
