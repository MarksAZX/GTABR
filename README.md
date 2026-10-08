# Bairro — jogo urbano brasileiro (Android, C++20 + Vulkan)

Um bairro pequeno e jogável, com posto de gasolina (R$20 / R$50 / completar), mercado (entrar, conversar, comprar) e oficina (conserto pago).
Tem câmera Top Down e câmera em Terceira Pessoa, com transição suave.
Há NPCs andando em navmesh, roda de itens com blur, dinheiro real e save/load.

## Build

### APK (arm64-v8a, minSdk 26)

```sh
cd android && ./gradlew assembleDebug
```

O APK sai em `android/app/build/outputs/apk/debug/app-debug.apk`.

### Desktop headless (lavapipe), para testes e screenshots

```sh
cmake -S . -B build && cmake --build build -j
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ./build/gtabr_headless --scenario look --out shots
```

**Cenários:**

| Cenário | O que faz |
|---|---|
| `start` | Gera as duas câmeras. |
| `look` | Gera manhã, tarde, pôr do sol e noite nas duas câmeras. |
| `char` | Mostra o personagem em idle, andando e correndo. |
| `mvp` | Roda o loop completo com input simulado e verificações. |

**Opções:**

| Opção | Efeito |
|---|---|
| `--time H` | Define a hora do dia. |
| `--day-rate R` | Define a velocidade do ciclo do dia. |
| `--quality 0..3` | Escolhe o preset: BAIXO, MÉDIO, ALTO ou ULTRA. |
| `--scale S` | Define a escala de resolução. |

## Visual

### Renderização

- Cena em HDR (B10G11R11, com fallback para RGBA16F).
- Composição final:
  - exposição;
  - bloom (cadeia Dual-Kawase);
  - tonemap ACES;
  - grading (lift/gain, contraste, saturação);
  - vinheta e grão.
- PBR GGX no mundo, nos carros (com clear coat) e nos personagens.
- Normal, roughness e cavity derivados para os materiais do mundo (`materials_n.gtex`).
- Sombras do sol em 2 cascatas com PCF.
- Céu analítico com nuvens FBM, sol e estrelas, mais névoa de altura.

### Hora do dia

O ciclo vai de madrugada a noite (manhã, meio-dia, tarde, pôr do sol, crepúsculo, noite). À noite acendem postes, faróis e lanternas, com até 16 luzes dinâmicas.

### Personagens 3D

- **Origem:** conceito no GPT Image 2.5, malha PBR no Tripo H3.1 e rig mais animações no Meshy, todos via Higgsfield.
- **Animação:**
  - blend idle/andar/correr pela velocidade real;
  - velocidade de reprodução casada com a passada (sem pé deslizando);
  - camadas procedurais: conversar, interagir/comprar, abastecer, agachar para entrar no carro, inclinar na curva, olhar;
  - variação por NPC (escala, fase, ritmo);
  - NPCs distantes com atualização de animação em taxa reduzida.
- **Pesos de skin:** os do auto-rig misturavam membros, então o runtime reconstrói os pesos por distância aos ossos.
- **Idle:** é procedural, porque o clipe "idle" de estoque marchava no lugar.

### Carros 3D

- Malhas do Tripo H3.1 com 3 LODs.
- As rodas originais são cortadas e trocadas por rodas procedurais, que giram e esterçam.
- Suspensão visual (pitch/roll).
- Luzes: faróis, freio, ré e setas.
- Máscara de pintura para variar a cor.

### Presets

Os presets BAIXO, MÉDIO, ALTO e ULTRA mudam:

- escala de render;
- tamanho e número de cascatas de sombra;
- bloom;
- distância de desenho;
- viés de LOD;
- número de luzes;
- quantos NPCs são animados em taxa cheia.

A resolução dinâmica é opcional.

## Pipeline de assets

| Etapa | Ferramenta |
|---|---|
| Texturas, sprites, fontes, ícones | `tools/build_assets.py` |
| Modelos 3D: GLB para `.gmesh`/`.ganim` + GTEX (ASTC 6x6 + fallback RGBA) | `tools/meshconv` (cgltf + meshoptimizer) e `tools/build_models.py` |
| Rig: esqueleto único ajustado à malha de cada personagem + pesos de pele pintados | `tools/rigfit.py` (roda depois do meshconv; marca o `.gmesh` com `kRigFitted`) |

### Rig e animação

- Todos os personagens compartilham os mesmos 24 ossos e os mesmos clipes (biblioteca Meshy via Higgsfield). O `rigfit` mede cada malha (fatias de membros, pontas das mãos, centros de tronco/cabeça), coloca as juntas dentro do corpo, mantém as rotações de repouso dos clipes e recalcula `invBind` para que a pose de repouso (braços relaxados) e a pose de bind (malha em A-pose) sejam consistentes.
- Pesos: distância a segmentos de osso (com lado esquerdo/direito separados) suavizada sobre a superfície da malha, 4 influências por vértice.
- Grafo de animação (`Animator`): camada base com estados Ground (idle / ready stance / walk / run por velocidade real), Swim, Air (clipe de pulo guiado pela física) e Dead, com fades temporizados a partir de um snapshot da pose; camada de ações pontuais com taxas de blend por ação; passos de giro no lugar; camadas procedurais aditivas só para gestos (fala, alcançar, abastecer, olhar).

### Renderização

- SSAO em meia resolução com blur bilateral, raios de luz volumétricos, reflexos em tela no chão molhado, bloom Dual-Kawase, ACES + grade.
- Grade de probes de visibilidade do céu assada por cidade (ambiente local e oclusão especular), até 128 luzes dinâmicas culladas em tiles de tela, decals texturizados (rachaduras, óleo, bueiros, grelhas, marcas de pneu, folhas, remendos, grafites, sujeira, cartazes).

## Limitações conhecidas

- Ready Player Me não estava disponível; os personagens vêm do pipeline 3D do Higgsfield.
- Os pesos de pele e as juntas são gerados automaticamente (`tools/rigfit.py`), não pintados à mão.
- Texturas de albedo vêm do GPT Image; normais/rugosidade são derivadas com micro-relevo autoral por material. Não há mapas PBR escaneados.
- Só houve teste em renderizador por software (lavapipe). O desempenho real precisa ser medido em aparelho.

## 0.7.0

- Bicos de entrega (balcão das lojas), XP, níveis e desconto nas lojas; chip de nível na HUD.
- Conversa limpa, sem fundo, com o **modelo 3D do personagem renderizado ao vivo** (passe offscreen próprio com câmera e luz de estúdio).
- Mira (ponto) com arma de fogo; correr em 3 toques (corre, corre rápido, para); movimento mais pesado.
- NPCs derrubados pelo jogador soltam dinheiro.
- Salto contextual sobre obstáculos baixos, esquiva e bloqueio, hitstop, mundo vivo (lojas abrem/fecham, trânsito por hora).
- O ZIP `Bairro-v0.7.0-completo.zip` (raiz do projeto, não versionado por causa do tamanho) traz todos os arquivos e o APK.
