#pragma once
#include "security.hpp"
#include <functional>
#include <map>
#include <set>

namespace qip {
struct Response { int status; std::string body; };
struct Transport {
    virtual ~Transport() = default;
    virtual Response get(const std::string& url, size_t limit) = 0;
    virtual void download(const std::string& url, const fs::path& destination, uint64_t limit) = 0;
};
struct Policy { std::string repository, modulus, qipHash, dockingHash; };
struct Offer { json manifest; std::string bytes, signature, url; };
inline std::string releaseUrl(const Policy& p, const std::string& tag, const std::string& asset) {
    require(std::regex_match(tag,std::regex(R"(v[0-9]+\.[0-9]+\.[0-9]+(?:\+[0-9A-Za-z.-]+)?)")), "tag");
    require(std::regex_match(asset,std::regex(R"([A-Za-z0-9._-]+)")), "asset_name");
    return "https://github.com/"+p.repository+"/releases/download/"+tag+"/"+asset;
}
inline json authenticate(const Policy& p, const std::string& bytes, const std::string& signature, uint64_t now) {
    require(bytes.size() <= 128*1024 && signature.size()<=2048 && verifyRsa(bytes,signature,p.modulus), "signature_invalid");
    auto m=json::parse(bytes); require(m.at("schema")==1 && m.at("repository").get<std::string>()==p.repository, "manifest_repository");
    auto version=m.at("version").get<std::string>(); auto v=SemVer::parse(version); require(v.pre.empty(), "prerelease");
    require(m.at("tag").get<std::string>()=="v"+version, "manifest_tag");
    require(m.at("asset").get<std::string>()=="QIP2005-Jimm-Setup-"+version+".exe", "manifest_asset");
    uint64_t issued=m.at("issued_at"), expires=m.at("expires_at");
    require(issued<=now+300 && now<=expires && expires>issued && expires-issued<=366*86400ULL, "manifest_expired");
    uint64_t size=m.at("size"); require(size>0 && size<=300*1024*1024ULL, "installer_size");
    require(std::regex_match(m.at("sha256").get<std::string>(),std::regex("[0-9a-f]{64}")), "manifest_hash");
    require(m.at("files").is_array() && m.at("files").size()<=1024, "manifest_files");
    std::set<std::string> seen; bool original=false,docking=false,agent=false,launcher=false,versionFile=false;
    for (auto& f:m.at("files")) {
        std::string path=f.at("path").get<std::string>(), hash=f.at("sha256").get<std::string>(), lower=path;
        std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return (char)std::tolower(c);});
        require(safeRelative(path) && !mutablePath(path) && !lower.starts_with("uninstall/") && seen.insert(lower).second, "manifest_path");
        require(std::regex_match(hash,std::regex("[0-9a-f]{64}")), "manifest_hash");
        if (lower=="qip.exe") { require(hash==p.qipHash,"original_qip_changed"); original=true; }
        if (lower=="plugins/docking.dll") { require(hash==p.dockingHash,"original_plugin_changed"); docking=true; }
        agent|=lower=="qipupdateagent.exe"; launcher|=lower=="qiplauncher.exe"; versionFile|=lower=="installed-version.json";
    }
    require(original && docking && agent && launcher && versionFile,"manifest_incomplete"); return m;
}
inline std::optional<Offer> checkUpdate(Transport& http, const Policy& p, const std::string& installed, const std::string& highest, uint64_t now) {
    auto response=http.get("https://api.github.com/repos/"+p.repository+"/releases/latest",512*1024);
    if (response.status==404) return {};
    require(response.status==200,"github_unavailable"); auto release=json::parse(response.body);
    require(!release.at("draft").get<bool>() && !release.at("prerelease").get<bool>(),"unstable_release");
    std::string tag=release.at("tag_name").get<std::string>(); require(tag.starts_with("v"),"tag"); std::string version=tag.substr(1);
    auto candidate=SemVer::parse(version); require(candidate.pre.empty(),"prerelease");
    if (!newer(version,installed)) return {};
    if (!highest.empty()) require(candidate.compare(SemVer::parse(highest))>=0,"rollback_release");
    std::map<std::string,std::string> assets; std::map<std::string,uint64_t> sizes;
    for (auto& a:release.at("assets")) {
        std::string name=a.at("name").get<std::string>(),url=a.at("browser_download_url").get<std::string>();
        require(url==releaseUrl(p,tag,name),"asset_origin"); require(assets.emplace(name,url).second,"duplicate_asset"); sizes[name]=a.at("size");
    }
    require(assets.count("update-manifest.json") && assets.count("update-manifest.sig"),"manifest_missing");
    auto raw=http.get(assets.at("update-manifest.json"),128*1024); auto signature=http.get(assets.at("update-manifest.sig"),2048);
    require(raw.status==200 && signature.status==200,"manifest_download"); auto m=authenticate(p,raw.body,signature.body,now);
    require(m.at("version").get<std::string>()==version,"release_version"); std::string asset=m.at("asset").get<std::string>();
    require(assets.count(asset) && sizes.at(asset)==m.at("size").get<uint64_t>(),"installer_missing");
    require(SemVer::parse(installed).compare(SemVer::parse(m.at("minimum_installed_version").get<std::string>()))>=0,"upgrade_path");
    return Offer{m,raw.body,signature.body,assets.at(asset)};
}
inline void verifyInstaller(const Offer& offer,const fs::path& file) {
    require(fs::file_size(file)==offer.manifest.at("size").get<uint64_t>() && hashFile(file)==offer.manifest.at("sha256").get<std::string>(),"installer_corrupt");
}
inline bool downloadAndApprove(Transport& http,const Offer& offer,const fs::path& setup,bool automatic,const std::function<bool()>& prompt) {
    bool cached=false;
    if(fs::exists(setup)){try{verifyInstaller(offer,setup);cached=true;}catch(...){} }
    if(!cached){auto partial=setup;partial+=L".part";http.download(offer.url,partial,offer.manifest.at("size"));verifyInstaller(offer,partial);require(MoveFileExW(partial.c_str(),setup.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"cache_commit");}
    verifyInstaller(offer,setup);return automatic || prompt();
}
inline bool due(uint64_t now,uint64_t last,bool enabled) { return enabled && (last==0 || (now>=last && now-last>=86400) || last>now+300); }
}
