# WB-MGE
Мультипротокольный шлюз Wiren Board

[English README](README.md)

## Описание проекта

WB-MGE предназначен для подключения устройств с интерфейсом RS-485 и боковых модулей
ввода-вывода WBIO к серверу автоматизации через Ethernet или Wi-Fi.

Для каждого из портов доступно два режима:

 - Modbus TCP — только для Modbus-устройств
 - Прозрачный шлюз — подходит для любых протоколов, работающих поверх RS-485.

## Инструкции по ручной сборке

### Необходимые инструменты

1. **Node.js 20.x** (требуется версия 20.x)
2. **Python 3.8+** (требуется версия 3.8 или выше)
3. **Git**

**Note:** Приведенные ниже инструкции предназначены для Debian/Ubuntu систем

### 0. Установка Node.js 20.x

```bash
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt-get install -y nodejs
```

### 1. Установка ESP-IDF через EIM

Debian/Ubuntu:

```bash
echo "deb [trusted=yes] https://dl.espressif.com/dl/eim/apt/ stable main" | sudo tee /etc/apt/sources.list.d/espressif.list
sudo apt update
sudo apt install eim-cli
```

macOS:

```bash
brew tap espressif/eim
brew install eim
```

Установка IDF:

```bash
eim install -i v5.4
```

Makefile активирует окружение EIM автоматически — `source` перед `make` не нужен.


### 2. Клонирование репозитория

```bash
git clone git@github.com:wirenboard/wb-mge.git
cd wb-mge
```

### 3. Сборка проекта

Для полной сборки проекта (юниттесты + frontend + прошивка):

```bash
make
```

Если компоненты собираются отдельно, то сначала соберите frontend:

```bash
make build-frontend
```

Затем прошивку:

```bash
make build-idf-project
```

## Сборка с использованием Docker

Docker позволяет собирать проект без установки ESP-IDF и Node.js на хост-систему.

### 0. Установка Docker

Установите Docker согласно официальной документации для вашей ОС:
https://docs.docker.com/get-started/get-docker/

### 1. Сборка Docker образа

```bash
# Из корневой директории проекта
docker build -t wb-mge-builder .
```

Это создаст Docker образ с:
- ESP-IDF v5.4
- Node.js 20.x
- Всеми необходимыми инструментами для сборки

### 2. Запуск контейнера

```bash
docker run --rm -it -v $(pwd):/root/esp/project wb-mge-builder
```

### 3. Сборка внутри контейнера

Сборка внутри контейнера не отличается от обычной сборки (см. раздел "Инструкции по ручной сборке", пункт 3).

### Альтернатива: сборка одной командой

Можно собрать проект, не заходя в контейнер:

```bash
docker run --rm -v $(pwd):/root/esp/project wb-mge-builder make
```


## Прошивка устройства

```bash
make flash
```

Для прошивки всех разделов явно (загрузчик, таблица разделов, OTA-данные, приложение):

```bash
make flash-all
```

## Подключение к консоли устройства

```bash
make monitor
```

Для отключения от монитора нажмите `Ctrl+]`.

## Очистка

```bash
make clean
```

Или внутри контейнера:

```bash
docker run --rm -v $(pwd):/root/esp/project wb-mge-builder make clean
```

Удалить Docker образ:
```bash
docker rmi wb-mge-builder
```
