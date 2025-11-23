# LZOM - LZO Scatter-Gather Implementation

Расширенная реализация алгоритма сжатия LZO для ядра Linux с поддержкой scatter-gather буферов (`struct bio_vec`). Работает с блочными устройствами без промежуточного копирования данных.

## Установка

1. Сборка модуля:
```bash
   make
```

2. Загрузка модуля:
```bash
   sudo insmod lzom_module.ko
```

## Использование

```bash
   echo -n "<path_to_your_block_device>" > /sys/module/lzom_module/parameters/path
```

## Тестирование

Запуск автотестов:
```bash
   sudo ./test/run_tests.sh
```