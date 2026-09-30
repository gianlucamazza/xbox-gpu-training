#include "model.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif
namespace e0 {
std::string sha256_file(const std::filesystem::path& path) {
  std::ifstream input(path,std::ios::binary);
  if(!input) throw std::runtime_error("cannot hash: "+path.u8string());
  std::vector<char> bytes(1<<20); unsigned char digest[32]{};
#ifdef _WIN32
  BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw std::runtime_error("SHA256 provider failed");
  if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0) { BCryptCloseAlgorithmProvider(algorithm,0);throw std::runtime_error("SHA256 hash creation failed"); }
  try {
    while(input.read(bytes.data(),bytes.size())||input.gcount())
      if(BCryptHashData(hash,reinterpret_cast<PUCHAR>(bytes.data()),ULONG(input.gcount()),0)<0) throw std::runtime_error("SHA256 update failed");
    if(BCryptFinishHash(hash,digest,32,0)<0) throw std::runtime_error("SHA256 finalize failed");
  } catch(...) { BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm,0); throw; }
  BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm,0);
#else
  auto* context=EVP_MD_CTX_new();
  if(!context) throw std::runtime_error("SHA256 context allocation failed");
  try {
    if(EVP_DigestInit_ex(context,EVP_sha256(),nullptr)!=1) throw std::runtime_error("SHA256 init failed");
    while(input.read(bytes.data(),bytes.size())||input.gcount())
      if(EVP_DigestUpdate(context,bytes.data(),size_t(input.gcount()))!=1) throw std::runtime_error("SHA256 update failed");
    unsigned int length=0;
    if(EVP_DigestFinal_ex(context,digest,&length)!=1||length!=32) throw std::runtime_error("SHA256 finalize failed");
  } catch(...) { EVP_MD_CTX_free(context); throw; }
  EVP_MD_CTX_free(context);
#endif
  if(input.bad()) throw std::runtime_error("SHA256 read failed");
  std::ostringstream out; out<<std::hex<<std::setfill('0');
  for(auto v:digest) out<<std::setw(2)<<unsigned(v);
  return out.str();
}
Json read_json(const std::filesystem::path& path) {
  std::ifstream input(path); if(!input) throw std::runtime_error("missing JSON: "+path.u8string());
  return Json::parse(input);
}
void atomic_json(const std::filesystem::path& path,const Json& value) {
  const auto temporary=path.u8string()+".tmp";
  { std::ofstream file(temporary,std::ios::binary|std::ios::trunc); if(!file) throw std::runtime_error("JSON write failed");file<<value.dump();file.flush();if(!file) throw std::runtime_error("JSON flush failed"); }
#ifdef _WIN32
  if(!MoveFileExW(std::filesystem::path(temporary).c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("JSON atomic replacement failed");
#else
  std::filesystem::rename(temporary,path);
#endif
}
namespace {
std::filesystem::path asset(const std::filesystem::path& root,const Json& descriptor) {
  std::filesystem::path relative=descriptor.at("path").get<std::string>();
  if(relative.is_absolute()||relative.empty()) throw std::runtime_error("asset path must be relative");
  for(const auto& part:relative) if(part=="..") throw std::runtime_error("asset escapes job directory");
  auto path=std::filesystem::weakly_canonical(root/relative);
  auto prefix=std::filesystem::weakly_canonical(root);
  auto rel=path.lexically_relative(prefix);
  for(const auto& part:rel) if(part=="..") throw std::runtime_error("asset symlink escapes job directory");
  if(!std::filesystem::exists(path)&&descriptor.contains("chunks")) {
    const auto temporary=path.u8string()+".assembling";
    { std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
      if(!output) throw std::runtime_error("asset assembly creation failed");
      std::vector<char> buffer(1<<20);uint64_t total=0;
      for(const auto& chunk:descriptor.at("chunks")) {
        if(chunk.contains("chunks")) throw std::runtime_error("nested asset chunks rejected");
        auto source=asset(root,chunk);std::ifstream input(source,std::ios::binary);
        while(input.read(buffer.data(),buffer.size())||input.gcount()) {
          output.write(buffer.data(),input.gcount());total+=uint64_t(input.gcount());
        }
        if(input.bad()||!output) throw std::runtime_error("asset assembly I/O failed");
      }
      output.flush();if(!output||total!=descriptor.at("bytes").get<uint64_t>()) throw std::runtime_error("asset assembly size mismatch");
    }
    if(sha256_file(temporary)!=descriptor.at("sha256")) throw std::runtime_error("assembled asset hash mismatch");
    std::filesystem::rename(temporary,path);
  }
  if(std::filesystem::file_size(path)!=descriptor.at("bytes").get<uint64_t>()||sha256_file(path)!=descriptor.at("sha256")) throw std::runtime_error("asset integrity failed: "+relative.u8string());
  return path;
}
void read_batch(std::ifstream& corpus,std::ifstream& indices,uint64_t step,uint32_t batch,uint32_t ctx,uint64_t corpus_size,Values& x,Values& y) {
  x.resize(uint64_t(batch)*ctx); y.resize(x.size());
  indices.clear();indices.seekg(std::streamoff(step*batch*8));
  std::vector<uint8_t> window(ctx+1);
  for(uint32_t b=0;b<batch;++b) {
    uint8_t encoded[8];indices.read(reinterpret_cast<char*>(encoded),8);if(!indices) throw std::runtime_error("index plan exhausted");
    uint64_t offset=0;for(unsigned i=0;i<8;++i) offset|=uint64_t(encoded[i])<<(8*i);
    if(offset>corpus_size||corpus_size-offset<ctx+1) throw std::runtime_error("index out of corpus bounds");
    corpus.clear();corpus.seekg(std::streamoff(offset));corpus.read(reinterpret_cast<char*>(window.data()),window.size());
    if(!corpus) throw std::runtime_error("corpus read failed");
    for(uint32_t t=0;t<ctx;++t) { x[b*ctx+t]=float(window[t]);y[b*ctx+t]=float(window[t+1]); }
  }
}
float learning_rate(uint64_t step,uint64_t warmup,float peak,uint64_t cd_start=UINT64_MAX,uint64_t cd_len=0) {
  if(step<warmup) return peak*float(step+1)/float(warmup);
  if(step<cd_start) return peak;
  return peak*std::max(0.0f,1-float(step-cd_start+1)/float(cd_len));
}
}
Json run_job(const std::filesystem::path& job_file,Kernel& kernel,uint64_t stop_after) {
  const auto root=std::filesystem::absolute(job_file).parent_path();
  Json job=read_json(job_file);
  const std::string id=job.at("job_id");
  if(id.empty()||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos) throw std::runtime_error("invalid job id");
  const auto result=root/"results"/id;
  std::filesystem::create_directories(result.parent_path());
  if(job.at("schema")!="floppylm.e0.job.v1") throw std::runtime_error("unsupported E0 job schema");
  const auto initial_path=asset(root,job.at("initialization"));
  Json initial=read_json(initial_path);
  if(initial.at("config")!=job.at("config")) throw std::runtime_error("initialization config mismatch");
  job["tensors"]=initial.at("tensors");job["initialization_sha256"]=job.at("initialization").at("sha256");
  const auto corpus_path=asset(root,job.at("data"));
  const auto indices_path=asset(root,job.at("indices"));
  Model model(job);
  const auto& spec=job.at("spec");
  const auto batch=spec.at("batch").get<uint32_t>();
  const auto tokens=spec.at("tokens").get<uint64_t>();
  if(!batch||!tokens||spec.at("branches")!=3||spec.at("warmup_frac")!=0.02||spec.at("cooldown_frac")!=0.1) throw std::runtime_error("E0 requires the declared three-branch WSD protocol");
  float lr=spec.at("lr"),wd=spec.at("wd");
  if(!std::isfinite(lr)||lr<=0||!std::isfinite(wd)||wd<0) throw std::runtime_error("invalid optimizer recipe");
  uint64_t T=std::max<uint64_t>(1,tokens/(uint64_t(batch)*model.config.ctx));
  uint64_t warmup=std::max<uint64_t>(1,uint64_t(0.02*double(T))), step=0;
  if(job.at("indices").at("bytes").get<uint64_t>()!=4*T*batch*8) throw std::runtime_error("index plan length mismatch");
  const bool resume=job.contains("resume");
  if(!resume&&!std::filesystem::create_directory(result)) throw std::runtime_error("job result already exists; explicit resume required");
  Json report={{"schema","floppylm.e0.result.v1"},{"job_id",job.at("job_id")},{"job_sha256",sha256_file(job_file)},
    {"hardware_gpu",kernel.hardware()},{"adapter",kernel.adapter()},{"branches",Json::array()},{"state","running"}};
  auto begin=std::chrono::steady_clock::now();
  std::ifstream corpus(corpus_path,std::ios::binary),indices(indices_path,std::ios::binary);
  auto status=[&](const std::string& state) {
    report["state"]=state; report["trunk_step"]=step;report["dispatches"]=kernel.dispatches;
    report["wall_seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    atomic_json(result/"status.json",report);
  };
  try {
    if(resume) {
      auto ckpt=read_json(asset(root,job.at("resume"))); model.restore(ckpt,job,step);
      const auto previous=read_json(result/"status.json");
      if(previous.at("job_id")!=job.at("job_id")) throw std::runtime_error("resume status job mismatch");
      report["branches"]=previous.at("branches");
      for(const auto& b:report["branches"]) asset(root,b.at("artifact"));
    }
    status("running");
    Values x,y; uint64_t executed=0;
    for(unsigned branch=0;branch<3;++branch) {
      const uint64_t end=T*(uint64_t(1)<<branch),cd=std::max<uint64_t>(1,uint64_t(0.1*double(end))),start=end-cd;
      if(step>start) continue;
      while(step<start) {
        if(std::filesystem::exists(root/"cancel")) { atomic_json(result/"checkpoint.json",model.checkpoint(step,job));status("interrupted");return report; }
        read_batch(corpus,indices,step,batch,model.config.ctx,job.at("data").at("bytes"),x,y);
        auto metric=model.step(kernel,x,y,batch,learning_rate(step,warmup,lr),wd,step+1);
        ++step;++executed;report["last_loss"]=metric.at("loss");
        if(step%64==0) { atomic_json(result/"checkpoint.json",model.checkpoint(step,job));status("running"); }
        if(stop_after&&executed>=stop_after) { atomic_json(result/"checkpoint.json",model.checkpoint(step,job));status("interrupted");return report; }
      }
      atomic_json(result/"checkpoint.json",model.checkpoint(step,job));
      const auto cooldown_begin=std::chrono::steady_clock::now();
      Model copy=model;
      for(uint64_t s=start;s<end;++s) {
        if(std::filesystem::exists(root/"cancel")) { status("interrupted");return report; }
        read_batch(corpus,indices,s,batch,model.config.ctx,job.at("data").at("bytes"),x,y);
        copy.step(kernel,x,y,batch,learning_rate(s,warmup,lr,start,cd),wd,s+1);
      }
      const std::string name="branch-"+std::to_string(end)+".json";
      atomic_json(result/name,{{"schema","floppylm.e0.weights.v1"},{"config",job.at("config")},{"tensors",copy.tensors()}});
      Json b={{"end_step",end},{"tokens_seen",end*batch*model.config.ctx},{"cooldown_start",start},{"cooldown_steps",cd},{"cooldown_seconds",std::chrono::duration<double>(std::chrono::steady_clock::now()-cooldown_begin).count()},
        {"artifact",{{"path","results/"+id+"/"+name},{"bytes",std::filesystem::file_size(result/name)},{"sha256",sha256_file(result/name)}}}};
      auto& bs=report["branches"];
      bool found=false;for(auto& old:bs) if(old.at("end_step")==end) { if(old.at("artifact")!=b.at("artifact")) throw std::runtime_error("resumed branch differs from previous artifact");found=true; }
      if(!found) bs.push_back(b);
      status("running");
    }
    if(report["branches"].size()!=3) throw std::runtime_error("incomplete branch collection");
    status("completed");return report;
  } catch(const std::exception& error) {
    report["error"]=error.what();status("failed");throw;
  }
}
}
