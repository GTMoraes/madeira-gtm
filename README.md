# madeira-gtm

Overlay de toque contextual para o GTA San Andreas (PC 1.0 US) rodando no Madeira.

- `dylib/GTMTouch.m`: overlay injetado no IPA oficial do Madeira. Troca o layout sozinho (a pé,
  nadando, jetpack, carro/moto/barco, helicóptero, avião, passageiro, cutscene, menu de pausa) e,
  dentro de cada layout, mostra só os botões que servem naquela hora: Sim/Não quando o jogo
  pergunta, Detonar/Jogar mais com satchel, Nitro, Hidráulica, Missão em veículo de submissão,
  Olhar para os lados com submetralhadora, Atirar 2 em aeronave armada, Recrutar/Ficar com o
  grupo, ícone do Atirar pela arma na mão, Entrar com o ícone do veículo perto. Editor de posição
  e tamanho por layout (lápis no canto). Ícones lidos de `Documents/gtmtouch/icons` no aparelho.
  Troca de arma deslizando sobre o ícone da arma do jogo (um toque sem deslizar recarrega a arma, quando dá);
  botão TAB contextual (status, telefone, trocar pela arma do chão). Teclado do iPhone quando o jogo pede um
  texto (placa dos carros da garagem).
- `asi/GTMTouchSA.cpp`: plugin do jogo (vai em `GTA San Andreas/scripts`). Escreve contexto e
  estado e recebe os toques por `C:\gtmtouch.bin` (`Documents/wine/drive_c/gtmtouch.bin` no
  iPhone), entregando ao GInput como um controle. Inclui a direção pelas setas, salvar na hora,
  autosave e o veículo salvo junto, correções do x87 (sons de missão e clima), pular os vídeos de abertura,
  radar no canto de cima a esquerda, proteção da arma fora da memória (sem ícone, mãos vazias, travar ao mirar),
  recarregar pelo toque no ícone da arma, save dentro do carro e o Skin Selector: menu com Status + Atirar,
  skin gravada junto com o save e o jeito de andar e a voz do personagem da skin, e a garagem pelo telefone
  (Status + Pular: os carros das garagens das casas viram seus; um membro da Grove entrega, busca e leva
  para a garagem; venda no mercado negro; os amassados do LocalDent vao junto, com o LocalDent modificado que
  exporta o estado de cada carro; cada garagem guarda quantos carros couberem nela e o carro que nao cabe fica
  seu, "sem vaga"; troca de placa que acompanha o carro; os patios da policia ficam de fora).
  Protocolo 4 (256 bytes: jogo 0-63, overlay 64-127, pedido de teclado 128-191, resposta 192-255).
- `.github/workflows/build-ipa.yml`: baixa o Madeira, compila e injeta o dylib, publica o IPA numa Release.
  Fonte padrao: o fork GTMoraes/Madeira (FEX de GTMoraes/FEX, branch gtm-x87pc: o x87 respeita a precisao
  simples que o Direct3D 9 pede; `env.MADEIRA_X87_PC = 0` desliga). A fonte "oficial" usa willfaust/Madeira.

Atualizar o Madeira: Actions > Madeira-GTM IPA > Run workflow (fonte fork ou oficial).
