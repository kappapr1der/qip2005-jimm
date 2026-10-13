#pragma once
#include "core.hpp"
#include <winhttp.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <chrono>
#include <thread>

namespace qip {
struct Handle {
    HANDLE h=nullptr;
    explicit Handle(HANDLE value=nullptr):h(value){}
    Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
    ~Handle(){if(h && h!=INVALID_HANDLE_VALUE) CloseHandle(h);}
};
struct Internet {
    HINTERNET h=nullptr;
    explicit Internet(HINTERNET value=nullptr):h(value){}
    Internet(const Internet&)=delete;
    ~Internet(){if(h)WinHttpCloseHandle(h);}
};
inline uint64_t now() { return (uint64_t)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
inline fs::path modulePath() { wchar_t b[32768]; DWORD n=GetModuleFileNameW(nullptr,b,32768); require(n>0 && n<32768,"module_path"); return fs::path(std::wstring(b,n)); }
inline fs::path shellFolder(int id) { wchar_t b[MAX_PATH]; require(SHGetFolderPathW(nullptr,id,nullptr,SHGFP_TYPE_CURRENT,b)==S_OK,"shell_folder"); return fs::path(b); }
inline fs::path dataRoot() { return shellFolder(CSIDL_LOCAL_APPDATA)/L"QIP2005"/L"UpdateAgent"; }
inline bool samePath(const fs::path& a,const fs::path& b) { auto x=fs::absolute(a).lexically_normal().wstring(),y=fs::absolute(b).lexically_normal().wstring(); return CompareStringOrdinal(x.c_str(),(int)x.size(),y.c_str(),(int)y.size(),TRUE)==CSTR_EQUAL; }
inline bool within(const fs::path& child,const fs::path& parent) {
    auto a=fs::absolute(child).lexically_normal().wstring(),b=fs::absolute(parent).lexically_normal().wstring();
    while(b.size()>3 && (b.back()==L'\\'||b.back()==L'/'))b.pop_back();
    return a.size()>=b.size() && CompareStringOrdinal(a.c_str(),(int)b.size(),b.c_str(),(int)b.size(),TRUE)==CSTR_EQUAL &&
        (a.size()==b.size() || a[b.size()]==L'\\' || a[b.size()]==L'/');
}
inline void noLinks(const fs::path& p) {
    auto current=fs::absolute(p).lexically_normal();
    while(!current.empty()) { DWORD a=GetFileAttributesW(current.c_str()); require(a==INVALID_FILE_ATTRIBUTES || !(a&FILE_ATTRIBUTE_REPARSE_POINT),"reparse_point"); auto parent=current.parent_path(); if(parent==current)break; current=parent; }
}
inline void writeAtomic(const fs::path& file,const std::string& bytes) {
    noLinks(file); fs::create_directories(file.parent_path()); auto temp=file; temp+=L".tmp";
    noLinks(temp); std::ofstream out(temp,std::ios::binary|std::ios::trunc); require(bool(out),"file_write"); out.write(bytes.data(),bytes.size()); out.flush(); require(bool(out),"file_write"); out.close();
    require(MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"atomic_write");
}
inline json readJson(const fs::path& path,json fallback=json::object()) { if(!fs::exists(path))return fallback; return json::parse(read(path)); }
inline void log(const char* code) {
    try { auto p=dataRoot()/L"agent.log"; noLinks(p); fs::create_directories(p.parent_path());
        if(fs::exists(p) && fs::file_size(p)>1024*1024) { auto old=dataRoot()/L"agent.previous.log"; noLinks(old); MoveFileExW(p.c_str(),old.c_str(),MOVEFILE_REPLACE_EXISTING); }
        std::ofstream f(p,std::ios::app); f<<now()<<" "<<code<<"\n";
    } catch(...) {}
}
inline void logFailure(const std::exception& error) {
    std::string code=error.what();
    if(std::regex_match(code,std::regex("[a-z_]{1,64}")))log(code.c_str());
    else log("io_or_configuration_error");
}
inline std::wstring quote(const std::wstring& s) { require(s.find(L'"')==std::wstring::npos,"argument_quote"); return L"\""+s+L"\""; }
inline DWORD process(const fs::path& executable,const std::wstring& arguments,bool wait,const fs::path& working) {
    noLinks(executable); std::wstring command=quote(executable.wstring())+L" "+arguments;
    STARTUPINFOW start{}; start.cb=sizeof(start); PROCESS_INFORMATION info{};
    require(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,working.c_str(),&start,&info),"process_start");
    Handle child(info.hProcess),thread(info.hThread);
    if(!wait)return 0;
    require(WaitForSingleObject(child.h,INFINITE)==WAIT_OBJECT_0,"process_wait"); DWORD code; require(GetExitCodeProcess(child.h,&code),"process_exit"); return code;
}
class Mutex {
    Handle handle; bool held=false;
public:
    Mutex(const std::wstring& name,bool take):handle(CreateMutexW(nullptr,FALSE,name.c_str())) { require(handle.h!=nullptr,"mutex"); if(take){DWORD s=WaitForSingleObject(handle.h,0);held=s==WAIT_OBJECT_0||s==WAIT_ABANDONED;} }
    ~Mutex(){if(held)ReleaseMutex(handle.h);}
    bool acquired() const{return held;}
};
inline std::wstring lockName(const char* purpose,const fs::path& app) { auto s=app.wstring(); CharLowerBuffW(s.data(),(DWORD)s.size()); return L"Local\\QIP2005."+wide(purpose)+L"."+wide(hex(digest(utf8(s))).substr(0,32)); }
inline bool appBusy(const fs::path& app,DWORD except=0) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)); require(snapshot.h!=INVALID_HANDLE_VALUE,"process_snapshot");
    PROCESSENTRY32W entry{}; entry.dwSize=sizeof(entry); require(Process32FirstW(snapshot.h,&entry),"process_snapshot");
    do {
        if(entry.th32ProcessID==except)continue;
        std::wstring n=entry.szExeFile;
        if(_wcsicmp(n.c_str(),L"qip.exe") && _wcsicmp(n.c_str(),L"QipUpdateAgent.exe") && _wcsicmp(n.c_str(),L"QipLauncher.exe"))continue;
        Handle h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID));
        if(!h.h)return true;
        wchar_t path[32768]; DWORD length=32768;
        if(!QueryFullProcessImageNameW(h.h,0,path,&length))return true;
        if(samePath(fs::path(std::wstring(path,length)).parent_path(),app))return true;
    } while(Process32NextW(snapshot.h,&entry)); return false;
}
class Http final:public Transport {
    Internet session;
    Response fetch(const std::string& initial,size_t limit,const fs::path* destination) {
        std::string url=initial;
        for(int redirects=0;redirects<=5;++redirects) {
            auto text=wide(url); URL_COMPONENTSW u{}; u.dwStructSize=sizeof(u); u.dwHostNameLength=u.dwUrlPathLength=u.dwExtraInfoLength=u.dwUserNameLength=u.dwPasswordLength=(DWORD)-1;
            require(WinHttpCrackUrl(text.c_str(),(DWORD)text.size(),0,&u),"url");
            require(u.nScheme==INTERNET_SCHEME_HTTPS && u.nPort==443 && !u.dwUserNameLength && !u.dwPasswordLength,"https_required");
            std::wstring host(u.lpszHostName,u.dwHostNameLength),path(u.lpszUrlPath,u.dwUrlPathLength); if(u.dwExtraInfoLength)path.append(u.lpszExtraInfo,u.dwExtraInfoLength);
            require(host==L"api.github.com" || host==L"github.com" || host==L"release-assets.githubusercontent.com" || host==L"objects.githubusercontent.com","http_origin");
            Internet connection(WinHttpConnect(session.h,host.c_str(),443,0)); require(connection.h,"http_connect");
            Internet request(WinHttpOpenRequest(connection.h,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE)); require(request.h,"http_request");
            DWORD disable=WINHTTP_DISABLE_REDIRECTS; require(WinHttpSetOption(request.h,WINHTTP_OPTION_DISABLE_FEATURE,&disable,sizeof(disable)),"http_options");
            DWORD protocols=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2|WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3; WinHttpSetOption(request.h,WINHTTP_OPTION_SECURE_PROTOCOLS,&protocols,sizeof(protocols));
            const wchar_t* headers=L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2026-03-10\r\n";
            require(WinHttpSendRequest(request.h,headers,(DWORD)-1,nullptr,0,0,0) && WinHttpReceiveResponse(request.h,nullptr),"network_error");
            DWORD status=0,sz=sizeof(status); require(WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&sz,nullptr),"http_status");
            if(status==301 || status==302 || status==303 || status==307 || status==308) {
                wchar_t next[16384];sz=sizeof(next);require(WinHttpQueryHeaders(request.h,WINHTTP_QUERY_LOCATION,nullptr,next,&sz,nullptr),"http_redirect");url=utf8(next);continue;
            }
            if(status!=200)return{(int)status,{}};
            std::ofstream out; if(destination){noLinks(*destination);fs::create_directories(destination->parent_path());out.open(*destination,std::ios::binary|std::ios::trunc);require(bool(out),"download_write");}
            std::string body; uint64_t total=0; char buffer[65536]; DWORD count=0;
            do { require(WinHttpReadData(request.h,buffer,sizeof(buffer),&count),"download_read");total+=count;require(total<=limit,"download_limit");if(destination){out.write(buffer,count);require(bool(out),"download_write");}else body.append(buffer,count); }while(count);
            if(destination){out.flush();require(bool(out),"download_write");}return{200,body};
        } throw std::runtime_error("redirect_limit");
    }
public:
    Http():session(WinHttpOpen(L"QIPUpdateAgent/1.1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)) { require(session.h,"http_init");require(WinHttpSetTimeouts(session.h,10000,10000,15000,15000),"http_timeout"); }
    Response get(const std::string& url,size_t limit) override{return fetch(url,limit,nullptr);}
    void download(const std::string& url,const fs::path& file,uint64_t limit) override{require(fetch(url,(size_t)limit,&file).status==200,"download_status");}
};
inline json settings() { return readJson(dataRoot()/L"settings.json",{{"check_automatically",true},{"install_after_exit",false},{"external_history_directory",""}}); }
inline void saveSettings(const json& s) { writeAtomic(dataRoot()/L"settings.json",s.dump(2)); }
inline fs::path externalHistory() {
    auto text=settings().value("external_history_directory",std::string());if(text.empty())return {};
    auto dir=fs::absolute(wide(text)).lexically_normal();noLinks(dir);require(fs::is_directory(dir) && dir.has_filename(),"external_history_path");
    require(!within(dataRoot(),dir) && !within(dir,dataRoot()),"external_history_recursive");
    wchar_t system[MAX_PATH];GetWindowsDirectoryW(system,MAX_PATH);
    require(!within(dir,fs::path(system)),"external_history_system");
    return dir;
}
inline std::string installedVersion(const fs::path& app) { return readJson(app/L"installed-version.json",{{"version","1.0.0"}}).at("version").get<std::string>(); }
inline std::string uniqueId() { GUID id;require(CoCreateGuid(&id)==S_OK,"guid");wchar_t text[40];StringFromGUID2(id,text,40);return utf8(text); }
}
