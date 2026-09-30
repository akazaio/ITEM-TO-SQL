#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <iostream>
#include <map>
#include <numeric>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;

using i64 = long long;
using Row = std::vector<std::string>;
using Rows = std::vector<Row>;
using ByItemOrg = std::unordered_map<i64, Rows>;
using ExtLookup = std::unordered_map<int, ByItemOrg>;
using Stats = std::map<std::string, i64>;

static constexpr int ITEM_ATTRIB_GENERAL = 0;
static constexpr int ITEM_ATTRIB_MAGIC = 1;
static constexpr int ITEM_ATTRIB_LAIR = 2;
static constexpr int ITEM_ATTRIB_UNIQUE = 4;
static constexpr int ITEM_ATTRIB_UPGRADE = 5;
static constexpr int ITEM_ATTRIB_COSPRE = 8;
static constexpr int ITEM_ATTRIB_UPGRADE_REVERSE = 11;
static constexpr int ITEM_ATTRIB_UNIQUE_REVERSE = 12;

static constexpr int ITEM_CLASS_JAMADAR = 140;
static constexpr int ITEM_CLASS_COSPLAY = 252;
static constexpr int ITEM_SLOT_TALISMAN = 113;
static constexpr int TALISMAN_RACE_COURAGE = 77;
static constexpr int TALISMAN_RACE_SPIRIT = 20;
static constexpr int ITEM_SLOT_COSTUME_PANTS = 105;
static constexpr int ITEM_SLOT_COSTUME_GLOVES = 106;
static constexpr int ITEM_SLOT_COSTUME_HELMET_OR_TOP = 107;
static constexpr int ITEM_SLOT_COSTUME_ARM = 108;
static constexpr int ITEM_SLOT_COSTUME_SHOES = 109;
static constexpr int ITEM_SLOT_WING = 110;
static constexpr int ITEM_SLOT_PET = 111;
static constexpr int ITEM_SLOT_TATTOO = 112;
static constexpr int ITEM_SLOT_EMBLEM = 114;

static const std::vector<std::string> USKO_ITEM_COLUMNS = {
    "Num","Extension","strName","Kind","Slot","Race","Class","Damage","Delay","Range","Weight","Duration","BuyPrice","SellPrice","SellNpcType","SellNpcPrice","Ac","Countable","Effect1","Effect2","ReqLevel","ReqLevelMax","ReqRank","ReqTitle","ReqStr","ReqSta","ReqDex","ReqIntel","ReqCha","SellingGroup","ItemType","Hitrate","Evasionrate","DaggerAc","JamadarAc","SwordAc","MaceAc","AxeAc","SpearAc","BowAc","FireDamage","IceDamage","LightningDamage","PoisonDamage","HPDrain","MPDamage","MPDrain","MirrorDamage","Droprate","StrB","StaB","DexB","IntelB","ChaB","MaxHpB","MaxMpB","FireR","ColdR","LightningR","MagicR","PoisonR","CurseR","ItemClass","NPbuyPrice","Bound","Grade","DropNotice","UpgradeNotice"
};

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b-1])) --b;
    return s.substr(a,b-a);
}
static std::string lower(std::string s){ for(char& c:s) c=(char)std::tolower((unsigned char)c); return s; }
static i64 to_int(const std::string& v){
    std::string s = trim(v);
    if(s.empty()) return 0;
    try { size_t idx=0; long long x = std::stoll(s,&idx,10); return x; } catch(...) { return 0; }
}
static std::string sql_string(const std::string& v){ std::string r="'"; for(char c:v){ if(c=='\'') r+="''"; else r+=c; } r+="'"; return r; }
static std::string regex_replace_i(const std::string& s, const std::string& pat, const std::string& repl){ return std::regex_replace(s, std::regex(pat, std::regex::icase), repl); }
static bool regex_match_i(const std::string& s, const std::string& pat){ return std::regex_match(s, std::regex(pat, std::regex::icase)); }
static bool regex_search_i(const std::string& s, const std::string& pat){ return std::regex_search(s, std::regex(pat, std::regex::icase)); }
static std::string normalize_spaces(std::string v){ v=trim(v); v=std::regex_replace(v,std::regex("\\s+")," "); return trim(v); }
static bool starts_with_i(const std::string& s, const std::string& prefix){ std::string a=lower(s), b=lower(prefix); return a.rfind(b,0)==0; }
static bool contains_i(const std::string& s, const std::string& needle){ return lower(s).find(lower(needle)) != std::string::npos; }

static Rows parse_sql_value_tuples(const fs::path& path){
    std::ifstream f(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Rows rows;
    size_t i=0,n=text.size();
    while(i<n){
        if(text[i]!='('){ ++i; continue; }
        ++i; Row fields; std::string cur; bool in_string=false;
        while(i<n){
            char ch=text[i];
            if(in_string){
                if(ch=='\''){
                    if(i+1<n && text[i+1]=='\''){ cur.push_back('\''); i+=2; continue; }
                    in_string=false; ++i; continue;
                }
                cur.push_back(ch); ++i; continue;
            }
            if(ch=='\''){ in_string=true; ++i; continue; }
            if(ch==','){ fields.push_back(trim(cur)); cur.clear(); ++i; continue; }
            if(ch==')'){ fields.push_back(trim(cur)); rows.push_back(std::move(fields)); ++i; break; }
            cur.push_back(ch); ++i;
        }
    }
    return rows;
}

static Rows parse_text_tbl_rows(const fs::path& path){
    std::ifstream f(path, std::ios::binary);
    Rows rows; std::string line;
    while(std::getline(f,line)){
        if(line.empty()) continue;
        if(!line.empty() && line.back()=='\r') line.pop_back();
        std::vector<std::string> fields;
        char delim = (line.find('\t')!=std::string::npos) ? '\t' : ',';
        std::string cur; bool inq=false;
        for(size_t i=0;i<line.size();++i){ char c=line[i];
            if(c=='\"'){ inq=!inq; continue; }
            if(!inq && c==delim){ fields.push_back(trim(cur)); cur.clear(); }
            else cur.push_back(c);
        }
        fields.push_back(trim(cur));
        if(fields.size()==41 || fields.size()==56) rows.push_back(std::move(fields));
    }
    return rows;
}

static Rows parse_source_rows(const fs::path& path){
    std::string ext = lower(path.extension().string());
    if(ext==".tbl" || ext==".txt" || ext==".csv"){
        Rows r = parse_text_tbl_rows(path);
        if(!r.empty()) return r;
    }
    return parse_sql_value_tuples(path);
}

static int parse_trailing_enhance_level(const std::string& name){ std::smatch m; if(!std::regex_search(name,m,std::regex("\\(\\+(\\d+)\\)\\s*$"))) return 0; int lv=std::stoi(m[1]); return (lv>=1 && lv<=99)?lv:0; }
static int resolve_tail_enhance_level(i64 item_index){ int tail=(int)(item_index%100); if(tail==0||tail>99) return 0; int grade=tail%10; return grade==0?10:grade; }
static bool is_enhance_display_eligible(int item_type){ return item_type==1||item_type==2||item_type==4||item_type==5||item_type==11||item_type==12; }
static int resolve_reverse_enhance_level(const Row& base,const Row& ext){ int lv=parse_trailing_enhance_level(ext[1]); if(lv>0) return lv; int e= (int)to_int(ext[44]); if(e>=1&&e<=30) return e; lv=parse_trailing_enhance_level(base[2]); if(lv>0) return lv; return resolve_tail_enhance_level(to_int(ext[0])); }
static int resolve_normal_enhance_level(const Row& base,const Row& ext){ int lv=parse_trailing_enhance_level(ext[1]); if(lv>0) return lv; lv=parse_trailing_enhance_level(base[2]); if(lv>0) return lv; return resolve_tail_enhance_level(to_int(ext[0])); }
static int resolve_item_enhance_level(const Row& base,const Row& ext){ int item_type=(int)to_int(ext[7]); if(!is_enhance_display_eligible(item_type)) return 0; if(item_type==ITEM_ATTRIB_UNIQUE){ int lv=parse_trailing_enhance_level(ext[1]); if(lv>0) return lv; return parse_trailing_enhance_level(base[2]); } if(item_type==ITEM_ATTRIB_MAGIC||item_type==ITEM_ATTRIB_LAIR||item_type==ITEM_ATTRIB_UPGRADE) return resolve_normal_enhance_level(base,ext); if(item_type==ITEM_ATTRIB_UNIQUE_REVERSE||item_type==ITEM_ATTRIB_UPGRADE_REVERSE) return resolve_reverse_enhance_level(base,ext); return 0; }

static std::string normalize_talisman_base_name(std::string v){ v=trim(v); v=regex_replace_i(v,"\\bTalilsman\\b","Talisman"); v=std::regex_replace(v,std::regex("\\s+-\\s+")," - "); v=regex_replace_i(v,"\\s+(Tier\\s+\\d+|Final\\s+Stage)\\s*$"," - $1"); return v; }
static bool is_talisman_base(const Row& b){ return b.size()==41 && to_int(b[10])==ITEM_CLASS_COSPLAY && to_int(b[12])==ITEM_SLOT_TALISMAN && contains_i(normalize_talisman_base_name(b[2]),"talisman"); }
static std::string talisman_family_key(const std::string& name){ std::string v=lower(normalize_talisman_base_name(name)); v=regex_replace_i(v,"\\s+-\\s+tier\\s+\\d+\\s*$",""); v=regex_replace_i(v,"\\s+-\\s+final\\s+stage\\s*$",""); return normalize_spaces(v); }
static int talisman_stage_sort_key(const std::string& name){ std::smatch m; std::string v=normalize_talisman_base_name(name); if(std::regex_search(v,m,std::regex("Tier\\s+(\\d+)",std::regex::icase))) return std::stoi(m[1]); if(regex_search_i(v,"Final\\s+Stage")) return 99; return 50; }
static std::string normalize_display_tokens(std::string v){ v=std::regex_replace(v,std::regex(u8"'"),"'"); return normalize_spaces(v); }
static std::string normalize_family_name(std::string v){ v=lower(normalize_display_tokens(v)); v=regex_replace_i(v,"^special\\s+",""); v=regex_replace_i(v,"\\s*\\((?:attack|defense|offense|defence)\\)\\s*$",""); v=regex_replace_i(v,"\\s*\\(\\d+\\s*(?:day|days)\\)\\s*$",""); v=regex_replace_i(v,"\\s+-\\s*(?:non-tradable|untradeable)\\s*$",""); v=std::regex_replace(v,std::regex("[^a-z0-9]+")," "); return normalize_spaces(v); }
static bool is_special_cospre_variant_base(const Row& b){ if(b.size()!=41) return false; if(to_int(b[10])!=ITEM_CLASS_COSPLAY) return false; int sl=(int)to_int(b[12]); if(!(sl==ITEM_SLOT_PET||sl==ITEM_SLOT_WING||sl==ITEM_SLOT_TATTOO||sl==ITEM_SLOT_EMBLEM)) return false; return starts_with_i(normalize_display_tokens(b[2]),"special "); }
static bool is_old_accessory_base(const Row& b){ if(b.size()!=41) return false; if(!starts_with_i(normalize_display_tokens(b[2]),"old ")) return false; int e=(int)to_int(b[1]), k=(int)to_int(b[10]), s=(int)to_int(b[12]); return (e==18||e==19||e==20||e==21) && (k==91||k==92||k==93||k==94) && (s==10||s==11||s==12||s==14); }
static bool is_generic_ext_name(const std::string& name){ static const std::set<std::string> g={"","supply item","upgrade item","magic item","unique item","cospre item"}; return g.count(lower(normalize_display_tokens(name)))>0; }
static bool is_global_zero_placeholder_ext(const Row& e){ return e.size()==56 && to_int(e[2])==0 && to_int(e[0])==0 && to_int(e[7])==ITEM_ATTRIB_GENERAL && is_generic_ext_name(e[1]); }

static std::vector<int> talisman_suffixes_by_race(int race){ if(race==77){ std::vector<int> v; for(int i=39;i<=68;++i)v.push_back(i); return v;} if(race==20){ std::vector<int> v; for(int i=117;i<=131;++i)v.push_back(i); return v;} return {}; }
static std::vector<int> talisman_stage_counts_by_race(int race){ if(race==77) return {16,5,5,4}; if(race==20) return {7,5,3}; return {}; }
static bool cospre_slot_single(int s){ return s>=105 && s<=114; }
static bool cospre_skip_slot(int s){ return s==ITEM_SLOT_WING||s==ITEM_SLOT_PET||s==ITEM_SLOT_TATTOO||s==ITEM_SLOT_TALISMAN||s==ITEM_SLOT_EMBLEM; }

static i64 canonical_base_id(i64 item_id){ return (item_id/1000)*1000; }
static i64 resolve_effective_ext_num(const Row& base,const Row& ext){ i64 base_id=to_int(base[0]), ext_num=to_int(ext[0]); if(ext_num==0) return 0; if(canonical_base_id(base_id+ext_num)==base_id) return ext_num; i64 normalized=ext_num%1000; if(normalized>0 && canonical_base_id(base_id+normalized)==base_id) return normalized; return ext_num; }
static bool uses_normalized_ext_suffix(const Row& base,const Row& ext){ i64 e=to_int(ext[0]); return e!=0 && resolve_effective_ext_num(base,ext)!=e; }
static i64 resolve_ext_absolute_item_num(const Row& base,const Row& ext){ i64 base_id=to_int(base[0]), item_org=to_int(ext[2]), abs=to_int(ext[3]); if(item_org==base_id && abs>0 && canonical_base_id(abs)==base_id) return abs; return 0; }
static i64 resolve_final_num(const Row& base,const Row& ext){ i64 abs=resolve_ext_absolute_item_num(base,ext); if(abs>0) return abs; return to_int(base[0]) + resolve_effective_ext_num(base,ext); }
static bool is_cospre_zero_placeholder_ext(const Row& b,const Row& e){ if(b.size()!=41||e.size()!=56) return false; return to_int(b[10])==ITEM_CLASS_COSPLAY && cospre_skip_slot((int)to_int(b[12])) && to_int(e[0])==0 && to_int(e[2])==0 && to_int(e[7])==ITEM_ATTRIB_GENERAL && is_generic_ext_name(e[1]); }
static bool is_cospre_single_base_zero_placeholder_ext(const Row& b,const Row& e){ if(b.size()!=41||e.size()!=56) return false; return to_int(b[10])==ITEM_CLASS_COSPLAY && cospre_slot_single((int)to_int(b[12])) && to_int(e[0])==0 && to_int(e[2])==0 && to_int(e[7])==ITEM_ATTRIB_GENERAL && is_generic_ext_name(e[1]); }
static int resolve_output_item_type(const Row& b,const Row& e){ int item_type=(int)to_int(e[7]); if(item_type==ITEM_ATTRIB_GENERAL && is_cospre_single_base_zero_placeholder_ext(b,e)) return ITEM_ATTRIB_COSPRE; if(item_type==ITEM_ATTRIB_GENERAL && is_old_accessory_base(b) && is_global_zero_placeholder_ext(e)) return ITEM_ATTRIB_UNIQUE; return item_type; }

static bool is_candidate_generatable_for_base(const Row& b,const Row& e,int is_unique,const std::unordered_set<i64>& seen){ if(e.size()!=56) return false; i64 item_org=to_int(e[2]), ext_num=to_int(e[0]); if(item_org==0 && is_unique==1 && ext_num!=0) return false; i64 base_id=to_int(b[0]), final_num=resolve_final_num(b,e); if(canonical_base_id(final_num)!=base_id) return false; if(seen.count(final_num)) return false; return true; }
static bool has_real_nonzero_variant(const Row& b,const Rows& c,int is_unique,const std::unordered_set<i64>& seen){ for(auto& e:c){ if(to_int(e[0])==0) continue; if(is_candidate_generatable_for_base(b,e,is_unique,seen)) return true; } return false; }
static bool has_real_nonzero_cospre_variant(const Row& b,const Rows& c,int is_unique,const std::unordered_set<i64>& seen){ if(to_int(b[10])!=ITEM_CLASS_COSPLAY) return false; return has_real_nonzero_variant(b,c,is_unique,seen); }
static bool is_upgradeable_zero_placeholder_ext(const Row& e){ return is_global_zero_placeholder_ext(e); }
static bool is_starter_basic_item_placeholder(const Row& b,const Row& e,bool has_nonzero){ if(has_nonzero) return false; if(b.size()!=41||e.size()!=56) return false; if(!is_global_zero_placeholder_ext(e)) return false; if(to_int(b[1])!=22) return false; std::string d=lower(normalize_display_tokens(b[3])); return d=="basic warrior item"||d=="basic rogue item"||d=="basic magician item"||d=="basic priest item"; }
static bool has_specific_unique_reverse_variant(const Row& b,const Rows& direct){ i64 base_id=to_int(b[0]); for(auto& c:direct){ if(c.size()!=56) continue; if(to_int(c[2])!=base_id||to_int(c[0])==0||to_int(c[7])!=ITEM_ATTRIB_UNIQUE_REVERSE) continue; if(canonical_base_id(resolve_final_num(b,c))==base_id) return true; } return false; }
static Rows filter_unique_reverse_generic_conflicts(const Row& b,const Rows& generic,const Rows& direct,Stats& stats){ if(!has_specific_unique_reverse_variant(b,direct)) return generic; Rows out; for(auto& r:generic){ int item_type=(int)to_int(r[7]); i64 ext_num=to_int(r[0]); if(ext_num!=0 && item_type==ITEM_ATTRIB_UPGRADE_REVERSE){ stats["skipped_unique_reverse_generic_type11"]++; continue; } if(is_global_zero_placeholder_ext(r)){ stats["skipped_unique_reverse_zero_placeholder"]++; continue; } out.push_back(r); } return out; }

static Rows keep_old_accessory_direct_unique_candidates(const Row& b,const Rows& candidates,Stats& stats){ if(!is_old_accessory_base(b)) return candidates; i64 base_id=to_int(b[0]); Rows direct; for(auto& e:candidates) if(to_int(e[2])==base_id && to_int(e[0])!=0 && to_int(e[7])==ITEM_ATTRIB_UNIQUE) direct.push_back(e); if(!direct.empty()){ stats["old_accessory_direct_unique_rows"] += (i64)direct.size(); stats["skipped_old_accessory_generic_or_zero"] += (i64)candidates.size() - (i64)direct.size(); return direct; } Rows zero; for(auto& e:candidates) if(is_global_zero_placeholder_ext(e)) zero.push_back(e); if(!zero.empty()){ stats["old_accessory_base_unique_rows"] += (i64)zero.size(); stats["skipped_old_accessory_generic_without_direct_unique"] += (i64)candidates.size() - (i64)zero.size(); return zero; } stats["skipped_old_accessory_without_direct_unique"] += (i64)candidates.size(); return {}; }

static std::string normalize_cospre_display_name(const Row& base,const Row& ext){ std::string base_name=normalize_display_tokens(base[2]); std::string ext_name=normalize_display_tokens(ext[1]); std::string ext_key=lower(ext_name); if(to_int(base[10])==ITEM_CLASS_COSPLAY && to_int(base[12])==ITEM_SLOT_PET) return base_name; if(is_special_cospre_variant_base(base)) return base_name; if(is_generic_ext_name(ext_name)) return base_name; if(ext_key=="nreids") return "Nereids"; if(ext_key=="driads") return "Dryad"; return ext_name.empty()?base_name:ext_name; }
static int resolve_talisman_enhance_level(const Row& base,const Row& ext){ if(!is_talisman_base(base)) return 0; auto suffixes=talisman_suffixes_by_race((int)to_int(base[13])); if(suffixes.empty()) return 0; i64 ext_num=resolve_effective_ext_num(base,ext); for(size_t i=0;i<suffixes.size();++i) if(suffixes[i]==ext_num) return (int)i+1; return 0; }
static int resolve_row_grade(const Row& b,const Row& e){ int t=resolve_talisman_enhance_level(b,e); return t>0?t:resolve_item_enhance_level(b,e); }
static std::string build_tooltip_display_name(const Row& base,const Row& ext){ int item_type=(int)to_int(ext[7]); int base_kind=(int)to_int(base[10]); bool is_cospre = item_type==ITEM_ATTRIB_COSPRE || base_kind==ITEM_CLASS_COSPLAY; int t=resolve_talisman_enhance_level(base,ext); if(t>0) return normalize_talisman_base_name(base[2]) + "(+" + std::to_string(t) + ")"; if(is_talisman_base(base)) return normalize_talisman_base_name(base[2]); if(base_kind==ITEM_CLASS_JAMADAR && !(item_type==ITEM_ATTRIB_UNIQUE||item_type==ITEM_ATTRIB_UNIQUE_REVERSE||item_type==ITEM_ATTRIB_COSPRE)) return base[2]; if(item_type!=ITEM_ATTRIB_UNIQUE && item_type!=ITEM_ATTRIB_UNIQUE_REVERSE && item_type!=ITEM_ATTRIB_COSPRE && !is_cospre){ int lv=resolve_item_enhance_level(base,ext); return base[2] + (lv>0 ? "(+"+std::to_string(lv)+")" : ""); } if(is_cospre) return normalize_cospre_display_name(base,ext); return ext[1].empty()?base[2]:ext[1]; }

static std::pair<std::vector<fs::path>, std::unordered_map<int,std::vector<fs::path>>> discover_files(const fs::path& dir){
    std::vector<fs::path> org; std::unordered_map<int,std::vector<fs::path>> extmap;
    std::regex org1(R"(^Item_Org_data\d+\.(sql|tbl|txt|csv)$)",std::regex::icase), org2(R"(^item_org(?:_us)?_data\d+\.(sql|tbl|txt|csv)$)",std::regex::icase);
    std::regex ex1(R"(^Item_Ext_(\d+)_+data\d+\.(sql|tbl|txt|csv)$)",std::regex::icase), ex2(R"(^item_ext_(\d+)(?:_us)?_data\d+\.(sql|tbl|txt|csv)$)",std::regex::icase);
    for(auto& de:fs::directory_iterator(dir)){ if(!de.is_regular_file()) continue; std::string name=de.path().filename().string(); std::smatch m; if(std::regex_match(name,org1)||std::regex_match(name,org2)){ org.push_back(de.path()); continue; } if(std::regex_match(name,m,ex1)||std::regex_match(name,m,ex2)){ int idx=std::stoi(m[1]); extmap[idx].push_back(de.path()); } }
    auto key=[](const fs::path& p){ return lower(p.filename().string()); };
    std::sort(org.begin(),org.end(),[&](auto&a,auto&b){return key(a)<key(b);});
    for(auto& kv:extmap) std::sort(kv.second.begin(),kv.second.end(),[&](auto&a,auto&b){return key(a)<key(b);});
    return {org,extmap};
}

struct Loaded { std::vector<fs::path> orgFiles; std::unordered_map<int,std::vector<fs::path>> extFiles; Rows orgRows; std::unordered_map<int,Rows> extRows; };
static Loaded load_source_rows(const fs::path& dir){ Loaded l; auto d=discover_files(dir); l.orgFiles=d.first; l.extFiles=d.second; for(auto& p:l.orgFiles){ Rows r=parse_source_rows(p); l.orgRows.insert(l.orgRows.end(),r.begin(),r.end()); } for(auto& kv:l.extFiles){ Rows rows; for(auto& p:kv.second){ Rows r=parse_source_rows(p); rows.insert(rows.end(),r.begin(),r.end()); } l.extRows[kv.first]=std::move(rows); } return l; }
static ExtLookup build_ext_lookup(const std::unordered_map<int,Rows>& extRows){ ExtLookup lookup; for(auto& kv:extRows){ ByItemOrg by; for(auto& r:kv.second) if(r.size()==56) by[to_int(r[2])].push_back(r); lookup[kv.first]=std::move(by); } return lookup; }

static std::unordered_map<i64,Rows> build_stage_template_variant_lookup(const Rows& org,const std::unordered_map<int,Rows>& extRows,Stats& stats){
    std::unordered_map<std::string,Rows> groups; std::unordered_map<std::string,int> raceBy, extBy; std::unordered_map<i64,Rows> result;
    for(auto& b:org){ if(!is_talisman_base(b)) continue; int race=(int)to_int(b[13]); if(talisman_suffixes_by_race(race).empty()) continue; int ext=(int)to_int(b[1]); std::string key=std::to_string(ext)+"|"+std::to_string(race)+"|"+talisman_family_key(b[2]); groups[key].push_back(b); raceBy[key]=race; extBy[key]=ext; }
    for(auto& kv:groups){ int race=raceBy[kv.first], extIdx=extBy[kv.first]; auto suffixes=talisman_suffixes_by_race(race); auto counts=talisman_stage_counts_by_race(race); if(suffixes.empty()||counts.empty()) continue; std::unordered_map<int,Row> bySuffix; auto it=extRows.find(extIdx); if(it==extRows.end()) continue; for(auto& r:it->second){ if(r.size()!=56) continue; int en=(int)to_int(r[0]); if(std::find(suffixes.begin(),suffixes.end(),en)==suffixes.end()) continue; if(to_int(r[2])==0) continue; bySuffix.emplace(en,r); }
        Rows orderedExt; for(int s:suffixes) if(bySuffix.count(s)) orderedExt.push_back(bySuffix[s]); if(orderedExt.size()!=suffixes.size()){ stats["stage_template_missing_suffix"] += (i64)suffixes.size()-(i64)orderedExt.size(); continue; }
        Rows bases=kv.second; std::sort(bases.begin(),bases.end(),[](const Row&a,const Row&b){ int ka=talisman_stage_sort_key(a[2]), kb=talisman_stage_sort_key(b[2]); if(ka!=kb) return ka<kb; return to_int(a[0])<to_int(b[0]); }); if(bases.size()!=counts.size()){ stats["stage_template_stage_count_mismatch"]++; continue; }
        size_t off=0; for(size_t i=0;i<bases.size();++i){ for(int j=0;j<counts[i] && off<orderedExt.size();++j,++off) result[to_int(bases[i][0])].push_back(orderedExt[off]); }
        stats["stage_template_groups"]++; stats["stage_template_candidate_rows"] += (i64)std::accumulate(counts.begin(),counts.end(),0);
    }
    return result;
}

static std::unordered_map<i64,Rows> build_special_cospre_variant_lookup(const Rows& org,const ExtLookup& lookup,Stats& stats){
    std::unordered_map<std::string,std::vector<std::pair<Row,Rows>>> directBy; std::unordered_map<i64,Rows> result;
    for(auto& b:org){ if(b.size()!=41||to_int(b[10])!=ITEM_CLASS_COSPLAY) continue; i64 base_id=to_int(b[0]); int extIdx=(int)to_int(b[1]); auto it=lookup.find(extIdx); if(it==lookup.end()) continue; auto jt=it->second.find(base_id); if(jt==it->second.end()) continue; Rows direct; for(auto& r:jt->second) if(r.size()==56 && to_int(r[0])!=0 && to_int(r[7])==ITEM_ATTRIB_COSPRE) direct.push_back(r); if(direct.empty()) continue; std::string fam=normalize_family_name(b[2]); if(fam.empty()) continue; std::string key=std::to_string(extIdx)+"|"+std::to_string(to_int(b[10]))+"|"+std::to_string(to_int(b[12]))+"|"+std::to_string(to_int(b[13]))+"|"+std::to_string(to_int(b[14]))+"|"+fam; directBy[key].push_back({b,direct}); }
    for(auto& b:org){ if(!is_special_cospre_variant_base(b)) continue; i64 base_id=to_int(b[0]); int extIdx=(int)to_int(b[1]); auto it=lookup.find(extIdx); if(it!=lookup.end()){ auto jt=it->second.find(base_id); if(jt!=it->second.end()){ bool any=false; for(auto& r:jt->second) if(r.size()==56&&to_int(r[0])!=0) any=true; if(any) continue; } } std::string fam=normalize_family_name(b[2]); std::string key=std::to_string(extIdx)+"|"+std::to_string(to_int(b[10]))+"|"+std::to_string(to_int(b[12]))+"|"+std::to_string(to_int(b[13]))+"|"+std::to_string(to_int(b[14]))+"|"+fam; auto sit=directBy.find(key); if(sit==directBy.end()||sit->second.size()!=1){ if(sit!=directBy.end()&&sit->second.size()>1) stats["special_cospre_template_ambiguous"]++; continue; } for(auto src:sit->second[0].second){ Row cp=src; cp[1]=normalize_display_tokens(b[2]); result[base_id].push_back(cp); } stats["special_cospre_template_bases"]++; stats["special_cospre_template_rows"] += (i64)sit->second[0].second.size(); }
    return result;
}

static i64 merge_requirement(const std::string& bv,const std::string& ev){ i64 b=to_int(bv); return b<=0?0:b+to_int(ev); }
static std::vector<std::string> build_item_row_values(const Row& base,const Row& ext){
    i64 final_num=resolve_final_num(base,ext); int item_type=resolve_output_item_type(base,ext); std::vector<std::string> v; v.reserve(USKO_ITEM_COLUMNS.size());
    auto add=[&](i64 x){ v.push_back(std::to_string(x)); };
    add(final_num); add(to_int(base[1])); v.push_back(build_tooltip_display_name(base,ext)); add(to_int(base[10])); add(to_int(base[12])); add(to_int(base[13])); add(to_int(base[14])); add(to_int(base[15])+to_int(ext[8])); add(to_int(base[16])); add(to_int(base[17])); add(to_int(base[18])); add(to_int(base[19])+to_int(ext[12])); add(to_int(base[20])*to_int(ext[13])); add(to_int(base[21])*to_int(ext[13])); add(0); add(0); add(to_int(base[22])+to_int(ext[14])); add(to_int(base[23])); add(to_int(base[24])); add(to_int(base[25])); add(merge_requirement(base[26],ext[46])); add(to_int(base[27])); add(merge_requirement(base[28],ext[47])); add(merge_requirement(base[29],ext[48])); add(merge_requirement(base[30],ext[49])); add(merge_requirement(base[31],ext[50])); add(merge_requirement(base[32],ext[51])); add(merge_requirement(base[33],ext[52])); add(merge_requirement(base[34],ext[53])); add(to_int(base[35])); add(item_type); add(to_int(ext[10])); add(to_int(ext[11])); add(to_int(ext[15])); add(to_int(ext[16])); add(to_int(ext[17])); add(to_int(ext[18])); add(to_int(ext[19])); add(to_int(ext[20])); add(to_int(ext[21])); add(to_int(ext[22])); add(to_int(ext[23])); add(to_int(ext[24])); add(to_int(ext[25])); add(to_int(ext[26])); add(to_int(ext[27])); add(to_int(ext[28])); add(to_int(ext[29])); add(0); add(to_int(ext[31])); add(to_int(ext[32])); add(to_int(ext[33])); add(to_int(ext[34])); add(to_int(ext[35])); add(to_int(ext[36])); add(to_int(ext[37])); add(to_int(ext[38])); add(to_int(ext[39])); add(to_int(ext[40])); add(to_int(ext[41])); add(to_int(ext[42])); add(to_int(ext[43])); add(to_int(base[36])); add(0); add(to_int(ext[30])); add(resolve_row_grade(base,ext)); add(0); add(0); return v;
}

static std::vector<std::vector<std::string>> iter_merged_rows(const Rows& org,const ExtLookup& lookup,Stats& stats,const std::unordered_map<i64,Rows>& stage,const std::unordered_map<i64,Rows>& special){
    std::vector<std::vector<std::string>> rows; std::unordered_set<i64> seen, basesOut;
    for(auto& base:org){ if(base.size()!=41){ stats["bad_org_rows"]++; continue; } i64 base_id=to_int(base[0]); int extIdx=(int)to_int(base[1]); int is_unique=(int)to_int(base[4]); auto it=lookup.find(extIdx); if(it==lookup.end()){ stats["missing_ext_table"]++; continue; }
        Rows generic, direct, candidates; auto g=it->second.find(0); if(g!=it->second.end()) generic=g->second; auto d=it->second.find(base_id); if(d!=it->second.end()) direct=d->second; generic=filter_unique_reverse_generic_conflicts(base,generic,direct,stats); candidates=generic; candidates.insert(candidates.end(),direct.begin(),direct.end());
        std::set<std::tuple<i64,i64,std::string>> keys; for(auto& r:candidates) keys.insert({to_int(r[0]),to_int(r[2]),r[1]}); auto addExtra=[&](const std::unordered_map<i64,Rows>& mp){ auto ex=mp.find(base_id); if(ex!=mp.end()) for(auto& r:ex->second){ auto k=std::make_tuple(to_int(r[0]),to_int(r[2]),r[1]); if(!keys.count(k)){ candidates.push_back(r); keys.insert(k);} } }; addExtra(stage); addExtra(special);
        candidates=keep_old_accessory_direct_unique_candidates(base,candidates,stats); if(candidates.empty()){ stats["base_without_ext_match"]++; continue; }
        bool hasNonzero=has_real_nonzero_variant(base,candidates,is_unique,seen); bool suppressCospre=has_real_nonzero_cospre_variant(base,candidates,is_unique,seen);
        for(auto& ext:candidates){ i64 item_org=to_int(ext[2]), ext_num=to_int(ext[0]); if(is_starter_basic_item_placeholder(base,ext,hasNonzero)){ stats["skipped_starter_basic_zero_placeholder"]++; continue; } if(hasNonzero && is_upgradeable_zero_placeholder_ext(ext)){ stats["skipped_base_zero_when_nonzero_variant"]++; continue; } if(suppressCospre && is_cospre_zero_placeholder_ext(base,ext)){ stats["skipped_cospre_zero_placeholder"]++; continue; } if(item_org==0 && is_unique==1 && ext_num!=0){ stats["skipped_unique_normal_variant"]++; continue; }
            i64 final_num=resolve_final_num(base,ext); if(canonical_base_id(final_num)!=base_id){ stats["skipped_noncanonical_client_lookup"]++; if(resolve_ext_absolute_item_num(base,ext)>0) stats["skipped_absolute_itemnum_noncanonical"]++; continue; } if(resolve_ext_absolute_item_num(base,ext)>0) stats["generated_absolute_itemnum_rows"]++; if(seen.count(final_num)){ stats["skipped_duplicate_num"]++; continue; }
            seen.insert(final_num); basesOut.insert(base_id); auto row=build_item_row_values(base,ext); if(uses_normalized_ext_suffix(base,ext)) stats["generated_normalized_ext_suffix_rows"]++; if(is_cospre_single_base_zero_placeholder_ext(base,ext)) stats["cospre_single_base_itemtype8_rows"]++; if(resolve_talisman_enhance_level(base,ext)>0) stats["stage_template_generated_rows"]++; auto sp=special.find(base_id); if(sp!=special.end()){ for(auto& src:sp->second) if(to_int(ext[0])==to_int(src[0])) {stats["special_cospre_template_generated_rows"]++; break;} }
            stats["by_item_type_"+row[30]]++; stats["by_extension_"+std::to_string(extIdx)]++; rows.push_back(std::move(row)); }
    }
    stats["base_rows_with_output"]=(i64)basesOut.size(); stats["unique_nums"]=(i64)seen.size(); stats["generated_rows"]=(i64)rows.size(); return rows;
}

static void write_sql(const std::vector<std::vector<std::string>>& rows,const fs::path& out,int batch){
    std::ofstream h(out,std::ios::binary); h << "IF OBJECT_ID('USKO_ITEM', 'U') IS NOT NULL DROP TABLE USKO_ITEM;\nGO\nCREATE TABLE USKO_ITEM (\n"; for(size_t i=0;i<USKO_ITEM_COLUMNS.size();++i){ h << "  " << USKO_ITEM_COLUMNS[i] << " " << (USKO_ITEM_COLUMNS[i]=="strName"?"VARCHAR(255)":"INT") << (i+1<USKO_ITEM_COLUMNS.size()?",":"") << "\n"; } h << ");\nGO\n";
    std::vector<std::string> vals; vals.reserve(batch); int count=0; for(auto& r:rows){ std::ostringstream oss; oss << "("; for(size_t i=0;i<r.size();++i){ if(i) oss << ","; if(USKO_ITEM_COLUMNS[i]=="strName") oss << sql_string(r[i]); else oss << to_int(r[i]); } oss << ")"; vals.push_back(oss.str()); count++; if((int)vals.size()>=batch){ h << "INSERT INTO USKO_ITEM VALUES\n"; for(size_t i=0;i<vals.size();++i) h << vals[i] << (i+1<vals.size()?",\n":";\nGO\n"); vals.clear(); } }
    if(!vals.empty()){ h << "INSERT INTO USKO_ITEM VALUES\n"; for(size_t i=0;i<vals.size();++i) h << vals[i] << (i+1<vals.size()?",\n":";\nGO\n"); }
}
static void write_report(const fs::path& report,const Loaded& l,const Stats& stats,const fs::path& sqlDir,const fs::path& out){ std::ofstream h(report); h << "USKO_ITEM C++ build report\n==========================\n"; h << "sql_dir: "<<sqlDir.string()<<"\n"; h << "out_file: "<<out.string()<<"\n"; h << "org_files: "<<l.orgFiles.size()<<"\norg_rows: "<<l.orgRows.size()<<"\n"; size_t extFiles=0, extRows=0; for(auto& kv:l.extFiles) extFiles+=kv.second.size(); for(auto& kv:l.extRows) extRows+=kv.second.size(); h << "ext_files: "<<extFiles<<"\next_tables: "<<l.extRows.size()<<"\next_rows: "<<extRows<<"\n"; for(auto& kv:stats) h << kv.first << ": " << kv.second << "\n"; }


using ProgressFn = std::function<void(int)>;

static int RunUSKOItemBuilder(const fs::path& sqlDir, const fs::path& out, const fs::path& report, std::string& logText, const ProgressFn& progress = {}) {
    const int batch = 1000;
    auto setProgress = [&](int value) { if (progress) progress(std::clamp(value, 0, 100)); };
    std::ostringstream log;
    try {
        setProgress(4);
        Stats stats;
        Loaded loaded = load_source_rows(sqlDir);
        setProgress(26);
        if (loaded.orgRows.empty() || loaded.extRows.empty()) {
            log << "Item_Org / Item_Ext source files not found.\n";
            log << "Source folder: " << sqlDir.string() << "\n";
            logText = log.str();
            setProgress(0);
            return 2;
        }

        auto lookup = build_ext_lookup(loaded.extRows);
        setProgress(38);
        auto stage = build_stage_template_variant_lookup(loaded.orgRows, loaded.extRows, stats);
        auto special = build_special_cospre_variant_lookup(loaded.orgRows, lookup, stats);
        setProgress(52);
        auto rows = iter_merged_rows(loaded.orgRows, lookup, stats, stage, special);
        setProgress(72);

        write_sql(rows, out, batch);
        setProgress(90);
        write_report(report, loaded, stats, sqlDir, out);
        setProgress(98);

        size_t extFiles = 0;
        size_t extRows = 0;
        for (const auto& kv : loaded.extFiles) extFiles += kv.second.size();
        for (const auto& kv : loaded.extRows) extRows += kv.second.size();

        log << "USKO_ITEM.sql generated successfully.\n\n";
        log << "Source folder: " << sqlDir.string() << "\n";
        log << "Output SQL: " << out.string() << "\n";
        log << "Report: " << report.string() << "\n\n";
        log << "Org files: " << loaded.orgFiles.size() << "\n";
        log << "Org rows: " << loaded.orgRows.size() << "\n";
        log << "Ext files: " << extFiles << "\n";
        log << "Ext tables: " << loaded.extRows.size() << "\n";
        log << "Ext rows: " << extRows << "\n";
        log << "Generated rows: " << rows.size() << "\n\n";

        auto printStat = [&](const char* key) {
            auto it = stats.find(key);
            if (it != stats.end())
                log << key << ": " << it->second << "\n";
        };

        printStat("generated_normalized_ext_suffix_rows");
        printStat("generated_absolute_itemnum_rows");
        printStat("stage_template_generated_rows");
        printStat("cospre_single_base_itemtype8_rows");
        printStat("old_accessory_direct_unique_rows");
        printStat("old_accessory_base_unique_rows");
        printStat("skipped_noncanonical_client_lookup");
        printStat("skipped_duplicate_num");

        logText = log.str();
        setProgress(100);
        return 0;
    } catch (const std::exception& e) {
        log << "ERROR: " << e.what() << "\n";
        logText = log.str();
        return 1;
    }
}



#ifdef _WIN32
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <thread>

#include "../resources/resource.h"

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "UxTheme.lib")

// -----------------------------------------------------------------------------
// Native C++17 / Win32 UI. No external runtime or framework is required.
// ----------------------------------------------------------------------------

namespace ui {

static constexpr COLORREF C_BG          = RGB(10, 15, 23);
static constexpr COLORREF C_CARD        = RGB(17, 24, 35);
static constexpr COLORREF C_CARD_2      = RGB(20, 28, 40);
static constexpr COLORREF C_BORDER      = RGB(42, 55, 74);
static constexpr COLORREF C_BORDER_HOT  = RGB(62, 86, 116);
static constexpr COLORREF C_EDIT        = RGB(12, 18, 28);
static constexpr COLORREF C_TEXT        = RGB(236, 242, 248);
static constexpr COLORREF C_MUTED       = RGB(148, 163, 184);
static constexpr COLORREF C_ACCENT      = RGB(55, 151, 255);
static constexpr COLORREF C_ACCENT_HOT  = RGB(76, 166, 255);
static constexpr COLORREF C_ACCENT_DOWN = RGB(34, 124, 220);
static constexpr COLORREF C_SUCCESS     = RGB(63, 201, 142);
static constexpr COLORREF C_ERROR       = RGB(255, 99, 111);
static constexpr COLORREF C_WARN        = RGB(244, 190, 76);
static constexpr COLORREF C_BUTTON      = RGB(35, 45, 60);
static constexpr COLORREF C_BUTTON_HOT  = RGB(45, 58, 76);
static constexpr COLORREF C_BUTTON_DOWN = RGB(28, 37, 50);

static HINSTANCE g_hInst = nullptr;
static HWND g_hMain = nullptr;
static HWND g_hTitle = nullptr;
static HWND g_hSubtitle = nullptr;
static HWND g_hLangLabel = nullptr;
static HWND g_hLang = nullptr;
static HWND g_hSourceSection = nullptr;
static HWND g_hFolderLabel = nullptr;
static HWND g_hEditFolder = nullptr;
static HWND g_hBrowse = nullptr;
static HWND g_hOutLabel = nullptr;
static HWND g_hEditOut = nullptr;
static HWND g_hSaveAs = nullptr;
static HWND g_hReportLabel = nullptr;
static HWND g_hEditReport = nullptr;
static HWND g_hReportAs = nullptr;
static HWND g_hBuild = nullptr;
static HWND g_hOpenOut = nullptr;
static HWND g_hOpenWhenDone = nullptr;
static HWND g_hProgress = nullptr;
static HWND g_hStatus = nullptr;
static HWND g_hLogSection = nullptr;
static HWND g_hCopyLog = nullptr;
static HWND g_hClearLog = nullptr;
static HWND g_hLog = nullptr;

static HFONT g_hFont = nullptr;
static HFONT g_hSmallFont = nullptr;
static HFONT g_hTitleFont = nullptr;
static HFONT g_hSemibold = nullptr;
static HFONT g_hMonoFont = nullptr;
static HBRUSH g_bgBrush = nullptr;
static HBRUSH g_editBrush = nullptr;
static HBRUSH g_cardBrush = nullptr;
static HICON g_hIcon = nullptr;

static bool g_isRunning = false;
static bool g_english = false;
static int g_progress = 0;
static std::wstring g_lastStatus;

static constexpr UINT WM_APP_PROGRESS = WM_APP + 11;
static constexpr UINT WM_APP_DONE     = WM_APP + 12;

#define IDC_LANG          1100
#define IDC_FOLDER        1101
#define IDC_BROWSE        1102
#define IDC_OUT           1103
#define IDC_SAVEAS        1104
#define IDC_REPORT        1105
#define IDC_REPORTAS      1106
#define IDC_BUILD         1107
#define IDC_OPENOUT       1108
#define IDC_OPEN_DONE     1109
#define IDC_LOG           1110
#define IDC_COPY_LOG      1111
#define IDC_CLEAR_LOG     1112
#define IDC_PROGRESS      1113

struct L10n {
    const wchar_t* title;
    const wchar_t* subtitle;
    const wchar_t* language;
    const wchar_t* sourceSection;
    const wchar_t* itemFolder;
    const wchar_t* browse;
    const wchar_t* sqlOutput;
    const wchar_t* saveAs;
    const wchar_t* report;
    const wchar_t* build;
    const wchar_t* openFolder;
    const wchar_t* openWhenDone;
    const wchar_t* logSection;
    const wchar_t* copy;
    const wchar_t* clear;
    const wchar_t* ready;
    const wchar_t* running;
    const wchar_t* loading;
    const wchar_t* matching;
    const wchar_t* buildingRows;
    const wchar_t* writingSql;
    const wchar_t* writingReport;
    const wchar_t* success;
    const wchar_t* error;
    const wchar_t* missingInput;
    const wchar_t* selectFolderFirst;
    const wchar_t* folderMissing;
    const wchar_t* threadFailed;
    const wchar_t* completedTitle;
    const wchar_t* completedText;
    const wchar_t* failedTitle;
    const wchar_t* failedText;
    const wchar_t* pickFolderTitle;
    const wchar_t* sqlSaveTitle;
    const wchar_t* reportSaveTitle;
    const wchar_t* sqlFilter;
    const wchar_t* textFilter;
    const wchar_t* allFiles;
};

static const L10n TR = {
    L"TBL to SQL Studio",
    L"Item_Org / Item_Ext kaynaklarını doğrular ve USKO_ITEM.sql dosyasına dönüştürür.",
    L"Dil",
    L"Kaynak ve çıktı",
    L"Item klasörü",
    L"Gözat",
    L"SQL çıktısı",
    L"Farklı kaydet",
    L"Rapor",
    L"SQL Oluştur",
    L"Çıktı klasörünü aç",
    L"Tamamlanınca çıktı klasörünü aç",
    L"İşlem günlüğü",
    L"Kopyala",
    L"Temizle",
    L"Hazır",
    L"İşleniyor...",
    L"Kaynak dosyalar okunuyor...",
    L"Item eşleşmeleri hazırlanıyor...",
    L"USKO_ITEM satırları oluşturuluyor...",
    L"SQL dosyası yazılıyor...",
    L"Rapor hazırlanıyor...",
    L"Tamamlandı",
    L"Hata",
    L"Eksik giriş",
    L"Önce Item klasörünü seçin.",
    L"Seçilen klasör bulunamadı.",
    L"Arka plan işlemi başlatılamadı.",
    L"Tamamlandı",
    L"USKO_ITEM.sql başarıyla oluşturuldu.",
    L"İşlem başarısız",
    L"SQL oluşturulamadı. Ayrıntılar için işlem günlüğünü kontrol edin.",
    L"Item klasörünü seç",
    L"SQL çıktı dosyasını seç",
    L"Rapor dosyasını seç",
    L"SQL Dosyası (*.sql)",
    L"Metin Dosyası (*.txt)",
    L"Tüm Dosyalar (*.*)"
};

static const L10n EN = {
    L"TBL to SQL Studio",
    L"Validates Item_Org / Item_Ext sources and converts them into USKO_ITEM.sql.",
    L"Language",
    L"Source and output",
    L"Item folder",
    L"Browse",
    L"SQL output",
    L"Save as",
    L"Report",
    L"Build SQL",
    L"Open output folder",
    L"Open output folder when finished",
    L"Activity log",
    L"Copy",
    L"Clear",
    L"Ready",
    L"Processing...",
    L"Reading source files...",
    L"Preparing item matches...",
    L"Building USKO_ITEM rows...",
    L"Writing SQL file...",
    L"Writing report...",
    L"Completed",
    L"Error",
    L"Missing input",
    L"Select the Item folder first.",
    L"The selected folder could not be found.",
    L"The background worker could not be started.",
    L"Completed",
    L"USKO_ITEM.sql was generated successfully.",
    L"Build failed",
    L"The SQL build failed. Check the activity log for details.",
    L"Select Item folder",
    L"Select SQL output file",
    L"Select report file",
    L"SQL File (*.sql)",
    L"Text File (*.txt)",
    L"All Files (*.*)"
};

static const L10n& T() { return g_english ? EN : TR; }

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    UINT cp = CP_UTF8;
    if (n <= 0) {
        cp = CP_ACP;
        n = MultiByteToWideChar(cp, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    }
    if (n <= 0) return L"";
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(cp, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

static std::wstring GetText(HWND h) {
    const int len = GetWindowTextLengthW(h);
    if (len <= 0) return L"";
    std::wstring s(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(h, s.data(), len + 1);
    s.resize(static_cast<size_t>(len));
    return s;
}

static void SetText(HWND h, const std::wstring& s) {
    if (h) SetWindowTextW(h, s.c_str());
}

static std::wstring SettingsPath() {
    wchar_t appData[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    fs::path base;
    if (n > 0 && n < MAX_PATH) base = fs::path(appData) / L"TBLtoSQLStudio";
    else base = fs::temp_directory_path() / L"TBLtoSQLStudio";
    std::error_code ec;
    fs::create_directories(base, ec);
    return (base / L"settings.ini").wstring();
}

static void SaveSettings() {
    const std::wstring ini = SettingsPath();
    WritePrivateProfileStringW(L"UI", L"Language", g_english ? L"en" : L"tr", ini.c_str());
    const std::wstring folder = GetText(g_hEditFolder);
    if (!folder.empty()) WritePrivateProfileStringW(L"Paths", L"LastFolder", folder.c_str(), ini.c_str());
}

static void LoadSettings() {
    const std::wstring ini = SettingsPath();
    wchar_t lang[16] = {};
    GetPrivateProfileStringW(L"UI", L"Language", L"tr", lang, 16, ini.c_str());
    g_english = (_wcsicmp(lang, L"en") == 0);

    wchar_t folder[32768] = {};
    GetPrivateProfileStringW(L"Paths", L"LastFolder", L"", folder, 32768, ini.c_str());
    if (folder[0]) SetText(g_hEditFolder, folder);
}

static void SetDefaultOutputPaths(const std::wstring& folder) {
    if (folder.empty()) return;
    const fs::path p(folder);
    SetText(g_hEditOut, (p / L"USKO_ITEM.sql").wstring());
    SetText(g_hEditReport, (p / L"USKO_ITEM.report.txt").wstring());
}

static std::wstring PickFolder(HWND owner) {
    std::wstring result;
    IFileOpenDialog* dlg = nullptr;
    const HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (SUCCEEDED(hr) && dlg) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dlg->SetTitle(T().pickFolderTitle);
        if (SUCCEEDED(dlg->Show(owner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR psz = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                    result = psz;
                    CoTaskMemFree(psz);
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    return result;
}

static std::wstring BuildFilter(const wchar_t* desc, const wchar_t* pattern) {
    std::wstring filter;
    filter.append(desc); filter.push_back(L'\0');
    filter.append(pattern); filter.push_back(L'\0');
    filter.append(T().allFiles); filter.push_back(L'\0');
    filter.append(L"*.*"); filter.push_back(L'\0'); filter.push_back(L'\0');
    return filter;
}

static std::wstring SaveFileDialog(HWND owner, const wchar_t* title, const std::wstring& defName, const wchar_t* desc, const wchar_t* pattern, const wchar_t* defExt) {
    std::vector<wchar_t> fileName(32768, L'\0');
    wcsncpy_s(fileName.data(), fileName.size(), defName.c_str(), _TRUNCATE);
    std::wstring filter = BuildFilter(desc, pattern);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrTitle = title;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = fileName.data();
    ofn.nMaxFile = static_cast<DWORD>(fileName.size());
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrDefExt = defExt;
    if (GetSaveFileNameW(&ofn)) return fileName.data();
    return L"";
}

static void AppendLog(const std::wstring& text) {
    if (!g_hLog || text.empty()) return;
    int len = GetWindowTextLengthW(g_hLog);
    if (len > 300000) {
        SendMessageW(g_hLog, EM_SETSEL, 0, 100000);
        SendMessageW(g_hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
        len = GetWindowTextLengthW(g_hLog);
    }
    SendMessageW(g_hLog, EM_SETSEL, len, len);
    SendMessageW(g_hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(g_hLog, EM_SCROLLCARET, 0, 0);
}

static void CopyLogToClipboard(HWND owner) {
    const std::wstring log = GetText(g_hLog);
    if (log.empty() || !OpenClipboard(owner)) return;
    EmptyClipboard();
    const size_t bytes = (log.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem) {
        void* dst = GlobalLock(mem);
        if (dst) {
            memcpy(dst, log.c_str(), bytes);
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
            mem = nullptr;
        }
        if (mem) GlobalFree(mem);
    }
    CloseClipboard();
}

static void OpenOutputFolder() {
    const std::wstring out = GetText(g_hEditOut);
    if (out.empty()) return;
    fs::path p(out);
    fs::path folder = p.has_parent_path() ? p.parent_path() : fs::current_path();
    ShellExecuteW(nullptr, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void EnableBuildControls(bool enabled) {
    EnableWindow(g_hBuild, enabled ? TRUE : FALSE);
    EnableWindow(g_hBrowse, enabled ? TRUE : FALSE);
    EnableWindow(g_hSaveAs, enabled ? TRUE : FALSE);
    EnableWindow(g_hReportAs, enabled ? TRUE : FALSE);
    EnableWindow(g_hEditFolder, enabled ? TRUE : FALSE);
    EnableWindow(g_hEditOut, enabled ? TRUE : FALSE);
    EnableWindow(g_hEditReport, enabled ? TRUE : FALSE);
    EnableWindow(g_hLang, enabled ? TRUE : FALSE);
}

static void SetProgress(int value) {
    g_progress = std::clamp(value, 0, 100);
    if (g_hProgress) InvalidateRect(g_hProgress, nullptr, FALSE);

    if (g_isRunning) {
        if (g_progress < 25) g_lastStatus = T().loading;
        else if (g_progress < 50) g_lastStatus = T().matching;
        else if (g_progress < 75) g_lastStatus = T().buildingRows;
        else if (g_progress < 94) g_lastStatus = T().writingSql;
        else g_lastStatus = T().writingReport;
        SetText(g_hStatus, g_lastStatus);
    }
}

static void ApplyLanguage() {
    SetText(g_hMain, T().title);
    SetText(g_hTitle, T().title);
    SetText(g_hSubtitle, T().subtitle);
    SetText(g_hLangLabel, T().language);
    SetText(g_hSourceSection, T().sourceSection);
    SetText(g_hFolderLabel, T().itemFolder);
    SetText(g_hBrowse, T().browse);
    SetText(g_hOutLabel, T().sqlOutput);
    SetText(g_hSaveAs, T().saveAs);
    SetText(g_hReportLabel, T().report);
    SetText(g_hReportAs, T().saveAs);
    SetText(g_hBuild, T().build);
    SetText(g_hOpenOut, T().openFolder);
    SetText(g_hOpenWhenDone, T().openWhenDone);
    SetText(g_hLogSection, T().logSection);
    SetText(g_hCopyLog, T().copy);
    SetText(g_hClearLog, T().clear);
    if (!g_isRunning) SetText(g_hStatus, T().ready);
    else SetProgress(g_progress);
    InvalidateRect(g_hMain, nullptr, TRUE);
}

static void DrawRoundedRect(HDC dc, const RECT& r, COLORREF fill, COLORREF border, int radius = 14) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldB = SelectObject(dc, b);
    HGDIOBJ oldP = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldB);
    SelectObject(dc, oldP);
    DeleteObject(b);
    DeleteObject(p);
}

static void DrawButton(const DRAWITEMSTRUCT* dis) {
    if (!dis) return;
    RECT r = dis->rcItem;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool selected = (dis->itemState & ODS_SELECTED) != 0;
    const bool isPrimary = dis->CtlID == IDC_BUILD;

    COLORREF fill = isPrimary ? C_ACCENT : C_BUTTON;
    COLORREF border = isPrimary ? C_ACCENT_HOT : C_BORDER;
    COLORREF textColor = C_TEXT;
    if (selected) fill = isPrimary ? C_ACCENT_DOWN : C_BUTTON_DOWN;
    if (disabled) {
        fill = RGB(30, 36, 46);
        border = RGB(48, 56, 68);
        textColor = RGB(111, 123, 139);
    }

    DrawRoundedRect(dis->hDC, r, fill, border, 12);
    wchar_t label[256] = {};
    GetWindowTextW(dis->hwndItem, label, 256);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, textColor);
    HGDIOBJ oldFont = SelectObject(dis->hDC, g_hSemibold ? g_hSemibold : g_hFont);
    DrawTextW(dis->hDC, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dis->hDC, oldFont);
}

static void DrawComboItem(const DRAWITEMSTRUCT* dis) {
    if (!dis) return;
    HBRUSH bg = CreateSolidBrush((dis->itemState & ODS_SELECTED) ? C_BUTTON_HOT : C_EDIT);
    FillRect(dis->hDC, &dis->rcItem, bg);
    DeleteObject(bg);

    if (dis->itemID != static_cast<UINT>(-1)) {
        wchar_t text[128] = {};
        SendMessageW(dis->hwndItem, CB_GETLBTEXT, dis->itemID, reinterpret_cast<LPARAM>(text));
        RECT r = dis->rcItem;
        r.left += 12;
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, C_TEXT);
        HGDIOBJ oldFont = SelectObject(dis->hDC, g_hFont);
        DrawTextW(dis->hDC, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dis->hDC, oldFont);
    }
}

static LRESULT CALLBACK ProgressWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{}; GetClientRect(hwnd, &rc);
        const int w = std::max(1L, rc.right - rc.left);
        const int h = std::max(1L, rc.bottom - rc.top);

        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
        HGDIOBJ oldBmp = SelectObject(mem, bmp);

        DrawRoundedRect(mem, rc, RGB(26, 34, 46), RGB(47, 60, 78), h);

        RECT inner = rc;
        if (g_progress > 0) {
            RECT fill = inner;
            fill.right = fill.left + static_cast<LONG>((w * g_progress) / 100.0);
            if (fill.right > fill.left) DrawRoundedRect(mem, fill, C_ACCENT, C_ACCENT_HOT, h);
        }

        wchar_t pct[16] = {};
        swprintf_s(pct, L"%d%%", g_progress);
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, C_TEXT);
        HGDIOBJ oldFont = SelectObject(mem, g_hSemibold ? g_hSemibold : g_hFont);
        DrawTextW(mem, pct, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(mem, oldFont);

        BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static HWND MakeLabel(HWND parent, const wchar_t* text, int id = 0) {
    HWND h = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", text, WS_CHILD | WS_VISIBLE,
        0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInst, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
    return h;
}

static HWND MakeEdit(HWND parent, int id, bool multi = false) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
    if (multi) style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY;
    HWND h = CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 10, 10, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInst, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(multi ? g_hMonoFont : g_hFont), TRUE);
    SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
    return h;
}

static HWND MakeButton(HWND parent, int id, const wchar_t* text) {
    HWND h = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInst, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_hSemibold), TRUE);
    return h;
}

static HWND MakeCheck(HWND parent, int id, const wchar_t* text) {
    HWND h = CreateWindowExW(WS_EX_TRANSPARENT, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInst, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
    SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
    return h;
}

static HWND MakeCombo(HWND parent, int id) {
    HWND h = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
        0, 0, 10, 200, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInst, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
    SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
    return h;
}

static void Move(HWND h, int x, int y, int w, int hgt) {
    if (h) MoveWindow(h, x, y, std::max(1, w), std::max(1, hgt), TRUE);
}

static void LayoutControls(HWND hwnd) {
    RECT rc{}; GetClientRect(hwnd, &rc);
    const int W = rc.right - rc.left;
    const int H = rc.bottom - rc.top;
    const int m = 28;
    const int right = W - m;
    const int contentW = right - m;

    Move(g_hTitle, m, 20, 420, 36);
    Move(g_hSubtitle, m, 58, std::max(300, contentW - 230), 26);
    Move(g_hLangLabel, right - 190, 24, 74, 22);
    Move(g_hLang, right - 112, 20, 112, 230);

    const int cardTop = 104;
    const int cardHeight = 300;
    const int x = m + 18;
    const int labelW = 118;
    const int btnW = g_english ? 138 : 128;
    const int inputX = x + labelW;
    const int inputW = std::max(180, contentW - 36 - labelW - btnW - 12);

    Move(g_hSourceSection, m + 18, cardTop + 14, 300, 25);
    Move(g_hFolderLabel, x, cardTop + 58, labelW - 8, 24);
    Move(g_hEditFolder, inputX, cardTop + 52, inputW, 36);
    Move(g_hBrowse, inputX + inputW + 12, cardTop + 52, btnW, 36);

    Move(g_hOutLabel, x, cardTop + 108, labelW - 8, 24);
    Move(g_hEditOut, inputX, cardTop + 102, inputW, 36);
    Move(g_hSaveAs, inputX + inputW + 12, cardTop + 102, btnW, 36);

    Move(g_hReportLabel, x, cardTop + 158, labelW - 8, 24);
    Move(g_hEditReport, inputX, cardTop + 152, inputW, 36);
    Move(g_hReportAs, inputX + inputW + 12, cardTop + 152, btnW, 36);

    Move(g_hBuild, inputX, cardTop + 205, g_english ? 158 : 150, 42);
    Move(g_hOpenOut, inputX + (g_english ? 170 : 162), cardTop + 205, g_english ? 190 : 182, 42);
    Move(g_hOpenWhenDone, x, cardTop + 255, std::max(360, contentW - 36), 30);

    const int progressY = cardTop + cardHeight + 18;
    Move(g_hProgress, m, progressY, contentW, 28);
    Move(g_hStatus, m, progressY + 36, contentW, 24);

    const int logTop = progressY + 78;
    Move(g_hLogSection, m + 18, logTop + 14, 240, 25);
    Move(g_hCopyLog, right - 190, logTop + 10, 82, 32);
    Move(g_hClearLog, right - 98, logTop + 10, 80, 32);
    const int logBottom = H - m;
    Move(g_hLog, m + 18, logTop + 52, contentW - 36, std::max(110, logBottom - (logTop + 70)));

    InvalidateRect(hwnd, nullptr, TRUE);
}

static void PaintBackground(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc{}; GetClientRect(hwnd, &rc);
    FillRect(dc, &rc, g_bgBrush);

    const int m = 28;
    const int W = rc.right;
    const int H = rc.bottom;
    RECT sourceCard{ m, 104, W - m, 404 };
    DrawRoundedRect(dc, sourceCard, C_CARD, C_BORDER, 18);

    const int progressY = 104 + 300 + 18;
    const int logTop = progressY + 78;
    RECT logCard{ m, logTop, W - m, H - m };
    DrawRoundedRect(dc, logCard, C_CARD, C_BORDER, 18);

    EndPaint(hwnd, &ps);
}

struct BuildResult {
    int code = 1;
    std::wstring log;
};

static void StartBuild(HWND hwnd) {
    if (g_isRunning) return;

    std::wstring folder = GetText(g_hEditFolder);
    std::wstring out = GetText(g_hEditOut);
    std::wstring report = GetText(g_hEditReport);

    if (folder.empty()) {
        MessageBoxW(hwnd, T().selectFolderFirst, T().missingInput, MB_OK | MB_ICONWARNING);
        return;
    }
    std::error_code ec;
    if (!fs::is_directory(fs::path(folder), ec)) {
        MessageBoxW(hwnd, T().folderMissing, T().error, MB_OK | MB_ICONERROR);
        return;
    }
    if (out.empty()) out = (fs::path(folder) / L"USKO_ITEM.sql").wstring();
    if (report.empty()) report = (fs::path(folder) / L"USKO_ITEM.report.txt").wstring();

    if (fs::path(out).has_parent_path()) fs::create_directories(fs::path(out).parent_path(), ec);
    if (fs::path(report).has_parent_path()) fs::create_directories(fs::path(report).parent_path(), ec);

    SetText(g_hEditOut, out);
    SetText(g_hEditReport, report);
    SaveSettings();

    SetText(g_hLog, L"");
    AppendLog(g_english ? L"SQL build started...\r\n\r\n" : L"SQL oluşturma işlemi başlatıldı...\r\n\r\n");
    g_isRunning = true;
    EnableBuildControls(false);
    SetProgress(1);
    SetText(g_hStatus, T().running);

    try {
        std::thread([folder, out, report]() {
            auto* result = new BuildResult();
            std::string log;
            auto progress = [](int value) {
                if (g_hMain) PostMessageW(g_hMain, WM_APP_PROGRESS, static_cast<WPARAM>(value), 0);
            };
            result->code = RunUSKOItemBuilder(fs::path(folder), fs::path(out), fs::path(report), log, progress);
            result->log = Utf8ToWide(log);
            if (!result->log.empty() && result->log.back() != L'\n') result->log += L"\r\n";
            if (g_hMain) PostMessageW(g_hMain, WM_APP_DONE, 0, reinterpret_cast<LPARAM>(result));
            else delete result;
        }).detach();
    } catch (...) {
        g_isRunning = false;
        EnableBuildControls(true);
        SetProgress(0);
        SetText(g_hStatus, T().error);
        MessageBoxW(hwnd, T().threadFailed, T().error, MB_OK | MB_ICONERROR);
    }
}

static void ApplyDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)); // DWMWA_USE_IMMERSIVE_DARK_MODE
    const DWORD corner = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner)); // DWMWA_WINDOW_CORNER_PREFERENCE
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_hSmallFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_hSemibold = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Semibold");
        g_hTitleFont = CreateFontW(-30, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Semibold");
        g_hMonoFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            FIXED_PITCH | FF_MODERN, L"Cascadia Mono");
        if (!g_hMonoFont) g_hMonoFont = g_hSmallFont;

        g_bgBrush = CreateSolidBrush(C_BG);
        g_editBrush = CreateSolidBrush(C_EDIT);
        g_cardBrush = CreateSolidBrush(C_CARD);

        ApplyDarkTitleBar(hwnd);
        DragAcceptFiles(hwnd, TRUE);

        g_hTitle = MakeLabel(hwnd, L"");
        SendMessageW(g_hTitle, WM_SETFONT, reinterpret_cast<WPARAM>(g_hTitleFont), TRUE);
        g_hSubtitle = MakeLabel(hwnd, L"");
        SendMessageW(g_hSubtitle, WM_SETFONT, reinterpret_cast<WPARAM>(g_hSmallFont), TRUE);
        g_hLangLabel = MakeLabel(hwnd, L"");
        g_hLang = MakeCombo(hwnd, IDC_LANG);
        SendMessageW(g_hLang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Türkçe"));
        SendMessageW(g_hLang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));

        g_hSourceSection = MakeLabel(hwnd, L"");
        SendMessageW(g_hSourceSection, WM_SETFONT, reinterpret_cast<WPARAM>(g_hSemibold), TRUE);
        g_hFolderLabel = MakeLabel(hwnd, L"");
        g_hEditFolder = MakeEdit(hwnd, IDC_FOLDER);
        g_hBrowse = MakeButton(hwnd, IDC_BROWSE, L"");
        g_hOutLabel = MakeLabel(hwnd, L"");
        g_hEditOut = MakeEdit(hwnd, IDC_OUT);
        g_hSaveAs = MakeButton(hwnd, IDC_SAVEAS, L"");
        g_hReportLabel = MakeLabel(hwnd, L"");
        g_hEditReport = MakeEdit(hwnd, IDC_REPORT);
        g_hReportAs = MakeButton(hwnd, IDC_REPORTAS, L"");
        g_hBuild = MakeButton(hwnd, IDC_BUILD, L"");
        g_hOpenOut = MakeButton(hwnd, IDC_OPENOUT, L"");
        g_hOpenWhenDone = MakeCheck(hwnd, IDC_OPEN_DONE, L"");
        SendMessageW(g_hOpenWhenDone, BM_SETCHECK, BST_CHECKED, 0);

        g_hProgress = CreateWindowExW(0, L"TBLtoSQLProgress", L"", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROGRESS)), g_hInst, nullptr);
        g_hStatus = MakeLabel(hwnd, L"");
        SendMessageW(g_hStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_hSemibold), TRUE);

        g_hLogSection = MakeLabel(hwnd, L"");
        SendMessageW(g_hLogSection, WM_SETFONT, reinterpret_cast<WPARAM>(g_hSemibold), TRUE);
        g_hCopyLog = MakeButton(hwnd, IDC_COPY_LOG, L"");
        g_hClearLog = MakeButton(hwnd, IDC_CLEAR_LOG, L"");
        g_hLog = MakeEdit(hwnd, IDC_LOG, true);

        LoadSettings();
        SendMessageW(g_hLang, CB_SETCURSEL, g_english ? 1 : 0, 0);
        if (GetText(g_hEditFolder).empty()) {
            wchar_t profile[MAX_PATH] = {};
            DWORD n = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
            if (n > 0 && n < MAX_PATH) SetText(g_hEditFolder, (fs::path(profile) / L"Desktop" / L"Item").wstring());
        }
        SetDefaultOutputPaths(GetText(g_hEditFolder));
        ApplyLanguage();
        SetProgress(0);
        LayoutControls(hwnd);
        return 0;
    }

    case WM_SIZE:
        LayoutControls(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 900;
        mmi->ptMinTrackSize.y = 720;
        return 0;
    }

    case WM_PAINT:
        PaintBackground(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        HWND child = reinterpret_cast<HWND>(lParam);
        COLORREF color = C_TEXT;
        if (child == g_hSubtitle || child == g_hLangLabel || child == g_hFolderLabel || child == g_hOutLabel || child == g_hReportLabel)
            color = C_MUTED;
        else if (child == g_hStatus) {
            if (!g_isRunning && g_progress == 100) color = C_SUCCESS;
            else if (!g_isRunning && g_progress == 0 && GetText(g_hStatus) == T().error) color = C_ERROR;
            else color = g_isRunning ? C_WARN : C_MUTED;
        }
        SetTextColor(dc, color);
        if (child == g_hSourceSection || child == g_hFolderLabel || child == g_hOutLabel || child == g_hReportLabel || child == g_hLogSection)
            return reinterpret_cast<LRESULT>(g_cardBrush);
        return reinterpret_cast<LRESULT>(g_bgBrush);
    }

    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, C_EDIT);
        SetTextColor(dc, C_TEXT);
        return reinterpret_cast<LRESULT>(g_editBrush);
    }

    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, C_TEXT);
        return reinterpret_cast<LRESULT>(g_cardBrush);
    }

    case WM_MEASUREITEM: {
        auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (mi && mi->CtlID == IDC_LANG) {
            mi->itemHeight = 30;
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (!dis) break;
        if (dis->CtlType == ODT_BUTTON) {
            DrawButton(dis);
            return TRUE;
        }
        if (dis->CtlType == ODT_COMBOBOX && dis->CtlID == IDC_LANG) {
            DrawComboItem(dis);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (id == IDC_LANG && code == CBN_SELCHANGE) {
            g_english = SendMessageW(g_hLang, CB_GETCURSEL, 0, 0) == 1;
            ApplyLanguage();
            LayoutControls(hwnd);
            SaveSettings();
            return 0;
        }
        if (id == IDC_BROWSE) {
            std::wstring folder = PickFolder(hwnd);
            if (!folder.empty()) {
                SetText(g_hEditFolder, folder);
                SetDefaultOutputPaths(folder);
                SaveSettings();
            }
            return 0;
        }
        if (id == IDC_SAVEAS) {
            std::wstring current = GetText(g_hEditOut);
            std::wstring name = current.empty() ? L"USKO_ITEM.sql" : fs::path(current).filename().wstring();
            std::wstring picked = SaveFileDialog(hwnd, T().sqlSaveTitle, name, T().sqlFilter, L"*.sql", L"sql");
            if (!picked.empty()) SetText(g_hEditOut, picked);
            return 0;
        }
        if (id == IDC_REPORTAS) {
            std::wstring current = GetText(g_hEditReport);
            std::wstring name = current.empty() ? L"USKO_ITEM.report.txt" : fs::path(current).filename().wstring();
            std::wstring picked = SaveFileDialog(hwnd, T().reportSaveTitle, name, T().textFilter, L"*.txt", L"txt");
            if (!picked.empty()) SetText(g_hEditReport, picked);
            return 0;
        }
        if (id == IDC_BUILD) {
            StartBuild(hwnd);
            return 0;
        }
        if (id == IDC_OPENOUT) {
            OpenOutputFolder();
            return 0;
        }
        if (id == IDC_COPY_LOG) {
            CopyLogToClipboard(hwnd);
            return 0;
        }
        if (id == IDC_CLEAR_LOG) {
            SetText(g_hLog, L"");
            return 0;
        }
        break;
    }

    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        wchar_t path[32768] = {};
        if (DragQueryFileW(drop, 0, path, 32768)) {
            fs::path p(path);
            std::error_code ec;
            if (fs::is_regular_file(p, ec)) p = p.parent_path();
            if (fs::is_directory(p, ec)) {
                SetText(g_hEditFolder, p.wstring());
                SetDefaultOutputPaths(p.wstring());
                SaveSettings();
            }
        }
        DragFinish(drop);
        return 0;
    }

    case WM_APP_PROGRESS:
        SetProgress(static_cast<int>(wParam));
        return 0;

    case WM_APP_DONE: {
        std::unique_ptr<BuildResult> result(reinterpret_cast<BuildResult*>(lParam));
        if (!result) return 0;
        AppendLog(result->log);
        g_isRunning = false;
        EnableBuildControls(true);
        if (result->code == 0) {
            SetProgress(100);
            SetText(g_hStatus, T().success);
            AppendLog(g_english ? L"\r\nStatus: Success.\r\n" : L"\r\nDurum: Başarılı.\r\n");
            if (SendMessageW(g_hOpenWhenDone, BM_GETCHECK, 0, 0) == BST_CHECKED) OpenOutputFolder();
            MessageBoxW(hwnd, T().completedText, T().completedTitle, MB_OK | MB_ICONINFORMATION);
        } else {
            g_progress = 0;
            InvalidateRect(g_hProgress, nullptr, FALSE);
            SetText(g_hStatus, T().error);
            AppendLog(g_english ? L"\r\nStatus: Error.\r\n" : L"\r\nDurum: Hata.\r\n");
            MessageBoxW(hwnd, T().failedText, T().failedTitle, MB_OK | MB_ICONERROR);
        }
        SaveSettings();
        return 0;
    }

    case WM_DESTROY:
        SaveSettings();
        DragAcceptFiles(hwnd, FALSE);
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hSmallFont) DeleteObject(g_hSmallFont);
        if (g_hTitleFont) DeleteObject(g_hTitleFont);
        if (g_hSemibold) DeleteObject(g_hSemibold);
        if (g_hMonoFont && g_hMonoFont != g_hSmallFont) DeleteObject(g_hMonoFont);
        if (g_bgBrush) DeleteObject(g_bgBrush);
        if (g_editBrush) DeleteObject(g_editBrush);
        if (g_cardBrush) DeleteObject(g_cardBrush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace ui

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    using namespace ui;
    g_hInst = hInstance;
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    g_hIcon = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));

    WNDCLASSEXW progressClass{};
    progressClass.cbSize = sizeof(progressClass);
    progressClass.lpfnWndProc = ProgressWndProc;
    progressClass.hInstance = hInstance;
    progressClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    progressClass.hbrBackground = nullptr;
    progressClass.lpszClassName = L"TBLtoSQLProgress";
    RegisterClassExW(&progressClass);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = g_hIcon ? g_hIcon : LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"TBLtoSQLStudioWindow";
    wc.hIconSm = g_hIcon ? g_hIcon : LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    g_hMain = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"TBL to SQL Studio",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1020, 760,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_hMain) {
        CoUninitialize();
        return 1;
    }

    ShowWindow(g_hMain, nCmdShow);
    UpdateWindow(g_hMain);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hIcon) DestroyIcon(g_hIcon);
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}

#else
int main() {
    std::cerr << "This GUI project targets Windows only.\n";
    return 1;
}
#endif
