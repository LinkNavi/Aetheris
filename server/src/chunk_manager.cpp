#include "chunk_manager.h"
#include "marching_cubes.h"
#include "net_common.h"
#include "noise_gen.h"
#include <algorithm>
#include <cmath>
#include "packets.h"

static constexpr int   TREE_TEMPLATE_COUNT = 6;
static constexpr float SEA_LEVEL           = 128.5f;
static constexpr float MIN_SPAWN_Y         = SEA_LEVEL + 10.f;
static constexpr float MAX_SPAWN_Y         = SEA_LEVEL + 50.f;

static ChunkCoord worldToChunk(float wx, float wy, float wz) {
  int sz = ChunkData::SIZE;
  return {(int)std::floor(wx/sz),(int)std::floor(wy/sz),(int)std::floor(wz/sz)};
}

ChunkManager::ChunkManager(int genThreads) : _pool(genThreads) {}

ClientState *ChunkManager::findClient(ENetPeer *peer) {
  for (auto &c : _clients) if (c.peer==peer) return &c;
  return nullptr;
}
void ChunkManager::addClient(ENetPeer *peer) { _clients.push_back({peer}); }
void ChunkManager::removeClient(ENetPeer *peer) {
  _clients.erase(std::remove_if(_clients.begin(),_clients.end(),
    [peer](const ClientState &c){return c.peer==peer;}),_clients.end());
}
void ChunkManager::resetClient(ENetPeer *peer) {
  ClientState *cs=findClient(peer); if(!cs) return;
  cs->sentChunks.clear(); cs->pendingChunks.clear();
  cs->lastChunk={INT_MIN,INT_MIN,INT_MIN};
}

void ChunkManager::scheduleChunk(ClientState &cs, ChunkCoord coord) {
  if (cs.sentChunks.count(coord)||cs.pendingChunks.count(coord)) return;
  {
    std::lock_guard lk(_cacheMu);
    auto it=_cache.find(coord);
    if(it!=_cache.end()){
      std::lock_guard rlk(_readyMu);
      _ready.push({cs.peer,coord,it->second});
      cs.sentChunks.insert(coord);
      return;
    }
  }
  cs.pendingChunks.insert(coord);
  ENetPeer *peer=cs.peer;
  _pool.submit([this,peer,coord](){generateAndEnqueue(peer,coord);});
}

void ChunkManager::generateAndEnqueue(ENetPeer *peer, ChunkCoord coord) {
  ChunkData data=generateChunk(coord);
  ChunkMesh mesh=marchChunk(data);
  auto bytes=ChunkDataPacket::from(mesh).serialize();

  TreeSpawnPacket treePkt;
  int N=ChunkData::SIZE;
  for(int x=0;x<N;x+=4) for(int z=0;z<N;z+=4){
    float wx=(float)(coord.x*N+x), wz=(float)(coord.z*N+z);
    float surfY=sampleSurfaceY(wx,wz);
    int tmpl=getTreeLibrary().samplePlacement(wx,surfY,wz,surfY);
    if(tmpl<0) continue;
    TreeSpawnPacket::Entry e;
    e.wx=wx; e.wy=surfY; e.wz=wz;
    e.yaw=fmodf(wx*0.37f+wz*0.53f,6.2831853f);
    e.scale=0.85f+fmodf(wx*0.11f+wz*0.17f,0.3f);
    e.templateIdx=(uint8_t)(tmpl%TREE_TEMPLATE_COUNT);
    treePkt.trees.push_back(e);
  }

  {std::lock_guard lk(_cacheMu); _cache.emplace(coord,bytes);}
  {
    std::lock_guard lk(_readyMu);
    ReadyChunk rc; rc.peer=peer; rc.coord=coord; rc.bytes=std::move(bytes);
    rc.treeBytes=treePkt.trees.empty()?std::vector<uint8_t>{}:treePkt.serialize();
    _ready.push(std::move(rc));
  }
}

void ChunkManager::updateClient(ENetPeer *peer, float wx, float wy, float wz) {
  ClientState *cs=findClient(peer); if(!cs) return;
  ChunkCoord center=worldToChunk(wx,wy,wz);
  if(center==cs->lastChunk) return;
  cs->lastChunk=center;

  struct CoordDist{ChunkCoord coord;int distSq;};
  std::vector<CoordDist> pending;
  pending.reserve((cs->renderDistXZ*2+1)*(cs->renderDistXZ*2+1)*(cs->renderDistY*2+1));
  for(int dx=-cs->renderDistXZ;dx<=cs->renderDistXZ;dx++)
  for(int dy=-cs->renderDistY; dy<=cs->renderDistY; dy++)
  for(int dz=-cs->renderDistXZ;dz<=cs->renderDistXZ;dz++)
    pending.push_back({{center.x+dx,center.y+dy,center.z+dz},dx*dx+dy*dy*4+dz*dz});
  std::sort(pending.begin(),pending.end(),
    [](const CoordDist &a,const CoordDist &b){return a.distSq<b.distSq;});
  for(auto &cd:pending) scheduleChunk(*cs,cd.coord);
}

void ChunkManager::flushReady(ENetHost *host) {
  std::queue<ReadyChunk> batch;
  {std::lock_guard lk(_readyMu); std::swap(batch,_ready);}
  bool sent=false;
  while(!batch.empty()){
    ReadyChunk &rc=batch.front();
    ClientState *cs=findClient(rc.peer);
    if(cs){
      cs->pendingChunks.erase(rc.coord); cs->sentChunks.insert(rc.coord);
      ENetPacket *pkt=enet_packet_create(rc.bytes.data(),rc.bytes.size(),ENET_PACKET_FLAG_RELIABLE);
      enet_peer_send(rc.peer,0,pkt);
      if(!rc.treeBytes.empty()){
        ENetPacket *tp=enet_packet_create(rc.treeBytes.data(),rc.treeBytes.size(),ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(rc.peer,0,tp);
      }
      sent=true;
    }
    batch.pop();
  }
  if(sent) enet_host_flush(host);
}

float ChunkManager::findSpawnY(float wx, float wz) {
  return sampleSurfaceY(wx,wz);
}

// Fast spawn: 20x20 grid of pure-math surface samples (~microseconds).
// Picks the flattest point in the valid height range (plains band).
// Grid offsets are jittered by seed so different seeds find different spots.
glm::vec3 ChunkManager::findSpawnPos() {
  // Seed-derived offset so each world seed explores a different region
  int seed = Config::WORLD_SEED;
  float offsetX = (float)((seed * 1619 + 31337) % 512) - 256.f;
  float offsetZ = (float)((seed * 6971 + 99991) % 512) - 256.f;

  constexpr int   GRID    = 20;
  constexpr float SPACING = 64.f;
  constexpr float HALF    = GRID * SPACING * 0.5f;

  float bestX=offsetX, bestZ=offsetZ, bestSY=MIN_SPAWN_Y, bestScore=-1e9f;
  bool  found = false;

  // Pass 1: strict — must be flat AND all neighbours in range
  for(int gx=0;gx<GRID;gx++)
  for(int gz=0;gz<GRID;gz++) {
    float wx = offsetX + gx*SPACING - HALF;
    float wz = offsetZ + gz*SPACING - HALF;
    float sy = sampleSurfaceY(wx,wz);
    if(sy<MIN_SPAWN_Y || sy>MAX_SPAWN_Y) continue;

    float n=sampleSurfaceY(wx,        wz+SPACING);
    float s=sampleSurfaceY(wx,        wz-SPACING);
    float e=sampleSurfaceY(wx+SPACING,wz        );
    float w=sampleSurfaceY(wx-SPACING,wz        );
    if(n<MIN_SPAWN_Y||s<MIN_SPAWN_Y||e<MIN_SPAWN_Y||w<MIN_SPAWN_Y) continue;
    if(n>MAX_SPAWN_Y||s>MAX_SPAWN_Y||e>MAX_SPAWN_Y||w>MAX_SPAWN_Y) continue;

    float slope = std::abs(n-sy)+std::abs(s-sy)+std::abs(e-sy)+std::abs(w-sy);
    float score = -slope - std::abs(sy-(SEA_LEVEL+20.f))*0.5f;
    if(score>bestScore){ bestScore=score; bestX=wx; bestZ=wz; bestSY=sy; found=true; }
  }

  // Pass 2: relaxed — just needs to be in height range
  if(!found) {
    for(int gx=0;gx<GRID;gx++)
    for(int gz=0;gz<GRID;gz++) {
      float wx=offsetX+gx*SPACING-HALF, wz=offsetZ+gz*SPACING-HALF;
      float sy=sampleSurfaceY(wx,wz);
      if(sy<MIN_SPAWN_Y||sy>MAX_SPAWN_Y) continue;
      float score=-std::abs(sy-(SEA_LEVEL+20.f));
      if(score>bestScore){ bestScore=score; bestX=wx; bestZ=wz; bestSY=sy; found=true; }
    }
  }

  // Pass 3: expand search radius if still nothing
  if(!found) {
    for(int r=1;r<=8&&!found;r++) {
      for(int gx=-r;gx<=r;gx++)
      for(int gz=-r;gz<=r;gz++) {
        if(std::abs(gx)!=r && std::abs(gz)!=r) continue;
        float wx=offsetX+(float)gx*SPACING*2.f, wz=offsetZ+(float)gz*SPACING*2.f;
        float sy=sampleSurfaceY(wx,wz);
        if(sy<MIN_SPAWN_Y||sy>MAX_SPAWN_Y) continue;
        bestX=wx; bestZ=wz; bestSY=sy; found=true; break;
      }
    }
  }

  return {bestX, bestSY + Config::PLAYER_HEIGHT + 20.f, bestZ};
}
