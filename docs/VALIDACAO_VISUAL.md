# Validação — 0.4.0 Citylife

Mesma base Android/Vulkan, branch `codex/persistent-procedural-city`, integrada com o remoto `acbdc91`. Verificação feita em 08/10/2026. O teste headless executa a engine e o renderer Vulkan reais, com Lavapipe e validation layers; não substitui a avaliação no celular.

## Resultados

| Verificação | Resultado |
|---|---|
| Compilação C++/shaders desktop | concluída |
| Android `assembleDebug`, arm64-v8a, API mínima 26 | concluída; versão 0.4.0, code 5 |
| Assinatura APK v2 e conteúdo do pacote | verificados; certificado debug preservado |
| CTest | 4/4: cidade, controles, recuperação de saves, clima |
| Cidade | 24 seeds, duas reconstruções por seed, hash de meshes/HLOD/mapa idêntico; todas as direções de costa e cidade sem costa |
| MVP seeds 1 e 7 | abastecimento, entrada/compra na loja, reparo pago, save/load, sem falhas |
| Visual baixo/seed 1, médio/2, alto/7, ultra/3 | tráfego em movimento, chuva/umidade, clima persistente, natação, anéis reais, aquisição de veículo e restauração de carro/pintura: sem falhas |
| AO, quadro congelado | médio: 9.422 pixels; alto: 31.465; ultra: 6.839 pixels alterados pela oclusão; resoluções de teste diferentes |
| Menus/lojas/persistência (`polish`, seed 7) | botões e configurações reais, quatro lojas/compras, códigos, controles, slots e backup; sem falhas; SSR altera 74 pixels no quadro congelado de 480×270 |
| Combate (`gameplay`, seed 7) | ataques, recarga, reação de NPCs, busca policial e persistência de armas/munição; sem falhas |
| Polícia (`wanted3`, seed 1) | policiais disparam, atingem o jogador, morte e respawn removem procurado; sem falhas |
| Reinício em outro processo | `resume --continue --seed 4294967295` recupera seed 7, posição, dinheiro, inventário e armas do save |

Os cenários de menus/compras/SSR, combate e polícia também são exercitados. Logs completos, capturas e APK acompanham a entrega. Nenhum número de FPS aqui representa desempenho Android.

O piloto do cenário MVP agora segue cruzamentos para trocar de lado em avenidas, faz curvas com pontos intermediários, prevê manobras com `stepVehicle` e contorna o carro estacionado ao caminhar até a loja. Apenas o teste usa essa previsão: continuam ativos física, colisões, consumo, tráfego e interações do gameplay. Não foram removidos canteiros ou veículos para fazer o teste passar.

O teste de saves encerra subprocessos à força antes/depois dos renames de backup e arquivo principal, além de corromper arquivos e verificar recuperação/CRC. Isso cobre persistência no filesystem de testes; não simula todos os controladores de armazenamento Android.

## Reprodução

```sh
cmake -S . -B build -G Ninja
cmake --build build -j 4
ctest --test-dir build --output-on-failure
./build/gtabr_headless --scenario visual --seed 7 --quality 2 --validation --save build/visual-save --out build/visual-shots
./build/gtabr_headless --scenario mvp --seed 7 --quality 0 --validation --save build/mvp-save --out build/mvp-shots
./build/gtabr_headless --scenario polish --seed 7 --quality 2 --validation --save build/polish-save --out build/polish-shots
./build/gtabr_headless --scenario gameplay --seed 7 --validation --save build/gameplay-save --out build/gameplay-shots
./build/gtabr_headless --scenario wanted3 --seed 1 --validation --save build/wanted-save --out build/wanted-shots
cd android
./gradlew assembleDebug
```

Necessários Vulkan/glslang, CMake/Ninja e, no Android, SDK 34/NDK 27.2/JDK 17. A configuração local de proxy/toolchain da sessão não faz parte do projeto. O APK usa os assets ASTC existentes; o fallback RGBA do desktop pode ser reproduzido com `python tools/decode_textures.py`.

## Roteiro no aparelho

1. Instalar o APK sobre a versão anterior assinada com o mesmo certificado, sem limpar os dados; carregar um slot antigo e confirmar seed, inventário e veículo.
2. Criar um slot novo, percorrer ruas/parques/praia; conferir portas acessíveis, pedestres, carros, streaming e sombras durante deslocamento rápido.
3. Entrar nas quatro lojas, conversar, comprar; conferir dinheiro e item. Abastecer e reparar um carro com pagamento.
4. Testar baixo/médio/alto/ultra e resolução dinâmica; comparar sombras, vegetação, AO e reflexos desligados/céu/SSR. Medir FPS e aquecimento por 15 minutos em cada preset apropriado ao aparelho.
5. Alternar clima automático/limpo/chuva, aguardar molhamento e secagem; conferir chão, nuvens, iluminação e faróis à noite.
6. Percorrer areia seca/molhada, entrar na água, nadar e sair; verificar espuma, ondas, anéis e câmera. Abrir mapa, pausa, inventário, códigos e configurações.
7. Ajustar tamanho/posição dos controles, legendas e contraste; verificar menu em paisagem, notch e diferentes resoluções.
8. Salvar, suspender, encerrar o aplicativo e continuar. Confirmar posição, estado climático, dinheiro, itens, arma/munição e carro adquirido. Testar interrupção do aplicativo durante saves com uma cópia de segurança dos slots.

Ainda pendentes: testes físicos Android, GPU/driver, áudio no aparelho, FPS, consumo de memória e temperatura. O APK entregue é de teste, assinado com chave debug; não é uma publicação na Play Store.
