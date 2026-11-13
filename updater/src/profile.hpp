#pragma once
#include "launcher.hpp"
#include <set>

namespace qip {
inline std::string lowerAscii(std::string text){for(auto& c:text)if(c>='A' && c<='Z')c+=32;return text;}
inline std::string trim(std::string text){
    auto first=text.find_first_not_of(" \t\r");if(first==std::string::npos)return {};
    return text.substr(first,text.find_last_not_of(" \t\r")-first+1);
}
class LegacyIni {
    enum Encoding {ansi,utf8bom,utf16le,utf16be} encoding=ansi;
    std::string original;
    std::string encode(const std::string& text)const{
        if(encoding==utf8bom)return std::string("\xef\xbb\xbf")+text;
        auto unicode=wide(text);
        if(encoding==utf16le || encoding==utf16be){
            std::string result=encoding==utf16le?std::string("\xff\xfe"):std::string("\xfe\xff");
            for(auto c:unicode){if(encoding==utf16le){result+=char(c&255);result+=char(c>>8);}else{result+=char(c>>8);result+=char(c&255);}}
            return result;
        }
        BOOL substituted=FALSE;int count=WideCharToMultiByte(1251,WC_NO_BEST_FIT_CHARS,unicode.data(),(int)unicode.size(),nullptr,0,nullptr,&substituted);
        require(count>0 && !substituted,"legacy_path_encoding");std::string result(count,0);
        WideCharToMultiByte(1251,WC_NO_BEST_FIT_CHARS,unicode.data(),(int)unicode.size(),result.data(),count,nullptr,&substituted);
        require(!substituted,"legacy_path_encoding");return result;
    }
public:
    std::string text;
    explicit LegacyIni(std::string raw):original(std::move(raw)){
        auto bytes=original;
        if(bytes.starts_with("\xef\xbb\xbf")){encoding=utf8bom;text=bytes.substr(3);wide(text);}
        else if(bytes.starts_with("\xff\xfe") || bytes.starts_with("\xfe\xff")){
            encoding=bytes.starts_with("\xff\xfe")?utf16le:utf16be;require(bytes.size()%2==0,"legacy_ini_encoding");
            std::wstring unicode;
            for(size_t i=2;i<bytes.size();i+=2){unsigned a=(unsigned char)bytes[i],b=(unsigned char)bytes[i+1];unicode+=wchar_t(encoding==utf16le?a|(b<<8):(a<<8)|b);}
            text=utf8(unicode);
        }else{
            require(bytes.find('\0')==std::string::npos,"legacy_ini_encoding");
            int count=MultiByteToWideChar(1251,0,bytes.data(),(int)bytes.size(),nullptr,0);require(count>0,"legacy_ini_encoding");
            std::wstring unicode(count,0);MultiByteToWideChar(1251,0,bytes.data(),(int)bytes.size(),unicode.data(),count);text=utf8(unicode);
        }
        require(encode(text)==original,"legacy_ini_roundtrip");
    }
    std::optional<std::string> get(const std::string& key)const{
        std::optional<std::string> found;std::istringstream lines(text);std::string line;
        while(std::getline(lines,line)){auto equals=line.find('=');if(equals==std::string::npos || lowerAscii(trim(line.substr(0,equals)))!=lowerAscii(key))continue;
            require(!found,"legacy_ini_duplicate");found=trim(line.substr(equals+1));}
        return found;
    }
    void set(const std::string& key,const std::string& value){
        std::string result;size_t start=0;int replaced=0;
        while(start<text.size()){
            auto end=text.find('\n',start);if(end==std::string::npos)end=text.size();
            auto line=text.substr(start,end-start);auto equals=line.find('=');
            if(equals!=std::string::npos && lowerAscii(trim(line.substr(0,equals)))==lowerAscii(key)){
                bool cr=!line.empty() && line.back()=='\r';line=line.substr(0,equals+1)+value+(cr?"\r":"");++replaced;
            }
            result+=line;if(end<text.size())result+='\n';start=end+1;
        }
        require(replaced==1,"legacy_ini_duplicate");text=std::move(result);
    }
    std::string bytes()const{return encode(text);}
    bool changed()const{return bytes()!=original;}
};
inline std::vector<fs::path> profileConfigurations(const fs::path& app){
    std::vector<fs::path> result;auto users=app/L"Users";noLinks(users);if(!fs::exists(users))return result;
    for(auto& entry:fs::recursive_directory_iterator(users)){
        noLinks(entry.path());if(!entry.is_regular_file())continue;auto extension=lowerAscii(utf8(entry.path().extension().wstring()));
        if((extension==".ini" || extension==".cfg") && fs::file_size(entry.path()) && fs::file_size(entry.path())<=1024*1024)result.push_back(entry.path());
    }return result;
}
inline bool historyIniBytes(const std::string& text){
    std::string ascii;for(auto c:text)if(c)ascii+=c;
    return lowerAscii(ascii).find("histpath=")!=std::string::npos;
}
inline std::vector<fs::path> configuredHistories(const fs::path& app){
    std::vector<fs::path> result;
    for(auto& file:profileConfigurations(app)){
        auto raw=read(file);if(!historyIniBytes(raw))continue;LegacyIni ini(raw);auto value=ini.get("HistPath");
        if(value && !value->empty()){auto dir=fs::path(wide(*value));if(dir.is_relative())dir=app/dir;result.push_back(fs::absolute(dir).lexically_normal());}
    }return result;
}
inline void removeNativeStartup(const fs::path& app){
    HKEY key;auto result=RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_QUERY_VALUE|KEY_SET_VALUE,&key);
    if(result==ERROR_FILE_NOT_FOUND)return;require(result==ERROR_SUCCESS,"registry_read");
    wchar_t buffer[32768]{};DWORD size=sizeof(buffer),type=0;result=RegQueryValueExW(key,L"QIP2005",nullptr,&type,(BYTE*)buffer,&size);
    if(result==ERROR_FILE_NOT_FOUND){RegCloseKey(key);return;}
    if(result!=ERROR_SUCCESS || (type!=REG_SZ && type!=REG_EXPAND_SZ) || !size || size>sizeof(buffer)){RegCloseKey(key);throw std::runtime_error("registry_read");}
    std::wstring value(buffer,size/sizeof(wchar_t));while(!value.empty() && value.back()==0)value.pop_back();
    if(value.size()>1 && value.front()==L'"' && value.back()==L'"')value=value.substr(1,value.size()-2);
    bool own=samePath(value,app/L"qip.exe") || samePath(value,app/L"QipLauncher.exe");
    if(own)result=RegDeleteValueW(key,L"QIP2005");RegCloseKey(key);require(!own || result==ERROR_SUCCESS || result==ERROR_FILE_NOT_FOUND,"registry_write");
}
inline void prepareProfile(const fs::path& app,bool updateRegistry=true){
    require(!clientRunning(app),"application_busy");
    for(auto& file:profileConfigurations(app)){
        auto raw=read(file);if(!historyIniBytes(raw))continue;LegacyIni ini(raw);auto value=ini.get("HistPath");if(!value)continue;
        if(!value->empty()){auto path=fs::path(wide(*value));if(path.is_relative())path=app/path;path=fs::absolute(path).lexically_normal();noLinks(path);ini.set("HistPath",utf8(path.wstring()));}
        if(ini.get("Startup"))ini.set("Startup","0");
        if(ini.changed()){
            auto recovery=dataRoot()/L"profile-migration"/wide(uniqueId());noLinks(recovery);fs::create_directories(recovery);
            writeAtomic(recovery/L"original.ini",raw);
            writeAtomic(recovery/L"location.json",json{{"file",utf8(file.wstring())}}.dump());
            writeAtomic(file,ini.bytes());
        }
    }
    if(updateRegistry)removeNativeStartup(app);
}
}
