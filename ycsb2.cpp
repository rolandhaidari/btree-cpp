#include <algorithm>
#include <csignal>
#include <fstream>
#include <random>
#include <string>
#include "btree/BtreeCppPerfEvent.hpp"
#include "btree/btree2020.hpp"

using namespace std;

extern "C" {
struct ZipfGenerator;
ZipfGenerator* zipf_init_generator(uint32_t, double);
void zipf_generate(ZipfGenerator*, uint32_t*, uint32_t);
}

constexpr unsigned ZIPF_GEN_SIZE = 1 << 25;
constexpr double ZIPF_PARAMETER = 1.5;

uint64_t envu64(const char* env)
{
   if (getenv(env))
      return atof(getenv(env));
   std::cerr << "missing env " << env << std::endl;
   abort();
}

void runTest(BTreeCppPerfEvent e, vector<string>& data, unsigned payloadSize, unsigned opCount)
{
   unsigned keyCount = data.size();

   unsigned* zipf_indices = new unsigned[ZIPF_GEN_SIZE];
   if (keyCount > 0) {
      ZipfGenerator* generator = zipf_init_generator(keyCount, ZIPF_PARAMETER);
      zipf_generate(generator, zipf_indices, ZIPF_GEN_SIZE);
   } else if (opCount != 0) {
      std::cerr << "no keys" << std::endl;
      throw;
   }

   uint8_t payload[payloadSize];
   memset(payload, 42, payloadSize);

   DataStructureWrapper t;
   {
      // insert
      e.setParam("op", "insert");
      BTreeCppPerfEventBlock b(e, keyCount);
      for (uint64_t i = 0; i < keyCount; i++) {
         uint8_t* key = (uint8_t*)data[i].data();
         unsigned int length = data[i].size();
         t.insert(key, length, payload, payloadSize);
      }
   }

   {
      e.setParam("op", "lookup");
      BTreeCppPerfEventBlock b(e, opCount);
      for (uint64_t i = 0; i < opCount; i++) {
         unsigned keyIndex = zipf_indices[i % ZIPF_GEN_SIZE] - 1;
         assert(keyIndex < data.size());
         unsigned payloadSizeOut;
         uint8_t* key = (uint8_t*)data[keyIndex].data();
         unsigned long length = data[keyIndex].size();
         uint8_t* payload = t.lookup(key, length, payloadSizeOut);
         if (!payload || (payloadSize != payloadSizeOut) || *payload != 42)
            throw;
      }
   }

   data.clear();
}

int main()
{
   vector<string> data;

   unsigned keyCount = envu64("KEY_COUNT");
   if (!getenv("DATA")) {
      std::cerr << "no keyset" << std::endl;
      abort();
   }
   std::string keySet = getenv("DATA");

   if (keySet == "int") {
      vector<uint32_t> v;
      for (uint32_t i = 0; i < keyCount; i++)
         v.push_back(i);
      string s;
      s.resize(4);
      for (auto x : v) {
         *(uint32_t*)(s.data()) = __builtin_bswap32(x);
         data.push_back(s);
      }
   } else if (keySet == "long1") {
      for (unsigned i = 0; i < keyCount; i++) {
         string s;
         for (unsigned j = 0; j < i; j++)
            s.push_back('A');
         data.push_back(s);
      }
   } else if (keySet == "long2") {
      for (unsigned i = 0; i < keyCount; i++) {
         string s;
         for (unsigned j = 0; j < i; j++)
            s.push_back('A' + random() % 60);
         data.push_back(s);
      }
   } else {
      ifstream in(keySet);
      keySet = "file:" + keySet;
      string line;
      while (getline(in, line))
         data.push_back(line);
   }
   unsigned payloadSize = envu64("PAYLOAD_SIZE");
   unsigned opCount = envu64("OP_COUNT");
   BTreeCppPerfEvent e = makePerfEvent(keySet, false, data.size());
   e.setParam("payload_size", payloadSize);
   e.setParam("run_id", envu64("RUN_ID"));

   if (keyCount <= data.size()) {
      random_shuffle(data.begin(), data.end());
      data.resize(keyCount);
      runTest(e, data, payloadSize, opCount);
   } else {
      std::cerr << "not enough keys in " << keySet << std::endl;
      data.resize(0);
      runTest(e, data, 0, 0);
   }
   return 0;
}
