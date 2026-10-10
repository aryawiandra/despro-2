// Graf evakuasi + Dijkstra (C++ murni, tanpa dependensi Arduino supaya bisa diuji di PC).
// Graf sama dengan Week5/kodeIntegrasi.cpp: 12 junction (j1..j12) + 3 exit (e1..e3), 20 jalur.
// Tiap jalur punya 1 sensor api. Jalur yang kena api (blocked) dibuang dari graf sebelum Dijkstra.
#pragma once
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#define NUM_NODES 15
#define NUM_EDGES 20
#define INF       999999

// j1=0 ... j12=11, e1=12, e2=13, e3=14
static const char *const NODE_NAMES[NUM_NODES] = {
  "j1", "j2", "j3", "j4", "j5", "j6", "j7", "j8", "j9", "j10", "j11", "j12",
  "e1", "e2", "e3"
};

// Ruangan 0..7 -> junction pertama
static const int ROOM_TO_JUNCTION[8] = { 0, 0, 4, 4, 7, 7, 10, 10 };

static const int EXIT_NODES[3] = { 12, 13, 14 };
#define NUM_EXITS 3

struct Edge { int u; int v; int weight; };

// Indeks di sini = nomor bit di mask = urutan LED di receiver. Nomor jalur untuk user = indeks + 1.
static const Edge EDGES[NUM_EDGES] = {
  {0, 1, 49},   // [0]  j1 - j2
  {0, 2, 23},   // [1]  j1 - j3
  {1, 2, 12},   // [2]  j2 - j3
  {1, 11, 13},  // [3]  j2 - j12
  {2, 3, 13},   // [4]  j3 - j4
  {3, 4, 23},   // [5]  j4 - j5
  {3, 5, 12},   // [6]  j4 - j6
  {4, 5, 49},   // [7]  j5 - j6
  {5, 6, 13},   // [8]  j6 - j7
  {6, 7, 23},   // [9]  j7 - j8
  {6, 8, 12},   // [10] j7 - j9
  {7, 8, 49},   // [11] j8 - j9
  {8, 9, 13},   // [12] j9 - j10
  {9, 10, 49},  // [13] j10 - j11
  {9, 11, 12},  // [14] j10 - j12
  {10, 11, 23}, // [15] j11 - j12
  {1, 12, 5},   // [16] j2 - e1
  {3, 13, 6},   // [17] j4 - e2
  {5, 13, 6},   // [18] j6 - e2
  {11, 14, 5}   // [19] j12 - e3
};

inline int findEdgeIndex(int u, int v) {
  for (int i = 0; i < NUM_EDGES; i++) {
    if ((EDGES[i].u == u && EDGES[i].v == v) || (EDGES[i].u == v && EDGES[i].v == u)) return i;
  }
  return -1;
}

// "j5" / "E2" -> indeks node, -1 jika tidak dikenal
inline int nodeIndex(const char *name) {
  for (int i = 0; i < NUM_NODES; i++) {
    const char *a = NODE_NAMES[i];
    const char *b = name;
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
    if (*a == 0 && *b == 0) return i;
  }
  return -1;
}

struct RouteResult {
  bool found;
  int exitNode;       // node exit tujuan
  int totalWeight;    // total bobot rute
  uint32_t pathMask;  // bit i = jalur i dilewati rute
  int nodes[NUM_NODES];
  int numNodes;       // urutan node dari start sampai exit
  int edgeSeq[NUM_NODES];  // jalur yang dilewati, berurutan dari start ke exit
  bool edgeRev[NUM_NODES]; // true = rute melewati jalur dari node v ke u (arah kebalikan tabel EDGES)
  int numEdges;
};

// Dijkstra dari startNode ke exit terdekat; jalur dengan bit blockedMask = 1 tidak dipakai.
inline RouteResult computeRoute(int startNode, uint32_t blockedMask) {
  RouteResult r;
  r.found = false; r.exitNode = -1; r.totalWeight = INF; r.pathMask = 0; r.numNodes = 0; r.numEdges = 0;

  int adj[NUM_NODES][NUM_NODES];
  for (int i = 0; i < NUM_NODES; i++)
    for (int j = 0; j < NUM_NODES; j++) adj[i][j] = (i == j) ? 0 : INF;
  for (int i = 0; i < NUM_EDGES; i++) {
    if ((blockedMask >> i) & 1) continue;
    adj[EDGES[i].u][EDGES[i].v] = EDGES[i].weight;
    adj[EDGES[i].v][EDGES[i].u] = EDGES[i].weight;
  }

  int dist[NUM_NODES], parent[NUM_NODES];
  bool visited[NUM_NODES];
  for (int i = 0; i < NUM_NODES; i++) { dist[i] = INF; parent[i] = -1; visited[i] = false; }
  dist[startNode] = 0;

  for (int count = 0; count < NUM_NODES; count++) {
    int u = -1, best = INF;
    for (int i = 0; i < NUM_NODES; i++)
      if (!visited[i] && dist[i] < best) { best = dist[i]; u = i; }
    if (u == -1) break;
    visited[u] = true;
    for (int v = 0; v < NUM_NODES; v++)
      if (!visited[v] && adj[u][v] != INF && dist[u] + adj[u][v] < dist[v]) {
        dist[v] = dist[u] + adj[u][v];
        parent[v] = u;
      }
  }

  for (int i = 0; i < NUM_EXITS; i++) {
    int e = EXIT_NODES[i];
    if (dist[e] < r.totalWeight) { r.totalWeight = dist[e]; r.exitNode = e; }
  }
  if (r.exitNode == -1) { r.totalWeight = INF; return r; }

  r.found = true;
  int rev[NUM_NODES], n = 0;
  for (int cur = r.exitNode; cur != -1; cur = parent[cur]) {
    rev[n++] = cur;
    if (parent[cur] != -1) {
      int idx = findEdgeIndex(parent[cur], cur);
      if (idx >= 0) r.pathMask |= (1UL << idx);
    }
  }
  for (int i = 0; i < n; i++) r.nodes[i] = rev[n - 1 - i];
  r.numNodes = n;
  for (int i = 0; i + 1 < n; i++) {
    int a = r.nodes[i], b = r.nodes[i + 1];
    int idx = findEdgeIndex(a, b);
    r.edgeSeq[r.numEdges] = idx;
    r.edgeRev[r.numEdges] = (EDGES[idx].u != a);
    r.numEdges++;
  }
  return r;
}

// ---------------------------------------------------------------------------------------------
// Peta jalur aman untuk LED strip: jalur terblokir -> merah, jalur aman yang menuju exit -> hijau.
// ---------------------------------------------------------------------------------------------
struct SafeMap {
  uint32_t greenMask;       // bit i = jalur i dilewati rute aman ke exit
  uint32_t revMask;         // bit i = arah menuju exit melewati jalur i dari node v ke u (kebalikan tabel EDGES)
  uint8_t  depth[NUM_EDGES];// jumlah jalur dari ujung awal jalur ini sampai exit (1 = jalur menempel ke exit)
  int      numGreen;
  uint16_t isolatedMask;    // bit n = junction n (tempat ruangan) tidak punya rute aman ke exit mana pun
};

// Dijkstra multi-sumber dari SEMUA exit sekaligus (graf tak berarah, jalur terblokir dibuang) memberi
// jarak dan langkah berikutnya menuju exit terdekat untuk tiap node. Lalu dari SETIAP RUANGAN (0..7, lewat
// junction ROOM_TO_JUNCTION) ditelusuri rute tercepatnya sampai exit; semua jalur yang dilewati menjadi hijau.
// Rute-rute itu membentuk pohon menuju exit, sehingga arah tiap jalur tidak pernah bertabrakan.
inline SafeMap computeSafeForest(uint32_t blockedMask) {
  SafeMap m;
  m.greenMask = 0; m.revMask = 0; m.numGreen = 0; m.isolatedMask = 0;
  memset(m.depth, 0, sizeof(m.depth));

  int dist[NUM_NODES], parent[NUM_NODES], hops[NUM_NODES];
  bool visited[NUM_NODES];
  for (int i = 0; i < NUM_NODES; i++) { dist[i] = INF; parent[i] = -1; hops[i] = 0; visited[i] = false; }
  for (int i = 0; i < NUM_EXITS; i++) dist[EXIT_NODES[i]] = 0;

  for (int count = 0; count < NUM_NODES; count++) {
    int u = -1, best = INF;
    for (int i = 0; i < NUM_NODES; i++)
      if (!visited[i] && dist[i] < best) { best = dist[i]; u = i; }
    if (u == -1) break;
    visited[u] = true;
    for (int e = 0; e < NUM_EDGES; e++) {
      if ((blockedMask >> e) & 1) continue;
      int v = -1;
      if (EDGES[e].u == u) v = EDGES[e].v; else if (EDGES[e].v == u) v = EDGES[e].u; else continue;
      if (!visited[v] && dist[u] + EDGES[e].weight < dist[v]) {
        dist[v] = dist[u] + EDGES[e].weight;
        parent[v] = u;
        hops[v] = hops[u] + 1;
      }
    }
  }

  for (int room = 0; room < 8; room++) {
    int start = ROOM_TO_JUNCTION[room];
    if (dist[start] >= INF) { m.isolatedMask |= (1U << start); continue; }   // ruangan ini terputus dari semua exit
    for (int n = start; parent[n] >= 0; n = parent[n]) {                      // telusuri sampai exit
      int idx = findEdgeIndex(n, parent[n]);
      if ((m.greenMask >> idx) & 1) break;                                    // sisa rute sudah ditandai ruangan lain
      m.greenMask |= (1UL << idx);
      if (EDGES[idx].v == n) m.revMask |= (1UL << idx);                       // berangkat dari v -> u
      m.depth[idx] = (uint8_t)hops[n];
      m.numGreen++;
    }
  }
  return m;
}

// Rute satu ruangan (RouteResult dari computeRoute) dalam format SafeMap yang sama.
inline SafeMap routeToSafeMap(const RouteResult &r) {
  SafeMap m;
  m.greenMask = 0; m.revMask = 0; m.numGreen = 0; m.isolatedMask = 0;
  memset(m.depth, 0, sizeof(m.depth));
  if (!r.found) return m;
  for (int k = 0; k < r.numEdges; k++) {
    int idx = r.edgeSeq[k];
    m.greenMask |= (1UL << idx);
    if (r.edgeRev[k]) m.revMask |= (1UL << idx);
    m.depth[idx] = (uint8_t)(r.numEdges - k);
    m.numGreen++;
  }
  return m;
}
