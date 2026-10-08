# Validação da etapa de cidade e persistência

Base preservada: `04820ba`, branch `codex/persistent-procedural-city`. Implementação incremental na engine C++20/Vulkan existente, sem recriar o projeto.

## Verificação executada

| Verificação | Resultado |
|---|---|
| CMake/Ninja desktop Release | Compilou |
| CTest `test_city` | 24 seeds, repetição idêntica de geometria/minimapa; costa nos quatro lados e mundo sem costa; passou |
| `menus`, seed 2 | Menu por toque, criar slot, quatro entradas/interiores/atendentes, compra real, dinheiro insuficiente, códigos, natação, save/load, proteção contra sobrescrita, segundo slot e exclusão: zero falhas |
| `resume --continue --seed 4294967295` após `menus` | Processo novo usou a seed 2 do disco, restaurou posição, dinheiro, itens, armas e munição: zero falhas |
| `mvp`, seed 1 | Andar/dirigir/abastecer/comprar/reparar pagando/salvar/carregar: zero falhas, 4069 frames simulados |
| `gameplay`, seed 1 | Dano real, reação de NPCs, pickups, disparos, polícia, perda de procura e persistência de armas: passou |
| `wanted3`, seed 1 | Polícia dispara/acerta, morte e respawn limpam procura: passou |
| Validação Vulkan nesses cenários | Nenhum erro ou aviso de validação nos logs finais |
| Gradle `assembleDebug` | APK arm64-v8a compilado, Android mínimo 26 |
| `apksigner verify --verbose` | Assinatura APK v2 válida |
| `git diff --check` | Sem erros de whitespace |

Os cenários usam input injetado e os sistemas reais da engine. Para acelerar o teste por software, os loops de simulação de alguns cenários renderizam somente nos checkpoints/screenshots; isso não é benchmark de FPS. A verificação de renderização adicional usa o preset alto com as duas câmeras.

## Reproduzir

Instale CMake, Ninja, headers/runtime Vulkan, glslc ou glslangValidator, zlib e a camada de validação. Para lavapipe sem ASTC, rode `python3 tools/decode_textures.py` com `astc-encoder-py` instalado.

```sh
cmake -S . -B build -G Ninja
cmake --build build -j 4
ctest --test-dir build --output-on-failure
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ./build/gtabr_headless --scenario menus --seed 2 --quality 0 --width 960 --height 540 --save build/test-save --out build/test-shots --validation
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ./build/gtabr_headless --scenario resume --continue --seed 4294967295 --quality 0 --width 960 --height 540 --save build/test-save --out build/resume-shots --validation
cd android
./gradlew assembleDebug
```

Use um diretório de saves vazio no primeiro teste `menus`; ele cria e usa o slot 01. Execute os demais cenários com diretórios separados para evitar compartilhar estado involuntariamente.

## Roteiro em aparelho Android — ainda pendente

1. Instalar o APK arm64, abrir e conferir o menu principal.
2. Criar um slot vazio; pausar, salvar, anotar a seed e a localização.
3. Ir a uma das quatro lojas pelo mapa, entrar pela porta, falar com o atendente e comprar. Conferir dinheiro, inventário e uso do consumível/equipamento.
4. Em uma seed costeira, ir à praia, caminhar até água funda, nadar e voltar à areia nas duas câmeras.
5. Testar todos os códigos com vida/dinheiro/armas/procura e veículo danificado/sem combustível.
6. Abrir mapa, ampliar, arrastar, centralizar; testar os botões Voltar e o Back do Android.
7. Alterar configurações e conferir o efeito visual/sonoro/controle. Fechar e verificar persistência.
8. Salvar dentro de uma loja e em um veículo; voltar ao menu, fechar o processo, reabrir e Continuar. Conferir posição, seed, dinheiro, itens, armas/munição, combustível e danos.
9. Criar outro slot sem sobrescrever o primeiro; cancelar e confirmar exclusão/substituição pela interface.
10. Percorrer a cidade por pelo menos 15 minutos em presets baixo/médio. Medir FPS, picos de frame time, memória, aquecimento e estabilidade durante suspensão/retomada.

## Limites e trabalhos seguintes

Não houve teste em aparelho Android físico nem certificação de ausência de crashes em todos os drivers. O teste disponível usa Vulkan por software (lavapipe).

Chunks/HLOD, LODs, instancing e deleção diferida estão implementados, mas a geração inicial ainda mantém os dados completos da cidade na CPU. Oclusão de setores e texture streaming por residência não foram implementados. Não há reflexos de cena por ray tracing/SSR nem simulação física de ondas: a água usa reflexão de céu e shader animado simples. As árvores/props novos são modelos procedurais próprios; as demais malhas de personagem e veículo vêm da base.

Os interiores são pequenos e usam iluminação ambiente, emissivos e luzes limitadas. A aparência foi melhorada dentro do renderer existente; não há promessa de qualidade AAA nem desempenho móvel medido.
