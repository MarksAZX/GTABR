# Evolução visual — 0.4.0 Citylife

Continuação da mesma base C++20/Vulkan, com Android arm64 como alvo. A branch incorpora também as correções do remoto `claude/higgsfield-connection-ormagy` (`acbdc91`); mantém ruas, seed, slots, compras, inventário, armas, veículos, polícia e controles existentes.

## Alterações implementadas

- Materiais originais de asfalto, concreto, grama e folhagem gerados com GPT Image, integrados nas mesmas camadas GTEX; normal/roughness derivados, mipmaps e ASTC. Camadas não substituídas são preservadas byte a byte.
- Copas com normais suaves, variações, folhas periféricas, troncos segmentados, textura compartilhada e vento. Corrigidos o estouro da paleta e as coordenadas UV que antes eliminavam detalhes. Deformação compatível no passe de sombras.
- Relevo nas fachadas: varandas, guarda-corpos, molduras, condensadores. HLOD distante usa fachadas em vez de paredes vazias. Mais lixeiras, jardineiras, balizadores, fios com queda, vegetação costeira e caixas térmicas, sem bloquear acessos às lojas.
- Mais pedestres determinísticos; variedade de corpos já disponíveis, escala e roupas. Mantidos os rigs e as animações funcionais da base.
- Até oito carros de tráfego, dirigidos pela IA e física existentes, com parada diante de pedestres e veículos. Priorizados por distância, com pelo menos uma tentativa de geração próxima ao início, orçamento por qualidade e passagem pelos cruzamentos para trocar de faixa em avenidas. Podem ser assumidos pelo jogador e passam a ser persistentes, incluindo modelo e pintura; tráfego não assumido é transitório.
- Mar com deslocamento e normais coerentes, ondulações, espuma nas cristas, faixa de arrebentação, mistura de areia molhada, reflexos do céu e SSR costeiro existente. Malha offshore pequena mantém o horizonte; bounds de culling incluem a amplitude das ondas. Corrigida a grama elevada que cobria a areia.
- Respingos e anéis ao entrar/sair da água e ao se mover nela. Preservados profundidade, transições, natação e câmera existentes.
- Clima automático determinístico com chuva, nuvens, vento, escurecimento e umidade do chão; secagem progressiva. Chuva em sprites, impactos no chão e exclusão de interiores/áreas cobertas. Iluminação solar e névoa reagem ao clima; mantidos dia/noite, postes e faróis.
- Oclusão ambiente de contato por profundidade nos níveis médio/alto/ultra; sombras refinadas em alto/ultra; reflexos de nuvens nos materiais; menor ruído de película.
- UI existente preservada e ampliada com controles reais de clima e oclusão ambiente. Configurações persistem; clock, intensidade de chuva e umidade pertencem ao save do slot. Saves antigos recebem clima inicial seco.

## Orçamentos e limites

| Qualidade | Tráfego ativo próximo | Gotas máximas | AO | Refinamento de sombras |
|---|---:|---:|---|---|
| Baixo | 2 | 64 | desligado | base |
| Médio | 4 | 128 | 4 amostras | base |
| Alto | 6 | 220 | 8 amostras | PCF adicional |
| Ultra | 8 | 320 | 8 amostras | PCF adicional |

Mantidos instancing, três LODs dos modelos, impostores distantes, HLOD, culling, streaming com limite de uploads, deleção GPU diferida, pools e resolução dinâmica. A chuva não cria centenas de objetos de gameplay. A malha do horizonte tem apenas 12 vértices.

Os modelos continuam low-poly; não são assets AAA. SSR cobre apenas geometria visível da costa e recorre ao céu quando não encontra interseção. Água não usa simulação de fluidos; a superfície é opaca, com profundidade representada por cor. Chuva é um efeito visual; não há áudio novo de chuva nem simulação de drenagem. O tráfego usa a IA rodoviária existente, sem semáforos novos ou sistema completo de trânsito. O teste em dispositivo Android físico, FPS, aquecimento e compatibilidade de drivers ainda precisam de avaliação no aparelho.

## Ferramentas e origem

Foi usado o gerador GPT Image realmente disponível nesta sessão. A ferramenta não informa versão e não permite selecionar “2.5 medium”. “medium” no script refere-se ao encoder ASTC. Higgsfield aparece instalado, mas não expõe ferramenta compatível de geração nesta sessão; não houve geração nova por ele. Os personagens, carros, rigs e animações existentes foram preservados. A evolução dos modelos urbanos foi feita pelo modelador procedural real da base.

Fontes e manifestos: `assets/source/gpt_image/urban_atlas.png`, `urban_manifest.json` e os materiais costeiros já presentes. Não foram copiados assets de GTA ou outros jogos.

Reprodução dos materiais: `python tools/build_urban_assets.py`; fallback desktop: `python tools/decode_textures.py`.

## Verificação

Os resultados finais e o roteiro manual estão em `VALIDACAO_VISUAL.md`. O cenário `visual` verifica movimento do tráfego, aquisição e persistência de um carro ambiente, chuva/umidade, restauração do clima, natação e efeitos reais de água. A comparação congelada de quadros confirma que AO altera os pixels da geometria. O cenário `polish` exercita menus, acessibilidade, lojas/compras, slots, códigos, mapa, configurações, SSR e recuperação de saves.
