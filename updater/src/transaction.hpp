#pragma once
#include "native.hpp"

namespace qip {
struct Integration {
    virtual ~Integration()=default;
    virtual json capture(const fs::path& backup)=0;
    virtual void restore(const fs::path& backup,const json& state)=0;
};
struct Installer {
    virtual ~Installer()=default;
    virtual bool busy(const fs::path& app)=0;
    virtual DWORD run(const fs::path& setup,const fs::path& app)=0;
};
inline void copyTree(const fs::path& source,const fs::path& destination,json& files) {
    require(!within(destination,source),"backup_recursive");
    noLinks(source);noLinks(destination);require(fs::is_directory(source),"backup_source");fs::create_directories(destination);
    for(auto& entry:fs::recursive_directory_iterator(source)) {
        noLinks(entry.path());auto relative=fs::relative(entry.path(),source);auto target=destination/relative;
        if(entry.is_directory())fs::create_directories(target);
        else { require(entry.is_regular_file(),"backup_file_type");fs::create_directories(target.parent_path());
            fs::copy_file(entry.path(),target,fs::copy_options::overwrite_existing);
            fs::last_write_time(target,fs::last_write_time(entry.path()));
            auto hash=hashFile(target);require(hash==hashFile(entry.path()),"backup_inconsistent");
            files.push_back({{"path",utf8(relative.generic_wstring())},{"sha256",hash}});
        }
    }
}
class Snapshot {
    fs::path root;
public:
    explicit Snapshot(fs::path p):root(std::move(p)){}
    void create(const fs::path& app,const fs::path& external,Integration& integration) {
        require(!fs::exists(root),"backup_exists");fs::create_directories(root);json files=json::array(),extra=json::array();
        copyTree(app,root/L"application",files);
        if(!external.empty()) { require(!samePath(external,app) && external.has_filename(),"external_backup_path");copyTree(external,root/L"external-history",extra); }
        auto state=integration.capture(root);
        writeAtomic(root/L"snapshot.json",json{{"files",files},{"external_files",extra},{"external_directory",utf8(external.wstring())},{"integration",state}}.dump(2));
    }
    void restore(const fs::path& app,Integration& integration,const json& candidateFiles) {
        auto state=readJson(root/L"snapshot.json");std::set<std::string> prior;
        for(auto& f:state.at("files")) {
            std::string relative=f.at("path").get<std::string>();require(safeRelative(relative),"backup_path");auto file=root/L"application"/wide(relative);
            noLinks(file);require(hashFile(file)==f.at("sha256").get<std::string>(),"backup_corrupt");prior.insert(relative);
        }
        noLinks(app);fs::create_directories(app);
        for(auto& f:candidateFiles) { std::string relative=f.at("path").get<std::string>();require(safeRelative(relative) && !mutablePath(relative),"rollback_path");
            auto file=app/wide(relative);noLinks(file);if(!prior.count(relative) && fs::is_regular_file(file))fs::remove(file);
        }
        for(auto& f:state.at("files")) {
            std::string relative=f.at("path").get<std::string>();auto file=root/L"application"/wide(relative),target=app/wide(relative);noLinks(target);fs::create_directories(target.parent_path());
            auto temp=target;temp+=L".qip-restore";noLinks(temp);fs::copy_file(file,temp,fs::copy_options::overwrite_existing);
            require(MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"restore_file");
            fs::last_write_time(target,fs::last_write_time(file));require(hashFile(target)==f.at("sha256").get<std::string>(),"restore_hash");
        }
        std::string external=state.at("external_directory").get<std::string>();
        if(!external.empty()) {
            for(auto& f:state.at("external_files")) { std::string relative=f.at("path").get<std::string>();require(safeRelative(relative),"external_backup_path");auto source=root/L"external-history"/wide(relative),target=fs::path(wide(external))/wide(relative);
                noLinks(source);noLinks(target);require(hashFile(source)==f.at("sha256").get<std::string>(),"backup_corrupt");fs::create_directories(target.parent_path());fs::copy_file(source,target,fs::copy_options::overwrite_existing);
            }
        }
        integration.restore(root,state.at("integration"));
    }
};
inline void verifyInstalled(const fs::path& app,const json& manifest) {
    require(installedVersion(app)==manifest.at("version").get<std::string>(),"installed_version");
    for(auto& f:manifest.at("files")) { std::string relative=f.at("path").get<std::string>();require(safeRelative(relative) && !mutablePath(relative),"installed_path");auto file=app/wide(relative);noLinks(file);require(hashFile(file)==f.at("sha256").get<std::string>(),"installed_hash"); }
}
enum class ApplyResult { busy, installed, rolledBack };
inline ApplyResult apply(Installer& installer,Integration& integration,const fs::path& app,const fs::path& setup,const fs::path& backup,const fs::path& phaseFile,const fs::path& external,const Offer& offer) {
    if(installer.busy(app))return ApplyResult::busy;
    verifyInstaller(offer,setup);
    require(newer(offer.manifest.at("version").get<std::string>(),installedVersion(app)),"downgrade_blocked");
    Snapshot snapshot(backup);snapshot.create(app,external,integration);
    if(installer.busy(app))return ApplyResult::busy;
    writeAtomic(phaseFile,"installing");
    try {
        Handle locked(CreateFileW(setup.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));require(locked.h!=INVALID_HANDLE_VALUE,"installer_lock");
        verifyInstaller(offer,setup);
        DWORD exit=installer.run(setup,app);require(exit==0,"installer_failed");verifyInstalled(app,offer.manifest);writeAtomic(phaseFile,"installed");return ApplyResult::installed;
    } catch(...) {
        writeAtomic(phaseFile,"restoring");snapshot.restore(app,integration,offer.manifest.at("files"));writeAtomic(phaseFile,"rolled-back");return ApplyResult::rolledBack;
    }
}
inline constexpr const wchar_t* UninstallKey=L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{A94D4B7F-5D7B-4E78-BF72-C44A3AC98849}_is1";
class WindowsIntegration final:public Integration {
    std::vector<fs::path> links() { return {shellFolder(CSIDL_DESKTOPDIRECTORY)/L"QIP 2005 (Jimm.online).lnk",shellFolder(CSIDL_STARTUP)/L"QIP 2005 (Jimm.online).lnk",shellFolder(CSIDL_PROGRAMS)/L"QIP 2005 (Jimm.online)"/L"QIP 2005 (Jimm.online).lnk",shellFolder(CSIDL_PROGRAMS)/L"QIP 2005 (Jimm.online)"/L"Удалить QIP 2005.lnk",shellFolder(CSIDL_PROGRAMS)/L"QIP 2005 (Jimm.online)"/L"Настройки обновлений.lnk"}; }
public:
    json capture(const fs::path& backup) override {
        json state={{"registry",json::array()},{"registry_present",false},{"links",json::array()}};HKEY key;
        LONG result=RegOpenKeyExW(HKEY_CURRENT_USER,UninstallKey,0,KEY_READ,&key);
        require(result==ERROR_SUCCESS || result==ERROR_FILE_NOT_FOUND,"registry_read");
        if(result==ERROR_SUCCESS) { state["registry_present"]=true;
            for(DWORD i=0;;++i){wchar_t name[16384];DWORD namesize=16384,type=0,size=0;
                result=RegEnumValueW(key,i,name,&namesize,nullptr,&type,nullptr,&size);if(result==ERROR_NO_MORE_ITEMS)break;
                if(result!=ERROR_SUCCESS || size>1024*1024){RegCloseKey(key);throw std::runtime_error("registry_read");}
                Bytes data(size);namesize=16384;result=RegEnumValueW(key,i,name,&namesize,nullptr,&type,data.data(),&size);require(result==ERROR_SUCCESS,"registry_read");
                state["registry"].push_back({{"name",utf8(std::wstring(name,namesize))},{"type",type},{"data",hex(data)}});
            }RegCloseKey(key);
        }
        int i=0;fs::create_directories(backup/L"integration");
        for(auto& link:links()){noLinks(link);bool exists=fs::is_regular_file(link);std::string file=std::to_string(i++)+".lnk";state["links"].push_back({{"present",exists},{"file",file}});if(exists)fs::copy_file(link,backup/L"integration"/wide(file));}
        return state;
    }
    void restore(const fs::path& backup,const json& state) override {
        LONG result=RegDeleteTreeW(HKEY_CURRENT_USER,UninstallKey);require(result==ERROR_SUCCESS || result==ERROR_FILE_NOT_FOUND,"registry_restore");
        if(state.at("registry_present").get<bool>()){HKEY key;DWORD disposition;require(RegCreateKeyExW(HKEY_CURRENT_USER,UninstallKey,0,nullptr,0,KEY_WRITE,nullptr,&key,&disposition)==ERROR_SUCCESS,"registry_restore");
            for(auto& value:state.at("registry")){auto name=wide(value.at("name").get<std::string>());auto data=unhex(value.at("data"));DWORD type=value.at("type");result=RegSetValueExW(key,name.c_str(),0,type,data.data(),(DWORD)data.size());if(result!=ERROR_SUCCESS){RegCloseKey(key);throw std::runtime_error("registry_restore");}}RegCloseKey(key);
        }
        auto paths=links();require(state.at("links").size()==paths.size(),"integration_backup");
        for(size_t i=0;i<paths.size();++i){auto& link=paths[i];noLinks(link);auto v=state.at("links").at(i);if(v.at("present").get<bool>()){fs::create_directories(link.parent_path());fs::copy_file(backup/L"integration"/wide(v.at("file").get<std::string>()),link,fs::copy_options::overwrite_existing);}else if(fs::is_regular_file(link))fs::remove(link);}
    }
};
class WindowsInstaller final:public Installer {
public:
    bool busy(const fs::path& app) override{return appBusy(app,GetCurrentProcessId());}
    DWORD run(const fs::path& setup,const fs::path& app) override { return process(setup,L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DIR="+quote(app.wstring()),true,app); }
};
}
