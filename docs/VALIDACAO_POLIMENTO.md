# Polimento 0.3.0

Continuação da mesma engine C++20/Vulkan, após `3c9db56`. Mantém a geração por seed, os quatro estabelecimentos, combate, veículos, polícia, inventário e streaming existentes.

## Mudanças funcionais

- Portas bloqueiam deslocamento e combate durante o fade. Atendentes e personagem se orientam para a conversa. Compra mantém o catálogo aberto, mostra recibo/saldo e entrega o produto real.
- Entrada/saída de veículos usa uma trajetória de 0,75 s com interpolação suave, orientação e gesto corporal existentes. Saída procura espaço sem colisão. A suspensão do Android conclui a interação em andamento e salva uma posição consistente; o salvamento manual aguarda o fim da transição.
- Caminhada/natação mistura velocidade, flutuação e foco da câmera ao cruzar a profundidade de entrada, preservando a histerese e os clips de natação da base.
- Menus inicial, slots, confirmação, pausa, inventário, códigos e ajustes compartilham cartões sóbrios, hierarquia e paginação conforme o espaço da tela. Catálogos ocultam o HUD durante o atendimento; o HUD limita avisos simultâneos e evita sobrepor o prompt às legendas.
- Controles têm escala de 70–140%, opacidade de 30–100%, espelhamento para canhotos, joystick fixo/flutuante e editor por arraste para os oito botões e joystick. Ajustes são persistidos e usados nas áreas reais de toque. Restaurar padrão também funciona.
- Legendas de diálogo e identificação de ondas/motor/sirene podem ser ativadas separadamente; texto é quebrado por palavras. Alto contraste aumenta a opacidade dos painéis e textos; movimento reduzido elimina deslizamentos dos menus e tremor da câmera.
- Areia seca/úmida mistura albedo e normal/roughness por distância da costa, com pequena variação animada; as duas superfícies não se sobrepõem e a faixa úmida também existe no HLOD. Materiais naturais recebem uma segunda amostragem em escala/orientação diferente nos presets médio ou superiores. Copas têm lobos assimétricos; fachadas ganham variação de acabamento e relevo nos pavimentos/cobertura, mantendo o HLOD simples.
- Interiores têm ambiente menos uniforme, temperatura de luz por estabelecimento, luz do balcão e painéis de teto emissivos durante o dia. O limite de luzes continua dependente do preset.
- Baixo, médio, alto e ultra preservam os orçamentos de resolução, sombras, LOD, distância, decoração, luzes e animação. Reflexos podem ser desligados, usar o céu, ou acrescentar SSR costeiro. O SSR consulta a profundidade da cena e usa 8/12/20/28 passos conforme a qualidade.

## Saves

Formato v5 com CRC32, campos essenciais validados e leitura limitada a 4 MiB. Formatos v3/v4 continuam legíveis; o v4 da etapa anterior podia repetir `health` e é aceito.

Cada slot tem `.sav` principal e `.sav.bak` com a versão válida anterior. O primeiro salvamento cria uma cópia recuperável antes do principal. As gravações usam temporário, flush, fsync, rename e sincronização do diretório. Um principal inválido nunca substitui o backup válido. Temporários incompletos não são carregados.

A leitura procura o backup quando o principal é inválido/ausente. A lista de slots indica recuperação; a seed usada para gerar a cidade também vem desse backup. Se ambos forem inválidos, o slot é identificado como corrompido e não pode ser carregado. Substituição continua exigindo confirmação na UI; exclusão remove os arquivos associados.

A recuperação retorna ao último snapshot válido disponível, que pode ser anterior ao principal. CRC32 detecta corrupção acidental; os saves legados não têm checksum.

## Validação automatizada

- CMake/Ninja Release e shaders GLSL/SPIR-V compilados.
- CTest: determinismo de geometria/minimapa/HLOD em 24 seeds e todas as costas; toque em joystick/botão reposicionados, câmera/canhotos/modal; CRC, corrupção, arquivo excessivo, backup e legado.
- O teste de interrupção usa processos filhos e SIGKILL antes/depois de rename, tanto do backup quanto do principal. Confere que o slot e o backup continuam válidos. Isso testa encerramento de processo, não simula perda física de energia.
- Cenário `polish`, seed 7: UI por toque, paginação, presets/reflexos, arraste e persistência, acessibilidade, quatro lojas/compras, suspensão durante porta e entrada/saída de carro, transição de natação, seed/save/load, recuperação dentro do jogo e exclusão de outro slot.
- Reflexos verificados com cena e tempo congelados: ligar apenas o SSR altera pixels visíveis, além do fallback de céu. Imagens `coast_reflection_*` e `coast_ssr_verified` acompanham os logs.
- `resume` em processo novo usa a seed do slot mesmo com outra seed solicitada na linha de comando, e confere posição, dinheiro, inventário, armas e munição.
- Regressões `mvp` (andar, dirigir, combustível, compra, reparo pago e persistência), `gameplay` (combate/polícia) e `wanted3` (tiros, dano e respawn).
- APK debug arm64-v8a, Android 26+, versão 0.3.0-polish. Logs e assinatura APK incluídos na entrega.

## Limites de validação e de renderização

Os cenários usam Vulkan/lavapipe. Não há aparelho Android físico conectado: FPS, aquecimento, consumo de memória em aparelho e estabilidade em todos os drivers ainda precisam de medição.

O SSR cobre água/faixa costeira e reflete somente geometria visível na cena; usa o céu quando não encontra um objeto. Não é ray tracing nem reflexão de toda a cidade. O preset baixo e a opção de céu permitem evitar esse custo. A água continua usando ondas simples por shader.

A cidade conserva chunks, instancing, LOD/HLOD e deleção diferida. Os dados completos da cidade ainda permanecem na CPU; esta etapa não acrescentou streaming de texturas por residência nem oclusão de setores. As texturas refinadas usam o atlas GPT Image já integrado; não houve nova geração atribuída ao Higgsfield ou a uma versão/preset específico do gerador.
