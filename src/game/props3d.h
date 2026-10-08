// Procedural 3D vegetation and street furniture baked straight into the world chunk meshes (so they receive the
// real sun shadows, PBR lighting, wind sway and HLOD like the rest of the city). Every instance is varied by the RNG.
#pragma once
#include "../core/util.h"
#include "meshbuilder.h"

namespace gtabr {

// Tree species: 0 mangueira, 1 ipe amarelo, 2 ipe roxo, 3 coqueiro, 4 palmeira imperial, 5 arbusto, 6 amendoeira, 7 flamboyant.
// Builds the high-detail tree into 'b' and a cheap silhouette (blob + trunk) into 'lod' (may be null). Returns the tree height.
float buildTree3D(MeshBuilder& b, MeshBuilder* lod, Rng& rng, int species, const Vec3& pos, float yaw, float scale);

// Street / beach furniture. Model ids:
//  0 lixeira, 1 banco, 2 poste de luz, 3 bomba de combustivel, 4 orelhao, 5 hidrante, 6 cone, 7 guarda-sol, 8 cadeira de praia,
//  9 quiosque, 10 vaso com planta, 11 caixas, 12 pneus, 13 tambor, 14 placa de rua, 15 torre de salva-vidas, 16 semaforo,
//  17 caixa de correio, 18 ponto de onibus, 19 outdoor, 20 placa PARE, 21 carrinho de supermercado, 22 banca de jornal,
//  23 bicicleta apoiada, 24 toalha de praia, 25 prancha de surf (em pe), 26 canteiro com flores
void buildProp3D(MeshBuilder& b, Rng& rng, int model, const Vec3& pos, float yaw, float scale = 1.0f);

}  // namespace gtabr
