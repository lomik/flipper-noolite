# NooLite для Flipper Zero

Приложение (.fap) для работы с классическим протоколом **nooLite** (433.92 МГц) через встроенный радиомодуль Flipper Zero:

- **Приём**: показывает команды пультов nooLite и показания датчиков PT111/PT112 (температура, влажность, разряд батареи). Нажатие OK на записи сохраняет её адрес как пульт.
- **Виртуальный пульт**: On / Off / Toggle, плавная регулировка (удержание ▲/▼), шаг яркости (◀/▶), установка уровня, RGB, Roll/Switch color, Switch mode/speed, пресеты, временное включение, привязка и отвязка.
- Пульты хранятся в `/ext/apps_data/noolite/remotes.txt`.

nooLite-F (AES, двусторонняя связь) не поддерживается.

## Управление пультом

| Кнопка | Действие |
|---|---|
| ▲ / ▼ | On / Off |
| OK | Toggle (Switch) |
| Удержание ▲ / ▼ | плавно ярче / темнее, отпускание — Stop |
| ◀ / ▶ | уровень −16 / +16 (SetLevel) |
| Удержание OK | меню: Bind, Unbind, Set level, RGB, цвета и режимы, пресеты, Temporary on, переименование, удаление |

Привязка к силовому блоку: переведите блок в режим привязки (кнопка на блоке), затем выберите **Bind (pair)** в меню пульта.

## Сборка

Нужен [ufbt](https://github.com/flipperdevices/flipperzero-ufbt) и SDK той прошивки, что стоит на устройстве.

```bash
python3 -m venv .venv && .venv/bin/pip install ufbt
.venv/bin/ufbt update --local=<firmware>/dist/f7-C/flipper-z-f7-sdk-local.zip   # или --channel=release
.venv/bin/ufbt            # сборка
.venv/bin/ufbt launch     # загрузить и запустить на Flipper по USB
```

Тесты кодека запускаются на хосте:

```bash
make -C tests
```

Они проверяют разбор, сборку и CRC на 13 реальных пакетах, захваченных Wiren Board, а также полный цикл «пакет → радиоимпульсы с джиттером ±10% → декодер» и устойчивость к шуму.

## Протокол

OOK, Manchester, полубит 500 мкс (данные `1` = low,high; `0` = high,low). Передача: преамбула-меандр (41 период), пауза, пакет, пауза 1,5 мс, пакет… (Flipper шлёт 3 копии).

Пакет, все поля LSB first:

```
start(1)=1 | flip(1) | cmd(4 при fmt<4, иначе 8) | args(0..4 байта) | addr_lo(8) | addr_hi(8) | fmt(8) | crc8(8)
```

- `flip` меняется при каждом нажатии, копии одного нажатия совпадают.
- `fmt`: 0 — без аргументов, 1 — яркость, 3 — RGB (r,g,b,0), 4 — без аргументов с 8-битной командой, 5 — 1 байт, 6 — 2 байта, 7 — 4 байта (датчик).
- CRC-8/MAXIM (полином 0x31 reflected, init 0) по всем битам после start и до CRC, дополненным нулями спереди до целого байта.
- Датчик (cmd 21, fmt 7): `t = signed12(args[1]&0x0F : args[0]) / 10`, тип датчика `(args[1]>>4)&7` (1 — PT112, 2 — PT111), разряд батареи `args[1]&0x80`, влажность `args[2]`.

Команды: 0 Off, 1 SlowDown, 2 On, 3 SlowUp, 4 Switch, 5 SlowSwitch, 6 SetLevel, 7 LoadPreset, 8 SavePreset, 9 Unbind, 10 Stop, 11/12 Step down/up, 13 BrightReg, 15 Bind, 16 RollColor, 17 SwitchColor, 18 SwitchMode, 19 SwitchSpeed, 20 BatteryLow, 21 Sensor, 23/24 Shadow preset/level, 25 TemporaryOn (шаг 5 с).

Источники: [разбор протокола на Habr](https://habr.com/ru/company/contactless/blog/216023), [парсер Wiren Board noolite.py](https://github.com/contactless/rfm69-linux/blob/master/noolite.py).

## Проверка на железе (MTRF-64-USB + noolite2mqtt)

`tools/` содержит вспомогательные скрипты (запускать питоном из тулчейна ufbt, там есть pyserial):

- `flipper_test.py keys ok down "ok long" "up hold" "up release" …` — нажатия кнопок через CLI;
- `flipper_test.py log 10 -- -t noolite2mqtt/write/tx/60/on ++ …` — снять лог Flipper, параллельно публикуя команды в MQTT;
- `flipper_screen.py out.png` — снимок экрана Flipper через RPC.

Проверено с MTRF-64-USB (каналы 60–63):

- MTRF → Flipper: On, Off, Switch, SetLevel (fmt 1), RGB (fmt 3), TemporaryOn (fmt 5 и 6), SwitchMode/SwitchColor (fmt 4).
- Flipper → MTRF: Bind (MTRF привязал адрес к каналу 60), затем On, Off, Switch, SetLevel, SlowUp/SlowDown/Stop, RGB, RollColor, SwitchColor, SwitchMode, SwitchSpeed, Load/SavePreset, TemporaryOn.

Датчики PT111/PT112 проверены только тестами на захваченном пакете.
