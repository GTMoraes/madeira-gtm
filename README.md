# madeira-gtm

Overlay de toque contextual para o GTA San Andreas (PC 1.0 US) rodando no Madeira.

- `dylib/GTMTouch.m`: overlay injetado no IPA oficial do Madeira. Troca o layout sozinho (a pé,
  nadando, jetpack, carro/moto/barco, helicóptero, avião, passageiro, cutscene, menu de pausa) e,
  dentro de cada layout, mostra só os botões que servem naquela hora: Sim/Não quando o jogo
  pergunta, Detonar/Jogar mais com satchel, Nitro, Hidráulica, Missão em veículo de submissão,
  Olhar para os lados com submetralhadora, Atirar 2 em aeronave armada, Recrutar/Ficar com o
  grupo, ícone do Atirar pela arma na mão, Entrar com o ícone do veículo perto. Editor de posição
  e tamanho por layout (lápis no canto). Ícones lidos de `Documents/gtmtouch/icons` no aparelho.
- `asi/GTMTouchSA.cpp`: plugin do jogo (vai em `GTA San Andreas/scripts`). Escreve contexto e
  estado e recebe os toques por `C:\gtmtouch.bin` (`Documents/wine/drive_c/gtmtouch.bin` no
  iPhone), entregando ao GInput como um controle. Inclui a direção pelas setas, salvar na hora,
  autosave e o veículo salvo junto. Protocolo 3 (128 bytes).
- `.github/workflows/build-ipa.yml`: baixa o Madeira, compila e injeta o dylib, publica o IPA numa Release.

Atualizar o Madeira: Actions > Madeira-GTM IPA > Run workflow (vazio = versão mais recente).
