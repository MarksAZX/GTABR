# Assets costeiros

`coastal_atlas.png` foi gerado pelo GPT Image disponível nesta sessão e integrado ao jogo. O modelo exato e o preset de geração não são expostos pela ferramenta; não se afirma que foi GPT Image 2.5 medium. O Higgsfield apareceu instalado no catálogo, mas não expôs ferramentas de geração. A origem dos demais assets Higgsfield da base permanece preservada.

Quadrantes: mar (superior esquerdo), areia seca (superior direito), máscara de espuma (inferior esquerdo), areia úmida (inferior direito). A fonte é preservada integralmente; recorte, repetição, mipmaps e compressão usam o pipeline de assets do jogo.

Para reconstruir somente a costa preservando os bytes dos outros materiais:

```sh
python3 -m pip install numpy Pillow scipy astc-encoder-py
python3 tools/build_coastal_assets.py --quality medium
python3 tools/decode_textures.py
```

`medium` nesse comando é o preset do compressor ASTC, independente do preset de geração. Normais/roughness são derivados pelo pipeline existente. IDs anteriores permanecem estáveis; foam=40 e sand_wet=41 foram acrescentados.

O build completo `python3 tools/build_assets.py --only materials --quality medium` também consome esse atlas, mas recomprime todos os materiais. Use o script específico para alterações apenas da costa.
