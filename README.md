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
  botão TAB contextual (status, telefone, trocar pela arma do chão). "Olhar atrás" dirigindo e de passageiro. Teclado do iPhone quando o jogo pede um
  texto (placa dos carros da garagem). Giroscópio: gira só a câmera do jogo, sempre na mira e, se quiser,
  também a pé e dirigindo (aí a câmera vira e fica, como o mouse; "Olhar atrás" no veículo e "Centralizar" a pé
  voltam ela para trás), com sensibilidade horizontal, vertical e na mira no painel do editor (lápis > Giroscópio).
  Depois do teclado, o foco do teclado volta para o Madeira.
- `asi/GTMTouchSA.cpp`: plugin do jogo (vai em `GTA San Andreas/scripts`). Escreve contexto e
  estado e recebe os toques por `C:\gtmtouch.bin` (`Documents/wine/drive_c/gtmtouch.bin` no
  iPhone), entregando ao GInput como um controle. Inclui a direção pelas setas, salvar na hora,
  autosave e o veículo salvo junto, correções do x87 (sons de missão e clima), pular os vídeos de abertura,
  radar no canto de cima a esquerda, proteção da arma fora da memória (sem ícone, mãos vazias, travar ao mirar),
  recarregar pelo toque no ícone da arma, save dentro do carro e o Skin Selector: menu com Status + Atirar,
  skin gravada junto com o save e o jeito de andar e a voz do personagem da skin (também nas respostas a pedestres,
  nas ordens aos recrutados e nas respostas à namorada), o trânsito sem carro surgindo logo à frente quando você olha para trás, o Vigilante com reforço da polícia
  pelo Sim/Não e recompensas por nível (12: mirar em policial não dá estrela, recrutar policial como parceiro de
  gangue, armeiro da polícia no Vigilante e o procurado caindo mais rápido; 21: roubo de carro e batida em viatura
  sem estrela e até 3 recrutas; 32: Vigilante sem timer; 64: Vigilante a qualquer hora, a pé ou em qualquer veículo,
  a polícia ignorando seus crimes e indo atrás dos seus alvos; salvar no Vigilante como checkpoint: o save volta no
  mesmo nível, com as kills e o reforço), as policiais femininas do SA-MP 0.3.7 na rua e no
  reforço (o limite de 278 modelos de pedestre do jogo sobe para 350), a mira do drive-by (de moto ou de carro, o
  tiro vai no suspeito do Vigilante, no veículo dele ou em quem atacou você; sem alvo, só a altura do tiro é acertada
  no pedestre ou veículo à frente), a mira a pé que prefere o suspeito (até dentro do carro) e, no nível hitman, passa
  sozinha para o próximo agressor, a pistola silenciada .45 mais rápida, botões próprios do Vigilante no overlay
  (Reforço, Viatura, Recrutar policial, Vigilante), trocar de arma no veículo e fazer drive-by com pistola, SMG,
  fuzil ou escopeta (recarregando pelo toque no ícone da arma, como a pé), atirar para a frente no carro quebrando
  o para-brisa, recrutas que ficam no seu veículo atirando da janela e voltam para você quando chamados, e a garagem pelo telefone
  (Status + Pular: os carros das garagens das casas viram seus; um membro da Grove entrega, busca e leva
  para a garagem; venda no mercado negro; os amassados do LocalDent vao junto, com o LocalDent modificado que
  exporta o estado de cada carro; cada garagem guarda quantos carros couberem nela e o carro que nao cabe fica
  seu, "sem vaga"; troca de placa que acompanha o carro; os patios da policia ficam de fora) e o traficante de
  armas pelo mesmo telefone (van preta que para no seu nivel e e puxada para perto se se perder; as portas de tras
  abrem quando voce chega na traseira; arma uma vez e depois so a municao, em
  caixas; precos pela fase da AmmuNation, com o mesmo calculo do script da loja, e as pesadas liberadas com Las
  Venturas; a policia so reage se ver de perto, por tras, a van aberta; parado atras da van, ele pergunta se voce quer comprar, e da para
  dispensar ele), dar a arma que voce segura para o pedestre na sua frente (Status + Correr, com um cumprimento de
  gangue) e as armas de fogo protegidas em missao (a missao nao troca nem tira pistola, escopeta, submetralhadora
  e fuzil de assalto seus: a arma dela vira municao da sua; municao infinita volta ao que era no fim, mais uma
  caixa se vencer; o que a missao tira de uma vez volta quando ela termina).
  Protocolo 5 (256 bytes: jogo 0-63, overlay 64-95, giroscópio 96-127, pedido de teclado 128-191, resposta 192-255).
- `.github/workflows/build-ipa.yml`: baixa o Madeira, compila e injeta o dylib, publica o IPA numa Release.
  Fonte padrao: o fork GTMoraes/Madeira (FEX de GTMoraes/FEX, branch gtm-x87pc: o x87 respeita a precisao
  simples que o Direct3D 9 pede; `env.MADEIRA_X87_PC = 0` desliga). A fonte "oficial" usa willfaust/Madeira.

Atualizar o Madeira: Actions > Madeira-GTM IPA > Run workflow (fonte fork ou oficial).
