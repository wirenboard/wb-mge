# WB-MGE
Мультипротокольный шлюз Wirenboard

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

### 1. Установка ESP-IDF

```bash
# Клонирование репозитория ESP-IDF
git clone --branch v5.4 --single-branch --depth 1 --recurse-submodules --shallow-submodules https://github.com/espressif/esp-idf.git

# Запуск скрипта установки
cd esp-idf
./install.sh

# Настройка переменных окружения
source export.sh
```

### 2. Клонирование репозитория

```bash
git clone git@github.com:wirenboard/wb-mge.git
cd wb-mge
```

### 3. Генерация файла ключей

``` bash
cd copy_protection
./keygen.py --random --keys_file keys.txt
```

### 4. Сборка проекта

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

**Важно:** Использование `idf.py build` напрямую НЕ встроит информацию о версии в бинарник. Всегда используйте `make build-idf-project` для production сборок.

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

Сборка внутри контейнера не отличается от обычной сборки (см. раздел "Инструкции по ручной сборке", пункты 3 и 4).

### Альтернатива: сборка одной командой

Можно собрать проект, не заходя в контейнер:

```bash
docker run --rm -v $(pwd):/root/esp/project wb-mge-builder bash -c "cd copy_protection && ./keygen.py --random --keys_file keys.txt && cd .. && make"
```

Или если ключи уже сгенерированы:

```bash
docker run --rm -v $(pwd):/root/esp/project wb-mge-builder make
```

## Прошивка устройства

```bash
idf.py -p /dev/ttyACM* flash
```

## Подключение к консоли устройства

```bash
idf.py monitor
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
