# madeira-gtm

Overlay de toque contextual para o GTA San Andreas (PC 1.0 US) rodando no Madeira.

- `dylib/GTMTouch.m`: overlay injetado no IPA oficial do Madeira. Troca os botões sozinho
  (a pé, carro, moto, helicóptero, avião, barco, passageiro) conforme o contexto do jogo.
- `asi/GTMTouchSA.cpp`: plugin do jogo (vai em `GTA San Andreas/scripts`). Escreve o contexto e
  recebe os toques por `C:\gtmtouch.bin` (`Documents/wine/drive_c/gtmtouch.bin` no iPhone),
  entregando ao GInput como um controle. Inclui a direção pelas setas.
- `.github/workflows/build-ipa.yml`: baixa o Madeira, compila e injeta o dylib, publica o IPA numa Release.

Atualizar o Madeira: Actions > Madeira-GTM IPA > Run workflow (vazio = versão mais recente).
