#pragma once
#include "profile.hpp"
#include "transaction.hpp"
#include <map>

namespace qip {
struct ProfileSource {std::string id;fs::path path;bool directory;};
inline void validateHistoryLocation(const fs::path& path,const fs::path& app,const fs::path& storage){
    noLinks(path);require(path.has_filename() && path!=path.root_path(),"history_root");
    require(!within(app,path) && !within(storage,path) && !within(path,storage),"history_recursive");
    wchar_t windows[MAX_PATH]{};GetWindowsDirectoryW(windows,MAX_PATH);
    require(!within(path,fs::path(windows)),"history_system");
}
inline std::vector<ProfileSource> profileSources(const fs::path& app,const fs::path& storage,const fs::path& extra={}){
    std::vector<ProfileSource> result;auto users=app/L"Users";noLinks(users);
    if(fs::is_directory(users))result.push_back({"users",users,true});
    for(auto name:{"LI/current.cfg","LI/langs.cfg","Skins/current.cfg","Skins/skins.cfg"}){
        auto path=app/wide(name);noLinks(path);if(fs::is_regular_file(path))result.push_back({std::string("setting-")+hex(digest(name)).substr(0,16),path,false});
    }
    auto histories=configuredHistories(app);if(!extra.empty())histories.push_back(fs::absolute(extra).lexically_normal());
    for(auto& path:histories){
        if(within(path,users))continue;
        validateHistoryLocation(path,app,storage);
        if(!fs::exists(path))continue;require(fs::is_directory(path),"history_directory");
        auto key=path.wstring();CharLowerBuffW(key.data(),(DWORD)key.size());auto id="history-"+hex(digest(utf8(key))).substr(0,24);
        if(std::none_of(result.begin(),result.end(),[&](auto& source){return samePath(source.path,path);}))result.push_back({id,path,true});
    }
    return result;
}
inline json sourceInventory(const std::vector<ProfileSource>& sources){
    json result=json::array();
    for(auto& source:sources){
        noLinks(source.path);json files=json::array();
        auto add=[&](const fs::path& file,const std::string& relative){
            noLinks(file);require(fs::is_regular_file(file) && safeRelative(relative),"profile_file");
            files.push_back({{"path",relative},{"sha256",hashFile(file)},{"bytes",fs::file_size(file)}});
        };
        if(source.directory){
            for(auto& entry:fs::recursive_directory_iterator(source.path)){noLinks(entry.path());if(entry.is_directory())continue;add(entry.path(),utf8(fs::relative(entry.path(),source.path).generic_wstring()));}
        }else add(source.path,"data");
        std::sort(files.begin(),files.end(),[](auto& a,auto& b){return a.at("path").template get<std::string>()<b.at("path").template get<std::string>();});
        result.push_back({{"id",source.id},{"path",utf8(source.path.wstring())},{"directory",source.directory},{"files",files}});
    }return result;
}
struct ProfileBackupResult {std::string status;std::string id;};
class ProfileBackupStore {
    fs::path storage;
    fs::path journal(const fs::path& app)const{return storage/wide("restore-"+hex(digest(utf8(fs::absolute(app).lexically_normal().wstring()))).substr(0,24)+".json");}
    fs::path folder(const std::string& id)const{
        require(std::regex_match(id,std::regex("profile-[0-9]{1,20}-[0-9a-fA-F-]{36}")),"profile_backup_id");
        auto path=storage/wide(id);noLinks(path);return path;
    }
    json load(const fs::path& app,const std::string& id)const{
        auto path=folder(id);auto manifest=json::parse(read(path/L"profile.json",16*1024*1024));
        require(manifest.at("schema")==1 && manifest.at("id")==id && samePath(wide(manifest.at("app").get<std::string>()),app),"profile_backup_owner");
        require(manifest.at("sources").is_array() && manifest.at("sources").size()<=256,"profile_backup_format");
        return manifest;
    }
    std::vector<ProfileSource> restorePlan(const fs::path& app,const json& manifest,const fs::path& extra,bool recovering=false)const{
        auto allowed=profileSources(app,storage,extra);std::vector<ProfileSource> result;std::set<std::string> ids;
        for(auto& source:manifest.at("sources")){
            auto id=source.at("id").get<std::string>();auto path=fs::path(wide(source.at("path").get<std::string>()));bool directory=source.at("directory");
            require(ids.insert(id).second,"profile_duplicate_source");
            auto match=std::find_if(allowed.begin(),allowed.end(),[&](auto& known){return known.id==id && samePath(known.path,path) && known.directory==directory;});
            bool fixed=id=="users" && samePath(path,app/L"Users") && directory;
            for(auto name:{"LI/current.cfg","LI/langs.cfg","Skins/current.cfg","Skins/skins.cfg"})
                if(id==std::string("setting-")+hex(digest(name)).substr(0,16) && samePath(path,app/wide(name)) && !directory)fixed=true;
            if(id.starts_with("history-") && directory){
                auto configured=configuredHistories(app);if(!extra.empty())configured.push_back(extra);
                for(auto& known:configured)if(samePath(known,path)){
                    validateHistoryLocation(path,app,storage);auto key=path.wstring();CharLowerBuffW(key.data(),(DWORD)key.size());
                    if(id=="history-"+hex(digest(utf8(key))).substr(0,24))fixed=true;
                }
            }
            if(match==allowed.end() && recovering && id.starts_with("history-")){
                validateHistoryLocation(path,app,storage);auto key=path.wstring();CharLowerBuffW(key.data(),(DWORD)key.size());
                require(id=="history-"+hex(digest(utf8(key))).substr(0,24) && directory,"profile_restore_source");
            }else require(fixed || match!=allowed.end(),"profile_restore_source");
            result.push_back({id,path,directory});
        }return result;
    }
    void verify(const fs::path& app,const json& manifest)const{
        auto base=folder(manifest.at("id"));std::set<std::string> ids;
        require(samePath(wide(manifest.at("app").get<std::string>()),app),"profile_backup_owner");
        for(auto& source:manifest.at("sources")){
            auto id=source.at("id").get<std::string>();require(std::regex_match(id,std::regex("(users|setting-[0-9a-f]{16}|history-[0-9a-f]{24})")) && ids.insert(id).second,"profile_backup_source");
            std::set<std::wstring> paths;require(source.at("files").size()<=100000,"profile_backup_format");
            for(auto& file:source.at("files")){
                std::string relative=file.at("path");require(safeRelative(relative),"profile_backup_path");
                auto lower=wide(relative);CharLowerBuffW(lower.data(),(DWORD)lower.size());require(paths.insert(lower).second,"profile_backup_path");
                require(source.at("directory").get<bool>() || relative=="data","profile_backup_path");
                auto path=base/L"content"/wide(id)/wide(relative);noLinks(path);
                require(fs::is_regular_file(path) && fs::file_size(path)==file.at("bytes").get<uint64_t>() && hashFile(path)==file.at("sha256").get<std::string>(),"profile_backup_corrupt");
            }
        }
    }
    ProfileBackupResult createUnlocked(const fs::path& app,bool manual,bool enabled,uint64_t timestamp,const fs::path& extra,const std::string& reason){
        require(!clientRunning(app),"application_busy");noLinks(storage);
        if(!manual && !enabled)return {"disabled",{}};
        auto previous=list(app);
        if(!manual && !previous.empty()){
            uint64_t last=previous.front().at("created");if(timestamp>=last && timestamp-last<86400)return {"not_due",{}};
        }
        auto sources=profileSources(app,storage,extra);require(!sources.empty(),"profile_backup_empty");auto inventory=sourceInventory(sources);
        auto fingerprint=hex(digest(inventory.dump()));
        if(!manual && !previous.empty() && previous.front().value("fingerprint",std::string())==fingerprint){
            try{verify(app,previous.front());return {"unchanged",previous.front().at("id")};}catch(...){}
        }
        auto guid=uniqueId();guid=guid.substr(1,guid.size()-2);auto id="profile-"+std::to_string(timestamp)+"-"+guid;
        auto final=folder(id),pending=storage/wide(".pending-"+id);noLinks(pending);require(!fs::exists(pending) && !fs::exists(final),"profile_backup_exists");
        fs::create_directories(pending);
        try{
            for(auto& source:inventory){
                auto original=fs::path(wide(source.at("path").get<std::string>()));bool directory=source.at("directory");
                for(auto& file:source.at("files")){
                    auto relative=wide(file.at("path").get<std::string>());auto from=directory?original/relative:original;auto to=pending/L"content"/wide(source.at("id").get<std::string>())/relative;
                    noLinks(from);noLinks(to);fs::create_directories(to.parent_path());fs::copy_file(from,to);fs::last_write_time(to,fs::last_write_time(from));
                    require(hashFile(to)==file.at("sha256").get<std::string>(),"profile_backup_inconsistent");
                }
            }
            require(!clientRunning(app) && sourceInventory(sources)==inventory,"profile_backup_inconsistent");
            auto manifest=json{{"schema",1},{"id",id},{"app",utf8(fs::absolute(app).lexically_normal().wstring())},{"version",installedVersion(app)},{"created",timestamp},{"reason",reason},{"fingerprint",fingerprint},{"sources",inventory}};
            writeAtomic(pending/L"profile.json",manifest.dump(2));fs::rename(pending,final);return {"created",id};
        }catch(...){
            if(within(pending,storage) && pending.filename().wstring().starts_with(L".pending-profile-")){noLinks(pending);std::error_code error;fs::remove_all(pending,error);}throw;
        }
    }
    void restoreFiles(const fs::path& app,const json& manifest,const std::vector<ProfileSource>& plan){
        verify(app,manifest);auto base=folder(manifest.at("id"));
        for(auto& source:manifest.at("sources")){
            auto match=std::find_if(plan.begin(),plan.end(),[&](auto& allowed){return allowed.id==source.at("id").get<std::string>();});require(match!=plan.end(),"profile_restore_source");
            for(auto& file:source.at("files")){
                require(!clientRunning(app),"application_busy");
                auto relative=wide(file.at("path").get<std::string>());auto from=base/L"content"/wide(match->id)/relative;auto target=match->directory?match->path/relative:match->path;
                noLinks(target);fs::create_directories(target.parent_path());auto temp=target;temp+=L".qip-profile-restore";noLinks(temp);
                fs::copy_file(from,temp,fs::copy_options::overwrite_existing);require(hashFile(temp)==file.at("sha256").get<std::string>(),"profile_restore_hash");
                require(MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"profile_restore_file");
                fs::last_write_time(target,fs::last_write_time(from));
            }
        }
    }
public:
    explicit ProfileBackupStore(fs::path path=dataRoot()/L"profile-backups"):storage(fs::absolute(path).lexically_normal()){noLinks(storage);}
    fs::path root()const{return storage;}
    bool pending(const fs::path& app)const{auto path=journal(app);noLinks(path);return fs::exists(path);}
    std::vector<json> list(const fs::path& app)const{
        std::vector<json> result;if(!fs::exists(storage))return result;noLinks(storage);
        for(auto& entry:fs::directory_iterator(storage)){
            noLinks(entry.path());if(!entry.is_directory() || !entry.path().filename().wstring().starts_with(L"profile-"))continue;
            try{auto manifest=load(app,utf8(entry.path().filename().wstring()));require(manifest.at("created").is_number_unsigned(),"profile_backup_format");result.push_back(std::move(manifest));}catch(...){}
        }
        std::sort(result.begin(),result.end(),[](auto& a,auto& b){return a.at("created").template get<uint64_t>()>b.at("created").template get<uint64_t>();});return result;
    }
    ProfileBackupResult create(const fs::path& app,bool manual,bool enabled=true,uint64_t timestamp=now(),const fs::path& extra={}){
        Mutex backup(lockName("ProfileBackup",app),true,10000);require(backup.acquired(),"profile_backup_busy");
        Mutex install(lockName("Install",app),true);require(install.acquired(),"application_busy");
        return createUnlocked(app,manual,enabled,timestamp,extra,manual?"manual":"after-exit");
    }
    void restore(const fs::path& app,const std::string& id,const fs::path& extra={}){
        Mutex backup(lockName("ProfileBackup",app),true,10000);require(backup.acquired(),"profile_backup_busy");
        Mutex install(lockName("Install",app),true);require(install.acquired() && !clientRunning(app),"application_busy");
        auto selected=load(app,id);verify(app,selected);auto plan=restorePlan(app,selected,extra);
        auto before=createUnlocked(app,true,true,now(),extra,"before-restore");auto prior=load(app,before.id);auto rollback=restorePlan(app,prior,extra);
        auto pending=journal(app);writeAtomic(pending,json{{"app",utf8(app.wstring())},{"previous",before.id}}.dump());
        try{restoreFiles(app,selected,plan);fs::remove(pending);}
        catch(...){if(!clientRunning(app)){try{restoreFiles(app,prior,rollback);fs::remove(pending);}catch(...){}}throw;}
    }
    bool recover(const fs::path& app,const fs::path& extra={}){
        auto pending=journal(app);if(!fs::exists(pending))return false;noLinks(pending);
        Mutex backup(lockName("ProfileBackup",app),true,10000);require(backup.acquired(),"profile_backup_busy");
        Mutex install(lockName("Install",app),true);require(install.acquired() && !clientRunning(app),"application_busy");
        auto state=readJson(pending);require(samePath(wide(state.at("app").get<std::string>()),app),"profile_backup_owner");
        auto prior=load(app,state.at("previous"));verify(app,prior);restoreFiles(app,prior,restorePlan(app,prior,extra,true));fs::remove(pending);return true;
    }
};
}
