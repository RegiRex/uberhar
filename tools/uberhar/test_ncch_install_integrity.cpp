// CodexAstraLocal: Exercise the real NCCH streaming transformation and verifier with synthetic bytes.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/sha.h>
#include "common/alignment.h"
#include "common/bit_field.h"
#include "common/common_types.h"
#include "common/swap.h"
#include "core/file_sys/ncch_install_verifier.h"
#define LOG_DEBUG(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
#define LOG_INFO(...) ((void)0)
// @STRUCTURES@
namespace HW::AES {
enum class KeySlotID { NCCHSecure1, NCCHSecure2, NCCHSecure3, NCCHSecure4 };
std::array<std::array<u8,16>,4> ys;
void InitKeys() {}
void SetKeyY(KeySlotID slot, const std::array<u8,16>& y) { ys[static_cast<unsigned>(slot)]=y; }
bool IsNormalKeyAvailable(KeySlotID) { return true; }
std::array<u8,16> GetNormalKey(KeySlotID slot) {
 auto key=ys[static_cast<unsigned>(slot)];
 for(auto& b:key) b^=static_cast<u8>(0x31+23*static_cast<unsigned>(slot));
 return key;
}
}
namespace FileSys { std::optional<std::array<u8,16>> GetSeed(u64) { return {}; } }
namespace FileUtil {
constexpr u32 MakeMagic(char a,char b,char c,char d) { return u32(a)|(u32(b)<<8)|(u32(c)<<16)|(u32(d)<<24); }
// CodexAstraLocal: Recording raw output controls short writes/close failures; crypto remains real.
class IOFileBase {
public:
 std::vector<u8> bytes;
 bool bad_write=false,bad_close=false;
 template<class T> std::size_t WriteBytes(const T* p,std::size_t n) {
  if(bad_write) return 0;
  const auto* b=reinterpret_cast<const u8*>(p);bytes.insert(bytes.end(),b,b+n);return n;
 }
 bool Close() { return !bad_close; }
};
}
namespace Service::AM {
// @CLASS@
NCCHCryptoFile::NCCHCryptoFile(const std::string&,bool,u64 expected_size_) : expected_size(expected_size_) {
 file=std::make_unique<FileUtil::IOFileBase>();decryption_authorized=true;
}
// @METHODS@
}
static unsigned checks;
void Check(bool condition,const char* text) { ++checks;if(!condition){std::fprintf(stderr,"FAIL %s\n",text);std::exit(1);} }
using Key=std::array<u8,16>;
Key KeyFor(const NCCH_Header& h,unsigned slot) { Key k{};for(unsigned i=0;i<16;++i)k[i]=h.signature[i]^(0x31+23*slot);return k; }
Key Counter(const NCCH_Header& h,unsigned section) { Key c{};std::reverse_copy(h.partition_id,h.partition_id+8,c.begin());c[8]=section;return c; }
void Hash(u8* dst,std::span<const u8> data) { CryptoPP::SHA256{}.CalculateDigest(dst,data.data(),data.size()); }
void Crypt(std::vector<u8>& data,std::size_t start,std::size_t length,Key key,Key ctr,std::size_t seek=0) {
 CryptoPP::CTR_Mode<CryptoPP::AES>::Encryption cipher(key.data(),key.size(),ctr.data());cipher.Seek(seek);
 cipher.ProcessData(data.data()+start,data.data()+start,length);
}
struct Sample { std::vector<u8> encoded,expected; };
// CodexAstraLocal: Independently assemble header hashes and ciphertext for primary/secondary regions.
Sample Make(bool exefs=true,bool romfs=true,bool exheader=true) {
 NCCH_Header h{};h.magic=FileUtil::MakeMagic('N','C','C','H');h.version=2;h.content_size=20;
 h.program_id=0x0004000001234500ULL;h.secondary_key_slot=1;h.no_crypto.Assign(0);
 for(unsigned i=0;i<16;++i)h.signature[i]=static_cast<u8>(i*13+7);
 for(unsigned i=0;i<8;++i)h.partition_id[i]=static_cast<u8>(0x70+i);
 Sample s;s.expected.resize(20*512);
 if(exheader){h.extended_header_size=0x400;for(unsigned i=0;i<0x800;++i)s.expected[512+i]=static_cast<u8>(i*17+3);Hash(h.extended_header_hash,{s.expected.data()+512,0x400});}
 if(exefs){
  h.exefs_offset=5;h.exefs_size=5;h.exefs_hash_region_size=1;ExeFs_Header e{};
  std::memcpy(e.section[0].name,"icon",4);e.section[0].size=128;
  std::memcpy(e.section[1].name,".code",5);e.section[1].offset=512;e.section[1].size=512;
  for(unsigned i=0;i<512;++i){s.expected[3072+i]=static_cast<u8>(i*3+1);s.expected[3584+i]=static_cast<u8>(i*7+2);}
  Hash(e.hashes[7],{s.expected.data()+3072,128});Hash(e.hashes[6],{s.expected.data()+3584,512});
  std::memcpy(s.expected.data()+2560,&e,sizeof(e));Hash(h.exefs_super_block_hash,{s.expected.data()+2560,512});
 }
 if(romfs){h.romfs_offset=10;h.romfs_size=10;h.romfs_hash_region_size=2;for(unsigned i=5120;i<s.expected.size();++i)s.expected[i]=static_cast<u8>(i*11+5);Hash(h.romfs_super_block_hash,{s.expected.data()+5120,1024});}
 std::memcpy(s.expected.data(),&h,sizeof(h));s.encoded=s.expected;
 if(exheader)Crypt(s.encoded,512,2048,KeyFor(h,0),Counter(h,1));
 if(exefs){Crypt(s.encoded,2560,512,KeyFor(h,0),Counter(h,2));Crypt(s.encoded,3072,512,KeyFor(h,0),Counter(h,2),512);Crypt(s.encoded,3584,512,KeyFor(h,1),Counter(h,2),1024);}
 if(romfs)Crypt(s.encoded,5120,5120,KeyFor(h,1),Counter(h,3));
 h.no_crypto.Assign(1);std::memcpy(s.expected.data(),&h,sizeof(h));return s;
}
void Feed(Service::AM::NCCHCryptoFile& writer,const std::vector<u8>& bytes) {
 constexpr std::array<std::size_t,7> chunks{1,127,389,16,1021,73,4096};std::size_t at=0,n=0;
 while(at<bytes.size()&&!writer.IsError()){auto size=std::min(chunks[n++%chunks.size()],bytes.size()-at);writer.Write(bytes.data()+at,size);at+=size;}
}
int main(){
 for(auto options:std::array<std::array<bool,3>,4>{{{true,true,true},{true,false,true},{false,true,false},{false,true,true}}}){
  auto s=Make(options[0],options[1],options[2]);Service::AM::NCCHCryptoFile writer("",true,s.encoded.size());Feed(writer,s.encoded);
  Check(writer.Finalize(),"valid encrypted NCCH finalizes");Check(writer.file->bytes==s.expected,"streamed transformed output matches");
 }
 for(unsigned position: {512u,2560u,3072u,3584u,5120u}){
  auto s=Make();s.encoded[position]^=0x23;Service::AM::NCCHCryptoFile writer("",true,s.encoded.size());Feed(writer,s.encoded);
  Check(!writer.Finalize(),"bad inner crypto rejected");
 }
 {auto s=Make(false,true,false);s.encoded[5120]^=1;Service::AM::NCCHCryptoFile w("",true,s.encoded.size());Feed(w,s.encoded);Check(!w.Finalize(),"no-exheader secondary key rejected");}
 {auto s=Make(true,false,false);s.encoded[3584]^=1;Service::AM::NCCHCryptoFile w("",true,s.encoded.size());Feed(w,s.encoded);Check(!w.Finalize(),"ExeFS-only secondary key rejected");}
 for(unsigned kind=0;kind<5;++kind){auto s=Make();NCCH_Header h{};std::memcpy(&h,s.encoded.data(),sizeof(h));
  if(kind==0)h.exefs_offset=0xffffffffu;if(kind==1)h.romfs_size=0xffffffffu;if(kind==2)h.extended_header_size=0xffffffffu;if(kind==3)h.exefs_hash_region_size=0;if(kind==4)h.romfs_hash_region_size=0xffffffffu;
  std::memcpy(s.encoded.data(),&h,sizeof(h));Service::AM::NCCHCryptoFile w("",true,s.encoded.size());Feed(w,s.encoded);Check(!w.Finalize(),"malformed inner range rejected");}
 {auto s=Make();Service::AM::NCCHCryptoFile w("",true,512);Feed(w,s.encoded);Check(!w.Finalize(),"NCCH cannot exceed TMD content");}
 for(bool write_failure:{false,true}){auto s=Make();Service::AM::NCCHCryptoFile w("",true,s.encoded.size());w.file->bad_write=write_failure;w.file->bad_close=!write_failure;Feed(w,s.encoded);Check(!w.Finalize(),"output failure rejected");}
 {auto s=Make();s.encoded.resize(6000);Service::AM::NCCHCryptoFile w("",true,10240);Feed(w,s.encoded);Check(!w.Finalize(),"truncated hash region rejected");}
 std::printf("PASS inner NCCH integrity: %u assertions\n",checks);
}
