# Bairro — jogo urbano brasileiro (Android, C++20 + Vulkan)

Continuação da base C++20/Vulkan existente: cidade procedural por seed, quatro comércios com interiores e compra, praia e natação, combate, veículos, posto, oficina e polícia. A cidade varia em distritos, ruas, avenidas, lotes e costa; o mapa e o minimapa usam os dados dessa geração.

## Jogar e persistir

O Android abre o menu principal. **Novo jogo** permite escolher um dos quatro slots; substituir ou excluir exige confirmação na interface. **Continuar** carrega o slot utilizado por último, e **Carregar jogo** mostra seed, data, localização, dinheiro e tempo jogado.

Cada slot guarda a seed exata, posição, dinheiro, itens, armas, munição, veículos persistentes, combustível, danos, câmera, hora, tempo jogado e estado de procura/progresso. O mundo é reconstruído antes de restaurar as entidades. Escrita atômica com sincronização em disco; saves antigos são migrados uma vez e o original é preservado.

A pausa reúne mapa com zoom/arraste, jogo, inventário utilizável, códigos com efeitos reais, configurações, salvar e menu principal. Configurações globais persistem separadamente e alteram resolução, sombras, distância, vegetação, efeitos, resolução dinâmica, limite de FPS Android, volume, sensibilidade, ciclo do dia e escala do HUD.

Na costa, andar para água profunda ativa natação; voltar à água rasa permite caminhar. Há animações de deslocamento e repouso, velocidade própria, flutuação, ondas simples, espuma e som de mar. Nas lojas, entre pela porta, interaja com o atendente e escolha o produto: o preço é descontado e o item/equipamento é entregue.

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

Para GPUs desktop sem ASTC, gere o fallback dos assets originais uma vez:

```sh
python3 -m pip install astc-encoder-py
python3 tools/decode_textures.py
```

**Cenários:**

| Cenário | O que faz |
|---|---|
| `start` | Gera as duas câmeras. |
| `look` | Gera manhã, tarde, pôr do sol e noite nas duas câmeras. |
| `char` | Mostra o personagem em idle, andando e correndo. |
| `mvp` | Caminhar, dirigir, abastecer, comprar, reparar pagando e salvar/carregar com input simulado. |
| `city` | Quatro lojas, compras, códigos, natação, persistência e independência dos slots. |
| `menus` | Acrescenta navegação real por toque do menu principal até a criação do slot. |
| `resume` | Com `--continue`, valida o save em um processo novo, incluindo seed, posição, dinheiro, itens e armas. |
| `gameplay` | Combate, reações dos NPCs, polícia e persistência de armas/munição. |
| `wanted3` | Perseguição e sobrevivência com procura máxima. |

**Opções:**

| Opção | Efeito |
|---|---|
| `--time H` | Define a hora do dia. |
| `--day-rate R` | Define a velocidade do ciclo do dia. |
| `--quality 0..3` | Escolhe o preset: BAIXO, MÉDIO, ALTO ou ULTRA. |
| `--scale S` | Define a escala de resolução. |
| `--seed N` | Seed reproduzível de teste; o Android gera uma nova por slot. |
| `--save DIRETÓRIO` | Isola os arquivos de uma sessão de teste. |
| `--continue` | Restaura o slot recente desse diretório. |
| `--validation` | Habilita validação Vulkan quando a camada estiver instalada. |

Teste determinístico sem renderização:

```sh
ctest --test-dir build --output-on-failure
```

`test_city` compara geometria e minimapa em 24 seeds, verifica os quatro lados de costa e seeds sem mar, interiores, spawn, geometria finita e orientação das superfícies de água.

## Cidade e orçamento gráfico

- Árvores de oito espécies com variação de forma/tamanho, três LODs e impostores distantes.
- Props urbanos próprios em 3D: postes, bancos, bombas, lixeiras, objetos de praia e outros; atlas compartilhado de albedo, normal e ORM.
- Instancing na GPU para árvores e props com modelo/material/LOD iguais; culling por distância e frustum.
- Chunks próximos detalhados, HLOD intermediário, descarregamento distante com histerese e uploads limitados por frame.
- Deleção de recursos da GPU adiada até a conclusão dos frames em voo; IDs reutilizados.
- O mapa completo continua disponível mesmo quando o chunk visual não está residente.

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

## Limitações conhecidas

- Ready Player Me não estava disponível; os personagens vêm do pipeline 3D do Higgsfield.
- Ainda não existem modelos próprios para mecânico e pedestre homem, por falta de créditos. Eles reutilizam outros corpos.
- Só houve teste em renderizador por software (lavapipe). O desempenho real precisa ser medido em aparelho.

Esta etapa foi validada em desktop headless com lavapipe e compilada para Android arm64. Não foi medida em aparelho Android físico. O APK de debug serve para instalação e teste; a assinatura de distribuição continua a cargo do projeto.

O streaming novo é de malhas na GPU: os dados da cidade ainda são gerados e mantidos na CPU. Não há novo sistema de oclusão nem streaming de texturas por residência nesta etapa; são próximos trabalhos de otimização, não recursos concluídos. As ondas usam shader simples, sem simulação física de fluidos.
