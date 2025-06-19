# Sistema de Controle de Bomba com Interface Web

---

## 🎯 Descrição do Projeto
Projeto desenvolvido no contexto do EmbarcaTech, utilizando a Raspberry Pi Pico W para monitoramento de nível, acionamento automático de bomba, sinalização visual e sonora, além de uma interface web para configuração remota.

---

## ⚙️ Requisitos

- **Microcontrolador**: Raspberry Pi Pico W
- **Editor de Código**: Visual Studio Code (VS Code)
- **SDK do Raspberry Pi Pico** devidamente configurado
- **Ferramentas de build**: CMake e Ninja

---

## Instruções de Uso

### 1. Clone o Repositório
```bash
git clone https://github.com/rafaelsouz10/controle-nivel-liquido.git
cd controle-nivel-liquido
code .
```

---

### 2. Instale as Dependências

### 2.1 Certifique-se de que o SDK do Raspberry Pi Pico está configurado corretamente no VS Code. As extensões recomendadas são:

- **C/C++** (Microsoft).
- **CMake Tools**.
- **Raspberry Pi Pico**.


### 3. Configure o VS Code

Abra o projeto no Visual Studio Code e siga as etapas abaixo:

1. Certifique-se de que as extensões mencionadas anteriormente estão instaladas.
2. No terminal do VS Code, compile o código clicando em "Compile Project" na interface da extensão do Raspberry Pi Pico.
3. O processo gerará o arquivo `.uf2` necessário para a execução no hardware real.

---

### 4. Teste no Hardware Real

#### Utilizando a Placa de Desenvolvimento BitDogLab com Raspberry Pi Pico W:

1. Conecte a placa ao computador no modo BOTSEEL:
   - Pressione o botão **BOOTSEL** (localizado na parte de trás da placa de desenvolvimento) e, em seguida, o botão **RESET** (localizado na frente da placa).
   - Após alguns segundos, solte o botão **RESET** e, logo depois, solte o botão **BOOTSEL**.
   - A placa entrará no modo de programação.

2. Compile o projeto no VS Code utilizando a extensão do [Raspberry Pi Pico W](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico):
   - Clique em **Compile Project**.

3. Execute o projeto clicando em **Run Project USB**, localizado abaixo do botão **Compile Project**.

---

### 🔌 5. Conexões e Esquemático
Abaixo está o mapeamento de conexões entre os componentes e a Raspberry Pi Pico W:

| **Componentes**        | **Pino Conectado (GPIO)** |
|------------------------|---------------------------|
| Display SSD1306 (SDA)  | GPIO 14                   |
| Display SSD1306 (SCL)  | GPIO 15                   |
| Potênciometro          | GPIO 28                   |
| LED_VERDE              | GPIO 16                   |
| LED VERMELHO           | GPIO 17                   |
| Buzzer                 | GPIO 21                   |
| Botão A                | GPIO 5                    |
| Botão B                | GPIO 6                    |
| MATRIZ DE LEDS RGB     | GPIO 7                    |
| RELÉ                   | GPIO 8                    |

#### 🛠️ Demais Hardwares Utilizado
- **Microcontrolador Raspberry Pi Pico W**
- **Wi-Fi (CYW43439)**

---

### 📌 6. Funcionamento do Sistema

**Monitoramento do nível**: leitura do potenciômetro (ADC) acoplado a bóia de nível

Acionamento automático da bomba:
- Liga quando nível < **min**
- Desliga quando nível > **max**

Sinalização visual:
- LED verde: **bomba ligada**
- LED vermelho: **bomba desligada**
- Matriz WS2812: **animação conforme nível**

Sinalização sonora:
- Buzzer soa em **nível crítico** (acima do máximo)

Interface web:
- Exibe informações em **tempo real**
- Permite configuração de **min/max**

#### 🌐 6.1 Interface Web

- ✅ Mostra o nível (valor + barra de progresso)
- ✅ Estado da bomba (Ligada / Desligada)
- ✅ Configura limites mínimo e máximo
- ✅ Botão para resetar limites para padrão

---

### 8. Vídeos Demonstrativo

**Click [AQUI](LINK DO VÍDEO VAI AQUI) para acessar o link do Vídeo Ensaio**
